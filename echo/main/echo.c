#include <stdlib.h>
#include <time.h>

#include "discord_bot.h"
#include "esp_coexist.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lab_store.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "thingspeak.h"
#include "zigbee_gateway.h"

static const char *TAG = "lab_gateway";

static void time_sync_start(void)
{
    setenv("TZ", CONFIG_LAB_TIMEZONE, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_LAB_SNTP_SERVER);
    config.wait_for_sync = false;
    config.start = true;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo iniciar SNTP: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "SNTP iniciado (%s)", CONFIG_LAB_SNTP_SERVER);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(lab_store_init());

#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_SOC_IEEE802154_SUPPORTED
    ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());
#endif

    ESP_LOGI(TAG, "Conectando Wi-Fi...");
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "Wi-Fi conectado");

    time_sync_start();

    ESP_ERROR_CHECK(thingspeak_init());
    ESP_ERROR_CHECK(discord_bot_start());
    ESP_ERROR_CHECK(zigbee_gateway_start());

    ESP_LOGI(TAG, "Gateway listo. Comandos: !equipo, !equipo <nombre>, !historial [N]");
}
