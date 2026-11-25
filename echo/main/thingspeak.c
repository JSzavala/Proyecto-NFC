#include "thingspeak.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "thingspeak";

#define THINGSPEAK_QUEUE_LEN 32
#define THINGSPEAK_TASK_STACK 6144

static QueueHandle_t s_queue;
static int64_t s_last_publish_us;

static bool thingspeak_configured(void)
{
    return CONFIG_LAB_THINGSPEAK_API_KEY[0] != '\0';
}

static void url_encode(char *destination, size_t destination_len, const char *source)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;

    if (destination_len == 0) {
        return;
    }

    for (const unsigned char *p = (const unsigned char *)source; *p != '\0' && out + 1 < destination_len; p++) {
        bool safe = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' ||
                    *p == '_' || *p == '.' || *p == '~';

        if (safe) {
            destination[out++] = (char)*p;
        } else if (out + 3 < destination_len) {
            destination[out++] = '%';
            destination[out++] = hex[*p >> 4];
            destination[out++] = hex[*p & 0x0F];
        } else {
            break;
        }
    }

    destination[out] = '\0';
}

static void format_timestamp(uint32_t timestamp, char *buffer, size_t buffer_len)
{
    if (timestamp < 1600000000U) {
        snprintf(buffer, buffer_len, "%lu", (unsigned long)timestamp);
        return;
    }

    time_t value = (time_t)timestamp;
    struct tm local;
    localtime_r(&value, &local);
    strftime(buffer, buffer_len, "%Y-%m-%d %H:%M:%S", &local);
}

static bool thingspeak_publish(const lab_event_t *event)
{
    char api_key[96];
    char team[96];
    char member[96];
    char card[48];
    char timestamp[48];
    char payload[512];

    url_encode(api_key, sizeof(api_key), CONFIG_LAB_THINGSPEAK_API_KEY);
    url_encode(team, sizeof(team), event->team);
    url_encode(member, sizeof(member), event->member);
    url_encode(card, sizeof(card), event->card);
    format_timestamp(event->timestamp, timestamp, sizeof(timestamp));

    int length = snprintf(payload, sizeof(payload),
                          "api_key=%s&field1=%s&field2=%s&field3=%s&field4=%s&field5=%s&field6=%s", api_key, team,
                          lab_event_type_name(event->type), member, timestamp, card,
                          lab_event_source_name(event->source));
    if (length <= 0 || length >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Payload de ThingSpeak demasiado grande");
        return false;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_LAB_THINGSPEAK_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "No se pudo crear el cliente HTTP");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, payload, length);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err == ESP_OK && status == 200) {
        char response[32] = {0};
        int read = esp_http_client_read_response(client, response, sizeof(response) - 1);
        if (read > 0) {
            response[read] = '\0';
            ESP_LOGI(TAG, "ThingSpeak OK (entry %s)", response);
        } else {
            ESP_LOGI(TAG, "ThingSpeak OK");
        }
    } else {
        ESP_LOGW(TAG, "ThingSpeak fallo: %s (HTTP %d)", esp_err_to_name(err), status);
    }

    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200;
}

static void thingspeak_task(void *argument)
{
    (void)argument;

    lab_event_t event;
    while (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdPASS) {
        /* ThingSpeak gratuito permite una actualizacion cada 15 segundos. */
        int64_t now = esp_timer_get_time();
        int64_t minimum_interval = (int64_t)CONFIG_LAB_THINGSPEAK_MIN_INTERVAL_S * 1000000LL;

        if (s_last_publish_us != 0) {
            int64_t elapsed = now - s_last_publish_us;
            if (elapsed < minimum_interval) {
                int64_t remaining_ms = (minimum_interval - elapsed) / 1000;
                ESP_LOGI(TAG, "Esperando %lld ms para respetar el limite de ThingSpeak",
                         (long long)remaining_ms);
                vTaskDelay(pdMS_TO_TICKS(remaining_ms));
            }
        }

        ESP_LOGI(TAG, "Publicando %s de %s", lab_event_type_name(event.type), event.team);
        if (thingspeak_publish(&event)) {
            s_last_publish_us = esp_timer_get_time();
        } else {
            /* Si fallo, reintentamos el siguiente evento sin castigar la cola. */
            s_last_publish_us = esp_timer_get_time();
        }
    }

    vTaskDelete(NULL);
}

esp_err_t thingspeak_init(void)
{
    if (!thingspeak_configured()) {
        ESP_LOGW(TAG, "CONFIG_LAB_THINGSPEAK_API_KEY esta vacio; no se publicara en ThingSpeak");
        return ESP_OK;
    }

    s_queue = xQueueCreate(THINGSPEAK_QUEUE_LEN, sizeof(lab_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(thingspeak_task, "thingspeak", THINGSPEAK_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ThingSpeak listo (%s)", CONFIG_LAB_THINGSPEAK_URL);
    return ESP_OK;
}

bool thingspeak_enqueue(const lab_event_t *event)
{
    if (!event || !s_queue) {
        return false;
    }

    if (xQueueSend(s_queue, event, 0) != pdPASS) {
        ESP_LOGW(TAG, "Cola de ThingSpeak llena; se descarta el evento de %s", event->team);
        return false;
    }

    return true;
}
