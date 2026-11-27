#pragma once

#include "esp_err.h"
#include "lab_protocol.h"

/* Crea el bot de Discord y registra los manejadores de comandos. */
esp_err_t discord_bot_start(void);

/* Encola una notificacion en tiempo real para el canal configurado. */
void discord_bot_notify_event(const lab_event_t *event);
