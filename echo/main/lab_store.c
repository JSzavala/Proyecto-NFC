#include "lab_store.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "lab_store";

#define LAB_STORE_NAMESPACE "labstore"
#define LAB_STORE_KEY_COUNT "count"
#define LAB_STORE_KEY_HEAD  "head"
#define LAB_STORE_TEAMS_MAX 12

typedef struct {
    char team[LAB_TEAM_LEN];
    lab_event_t last;
} team_state_t;

static lab_event_t s_events[LAB_STORE_CAPACITY];
static size_t s_count;
static size_t s_head; /* Proximo indice a escribir. */
static SemaphoreHandle_t s_mutex;
static nvs_handle_t s_nvs;
static bool s_nvs_ready;

static void event_key(char *buffer, size_t buffer_len, size_t index)
{
    snprintf(buffer, buffer_len, "e%03u", (unsigned)index);
}

static void append_text(char *buffer, size_t buffer_len, size_t *offset, const char *fmt, ...)
{
    if (*offset >= buffer_len) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *offset, buffer_len - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    *offset += (size_t)written;
    if (*offset >= buffer_len) {
        *offset = buffer_len - 1;
        buffer[*offset] = '\0';
    }
}

static void format_event_time(uint32_t timestamp, char *buffer, size_t buffer_len)
{
    /* Si el Gateway aun no sincronizo la hora, no mostramos una fecha falsa. */
    if (timestamp < 1600000000U) {
        snprintf(buffer, buffer_len, "sin hora");
        return;
    }

    time_t value = (time_t)timestamp;
    struct tm local;
    localtime_r(&value, &local);
    strftime(buffer, buffer_len, "%d/%m %H:%M", &local);
}

esp_err_t lab_store_init(void)
{
    if (s_mutex) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    s_count = 0;
    s_head = 0;

    esp_err_t err = nvs_open(LAB_STORE_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir NVS (%s); el historial sera solo en RAM", esp_err_to_name(err));
        return ESP_OK;
    }
    s_nvs_ready = true;

    uint32_t count = 0;
    uint32_t head = 0;
    nvs_get_u32(s_nvs, LAB_STORE_KEY_COUNT, &count);
    nvs_get_u32(s_nvs, LAB_STORE_KEY_HEAD, &head);

    if (count > LAB_STORE_CAPACITY) {
        count = LAB_STORE_CAPACITY;
    }
    head %= LAB_STORE_CAPACITY;

    size_t loaded = 0;
    for (size_t i = 0; i < count; i++) {
        size_t index = (head + LAB_STORE_CAPACITY - count + i) % LAB_STORE_CAPACITY;
        char key[8];
        event_key(key, sizeof(key), index);

        size_t blob_len = sizeof(lab_event_t);
        lab_event_t event;
        if (nvs_get_blob(s_nvs, key, &event, &blob_len) == ESP_OK && blob_len == sizeof(lab_event_t)) {
            s_events[index] = event;
            loaded++;
        }
    }

    s_count = loaded;
    s_head = head;

    ESP_LOGI(TAG, "Historial cargado: %u evento(s)", (unsigned)s_count);
    return ESP_OK;
}

