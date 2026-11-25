#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lab_protocol.h"

/* Crea la cola y la tarea que publica en ThingSpeak. */
esp_err_t thingspeak_init(void);

/* Encola un evento para publicarlo. No bloquea. */
bool thingspeak_enqueue(const lab_event_t *event);
