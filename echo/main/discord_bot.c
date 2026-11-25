#include "discord_bot.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "discord.h"
#include "discord/message.h"
#include "discord/session.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lab_store.h"

static const char *TAG = "discord_bot";

#define DISCORD_RESPONSE_LEN 1900
#define DISCORD_NOTIFY_QUEUE_LEN 16
#define DISCORD_NOTIFY_TASK_STACK 6144

static discord_handle_t s_bot;
static QueueHandle_t s_notify_queue;
static SemaphoreHandle_t s_channel_mutex;
static char s_notify_channel[32] = CONFIG_LAB_DISCORD_NOTIFY_CHANNEL;

static void trim_in_place(char *text)
{
    if (!text) {
        return;
    }

    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
}

static const char *command_argument(const char *content, const char *command)
{
    size_t length = strlen(command);
    if (strncasecmp(content, command, length) != 0) {
        return NULL;
    }

    if (content[length] != '\0' && !isspace((unsigned char)content[length])) {
        return NULL;
    }

    const char *argument = content + length;
    while (*argument != '\0' && isspace((unsigned char)*argument)) {
        argument++;
    }
    return argument;
}

static esp_err_t send_text(const char *channel_id, const char *text)
{
    if (!s_bot || !channel_id || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    discord_message_t message = {
        .content = (char *)text,
        .channel_id = (char *)channel_id,
    };

    esp_err_t err = discord_message_send(s_bot, &message, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo enviar el mensaje: %s", esp_err_to_name(err));
    }
    return err;
}

static void remember_channel(const char *channel_id)
{
    if (!s_channel_mutex || !channel_id || channel_id[0] == '\0') {
        return;
    }

    xSemaphoreTake(s_channel_mutex, portMAX_DELAY);
    if (CONFIG_LAB_DISCORD_NOTIFY_CHANNEL[0] == '\0') {
        strncpy(s_notify_channel, channel_id, sizeof(s_notify_channel) - 1);
        s_notify_channel[sizeof(s_notify_channel) - 1] = '\0';
    }
    xSemaphoreGive(s_channel_mutex);
}

static void get_notify_channel(char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!s_channel_mutex) {
        return;
    }

    xSemaphoreTake(s_channel_mutex, portMAX_DELAY);
    strncpy(buffer, s_notify_channel, buffer_len - 1);
    buffer[buffer_len - 1] = '\0';
    xSemaphoreGive(s_channel_mutex);
}

static void discord_notify_task(void *argument)
{
    (void)argument;

    lab_event_t event;
    while (xQueueReceive(s_notify_queue, &event, portMAX_DELAY) == pdPASS) {
        char channel[32];
        get_notify_channel(channel, sizeof(channel));
        if (channel[0] == '\0') {
            continue;
        }

        char text[256];
        char when[24];
        if (event.timestamp >= 1600000000U) {
            time_t value = (time_t)event.timestamp;
            struct tm local;
            localtime_r(&value, &local);
            strftime(when, sizeof(when), "%d/%m %H:%M", &local);
        } else {
            snprintf(when, sizeof(when), "sin hora");
        }

        if (event.member[0] != '\0' && event.card[0] != '\0') {
            snprintf(text, sizeof(text), "%s: %s - %s [%s] (%s)", lab_event_type_name(event.type), event.team,
                     event.member, event.card, when);
        } else if (event.member[0] != '\0') {
            snprintf(text, sizeof(text), "%s: %s - %s (%s)", lab_event_type_name(event.type), event.team,
                     event.member, when);
        } else if (event.card[0] != '\0') {
            snprintf(text, sizeof(text), "%s: %s [%s] (%s)", lab_event_type_name(event.type), event.team,
                     event.card, when);
        } else {
            snprintf(text, sizeof(text), "%s: %s (%s)", lab_event_type_name(event.type), event.team, when);
        }

        send_text(channel, text);
    }

    vTaskDelete(NULL);
}

static void handle_equipo(const char *channel_id, const char *argument)
{
    char *response = malloc(DISCORD_RESPONSE_LEN);
    if (!response) {
        return;
    }

    char filter[LAB_TEAM_LEN] = {0};
    if (argument && argument[0] != '\0') {
        strncpy(filter, argument, sizeof(filter) - 1);
        trim_in_place(filter);
    }

    lab_store_format_teams(response, DISCORD_RESPONSE_LEN, filter[0] ? filter : NULL);
    send_text(channel_id, response);
    free(response);
}

