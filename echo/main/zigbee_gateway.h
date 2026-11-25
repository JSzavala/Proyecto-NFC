#pragma once

#include "esp_err.h"

/* Inicia el stack Zigbee y las tareas de procesamiento de eventos. */
esp_err_t zigbee_gateway_start(void);
