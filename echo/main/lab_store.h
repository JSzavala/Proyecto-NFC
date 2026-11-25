#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "lab_protocol.h"

#define LAB_STORE_CAPACITY 64

/* Carga el historial persistido en NVS y prepara el buffer circular. */
esp_err_t lab_store_init(void);

/* Agrega un evento al historial (RAM + NVS). */
esp_err_t lab_store_append(const lab_event_t *event);

size_t lab_store_count(void);

/* index = 0 devuelve el evento mas reciente. */
bool lab_store_get_recent(size_t index, lab_event_t *out);

/*
 * Formatea los ultimos max_events eventos en formato listo para Discord.
 * Devuelve la cantidad de caracteres escritos.
 */
size_t lab_store_format_history(char *buffer, size_t buffer_len, size_t max_events);

/*
 * Formatea los equipos que estan actualmente dentro del laboratorio.
 * Si filter no es NULL/vacio, muestra el ultimo evento de ese equipo
 * sin importar si esta dentro o fuera.
 */
size_t lab_store_format_teams(char *buffer, size_t buffer_len, const char *filter);