static void handle_historial(const char *channel_id, const char *argument)
{
    size_t requested = CONFIG_LAB_HISTORY_MAX;

    if (argument && argument[0] != '\0') {
        char *end = NULL;
        unsigned long value = strtoul(argument, &end, 10);
        if (end != argument && value > 0) {
            requested = (size_t)value;
        }
    }

    if (requested > 30) {
        requested = 30;
    }

    char *response = malloc(DISCORD_RESPONSE_LEN);
    if (!response) {
        return;
    }

    lab_store_format_history(response, DISCORD_RESPONSE_LEN, requested);
    send_text(channel_id, response);
    free(response);
}

static void handle_help(const char *channel_id)
{
    send_text(channel_id,
              "Comandos del laboratorio:\n"
              "!equipo - equipos dentro del laboratorio\n"
              "!equipo <nombre> - ultimo evento de un equipo\n"
              "!historial [N] - ultimos N eventos (1-30)");
}

static void handle_message(discord_message_t *message)
{
    if (!message || !message->author || !message->content || !message->channel_id) {
        ESP_LOGW(TAG, "Mensaje incompleto");
        return;
    }

    if (message->author->bot) {
        return;
    }

    remember_channel(message->channel_id);

    char content[256];
    strncpy(content, message->content, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    trim_in_place(content);

    const char *argument = command_argument(content, "!equipo");
    if (argument) {
        handle_equipo(message->channel_id, argument);
        return;
    }

    argument = command_argument(content, "!historial");
    if (argument) {
        handle_historial(message->channel_id, argument);
        return;
    }

    if (command_argument(content, "!ayuda") || command_argument(content, "!help")) {
        handle_help(message->channel_id);
    }
}

static void bot_event_handler(void *handler_arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_arg;
    (void)base;

    discord_event_data_t *data = (discord_event_data_t *)event_data;

    switch (event_id) {
    case DISCORD_EVENT_CONNECTED: {
        discord_session_t *session = (discord_session_t *)data->ptr;
        ESP_LOGI(TAG, "Bot %s#%s conectado", session->user->username, session->user->discriminator);
    } break;

    case DISCORD_EVENT_MESSAGE_RECEIVED: {
        discord_message_t *message = (discord_message_t *)data->ptr;
        if (!message || !message->author || !message->content || !message->channel_id) {
            break;
        }

        ESP_LOGI(TAG, "Mensaje de %s#%s: %s", message->author->username ? message->author->username : "desconocido",
                 message->author->discriminator ? message->author->discriminator : "desconocido",
                 message->content);

        handle_message(message);
    } break;

    case DISCORD_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Bot desconectado");
        break;

    default:
        break;
    }
}

esp_err_t discord_bot_start(void)
{
    s_channel_mutex = xSemaphoreCreateMutex();
    if (!s_channel_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_notify_queue = xQueueCreate(DISCORD_NOTIFY_QUEUE_LEN, sizeof(lab_event_t));
    if (!s_notify_queue) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(discord_notify_task, "discord_notify", DISCORD_NOTIFY_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    discord_config_t config = {
        .intents = DISCORD_INTENT_GUILD_MESSAGES | DISCORD_INTENT_MESSAGE_CONTENT,
    };

    s_bot = discord_create(&config);
    if (!s_bot) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(discord_register_events(s_bot, DISCORD_EVENT_CONNECTED, bot_event_handler, NULL));
    ESP_ERROR_CHECK(discord_register_events(s_bot, DISCORD_EVENT_MESSAGE_RECEIVED, bot_event_handler, NULL));
    ESP_ERROR_CHECK(discord_register_events(s_bot, DISCORD_EVENT_DISCONNECTED, bot_event_handler, NULL));

    esp_err_t err = discord_login(s_bot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar sesion en Discord: %s", esp_err_to_name(err));
    }

    return err;
}

void discord_bot_notify_event(const lab_event_t *event)
{
    if (!event || !s_notify_queue) {
        return;
    }

    if (xQueueSend(s_notify_queue, event, 0) != pdPASS) {
        ESP_LOGW(TAG, "Cola de notificaciones llena; se descarta el evento de %s", event->team);
    }
}