esp_err_t lab_store_append(const lab_event_t *event)
{
    if (!event || !s_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t index = s_head;
    s_events[index] = *event;
    s_head = (s_head + 1) % LAB_STORE_CAPACITY;
    if (s_count < LAB_STORE_CAPACITY) {
        s_count++;
    }

    if (s_nvs_ready) {
        char key[8];
        event_key(key, sizeof(key), index);
        esp_err_t err = nvs_set_blob(s_nvs, key, event, sizeof(*event));
        if (err == ESP_OK) {
            err = nvs_set_u32(s_nvs, LAB_STORE_KEY_HEAD, (uint32_t)s_head);
        }
        if (err == ESP_OK) {
            err = nvs_set_u32(s_nvs, LAB_STORE_KEY_COUNT, (uint32_t)s_count);
        }
        if (err == ESP_OK) {
            err = nvs_commit(s_nvs);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo persistir el evento: %s", esp_err_to_name(err));
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

size_t lab_store_count(void)
{
    if (!s_mutex) {
        return 0;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t count = s_count;
    xSemaphoreGive(s_mutex);
    return count;
}

bool lab_store_get_recent(size_t index, lab_event_t *out)
{
    if (!out || !s_mutex) {
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool found = false;
    if (index < s_count) {
        size_t position = (s_head + LAB_STORE_CAPACITY - 1 - index) % LAB_STORE_CAPACITY;
        *out = s_events[position];
        found = true;
    }
    xSemaphoreGive(s_mutex);
    return found;
}

size_t lab_store_format_history(char *buffer, size_t buffer_len, size_t max_events)
{
    if (!buffer || buffer_len == 0) {
        return 0;
    }

    if (max_events > LAB_STORE_CAPACITY) {
        max_events = LAB_STORE_CAPACITY;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t total = s_count;
    size_t shown = total < max_events ? total : max_events;
    size_t offset = 0;

    append_text(buffer, buffer_len, &offset, "Historial del laboratorio (ultimos %u de %u)\n\n",
                (unsigned)shown, (unsigned)total);

    if (total == 0) {
        append_text(buffer, buffer_len, &offset, "Todavia no hay eventos registrados.\n");
    }

    for (size_t i = 0; i < shown; i++) {
        size_t position = (s_head + LAB_STORE_CAPACITY - 1 - i) % LAB_STORE_CAPACITY;
        const lab_event_t *event = &s_events[position];

        char when[24];
        format_event_time(event->timestamp, when, sizeof(when));

        append_text(buffer, buffer_len, &offset, "%s  %-11s  %s", when, lab_event_type_name(event->type),
                    event->team);

        if (event->member[0] != '\0') {
            append_text(buffer, buffer_len, &offset, " - %s", event->member);
        }
        if (event->card[0] != '\0') {
            append_text(buffer, buffer_len, &offset, " [%s]", event->card);
        }
        append_text(buffer, buffer_len, &offset, "\n");
    }

    xSemaphoreGive(s_mutex);
    return offset;
}

static team_state_t *find_team(team_state_t *states, size_t states_len, const char *team)
{
    for (size_t i = 0; i < states_len; i++) {
        if (strncmp(states[i].team, team, LAB_TEAM_LEN) == 0) {
            return &states[i];
        }
    }
    return NULL;
}

size_t lab_store_format_teams(char *buffer, size_t buffer_len, const char *filter)
{
    if (!buffer || buffer_len == 0) {
        return 0;
    }

    bool has_filter = filter && filter[0] != '\0';

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    team_state_t states[LAB_STORE_TEAMS_MAX];
    size_t states_len = 0;

    for (size_t i = 0; i < s_count && states_len < LAB_STORE_TEAMS_MAX; i++) {
        size_t position = (s_head + LAB_STORE_CAPACITY - 1 - i) % LAB_STORE_CAPACITY;
        const lab_event_t *event = &s_events[position];

        if (has_filter && strcasecmp(event->team, filter) != 0) {
            continue;
        }

        if (find_team(states, states_len, event->team)) {
            continue;
        }

        strncpy(states[states_len].team, event->team, LAB_TEAM_LEN - 1);
        states[states_len].team[LAB_TEAM_LEN - 1] = '\0';
        states[states_len].last = *event;
        states_len++;
    }

    size_t offset = 0;

    if (has_filter) {
        if (states_len == 0) {
            append_text(buffer, buffer_len, &offset, "No hay registros para el equipo \"%s\".\n", filter);
        } else {
            const team_state_t *state = &states[0];
            char when[24];
            format_event_time(state->last.timestamp, when, sizeof(when));

            append_text(buffer, buffer_len, &offset, "Equipo: %s\n", state->team);
            append_text(buffer, buffer_len, &offset, "Ultimo evento: %s (%s)\n",
                        lab_event_type_name(state->last.type), when);
            append_text(buffer, buffer_len, &offset, "Integrante: %s\n",
                        state->last.member[0] ? state->last.member : "sin dato");
            append_text(buffer, buffer_len, &offset, "Tarjeta: %s\n",
                        state->last.card[0] ? state->last.card : "sin dato");
        }
    } else {
        size_t inside = 0;
        for (size_t i = 0; i < states_len; i++) {
            if (states[i].last.type == LAB_EVENT_ENTRADA) {
                inside++;
            }
        }

        append_text(buffer, buffer_len, &offset, "Equipos dentro del laboratorio: %u\n", (unsigned)inside);

        if (inside == 0) {
            append_text(buffer, buffer_len, &offset, "No hay equipos dentro en este momento.\n");
        } else {
            append_text(buffer, buffer_len, &offset, "\n");
            for (size_t i = 0; i < states_len; i++) {
                if (states[i].last.type != LAB_EVENT_ENTRADA) {
                    continue;
                }

                char when[24];
                format_event_time(states[i].last.timestamp, when, sizeof(when));

                append_text(buffer, buffer_len, &offset, "- %s", states[i].team);
                if (states[i].last.member[0] != '\0') {
                    append_text(buffer, buffer_len, &offset, " - %s", states[i].last.member);
                }
                append_text(buffer, buffer_len, &offset, " (entrada %s)\n", when);
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return offset;
}
