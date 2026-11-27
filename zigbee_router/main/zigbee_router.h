#ifndef ZIGBEE_ROUTER_H
#define ZIGBEE_ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_zigbee_core.h"

// ============================================================================
// CONFIGURACION DE RED ZIGBEE (COMPARTIDA ENTRE LAS 3 PLACAS)
// ============================================================================

#define INSTALLCODE_POLICY_ENABLE       false
#define ZB_ROUTER_ENDPOINT              10
#define ZB_END_DEVICE_ENDPOINT          10
#define ZB_GATEWAY_ENDPOINT             10

// Canal primario Zigbee (Canal 13 por defecto, mascara para todos los canales)
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

// Direccion corta del Coordinador / Gateway en la red Zigbee (0x0000 por estandar)
#define ZB_GATEWAY_SHORT_ADDR           0x0000

// ============================================================================
// CLUSTER PERSONALIZADO Y COMANDOS PARA EL SISTEMA DE LABORATORIO CISCO
// ============================================================================

#define LAB_CUSTOM_CLUSTER_ID           0xFF00

// Comandos ZCL personalizados entre End Device (Jose), Router (Nosotros) y Gateway (Sam)
typedef enum {
    CMD_NFC_UID_EVENT       = 0x01, // End Device (Jose) -> Router: Envia UID leido por PN532
    CMD_NOTIFY_GATEWAY      = 0x02, // Router -> Gateway (Sam): Notifica entrada/salida en tiempo real
    CMD_QUERY_EQUIPO        = 0x03, // Gateway (Sam) -> Router: Solicita estado para comando !equipo
    CMD_RESP_EQUIPO         = 0x04, // Router -> Gateway (Sam): Respuesta con el equipo actual
    CMD_QUERY_HISTORIAL     = 0x05, // Gateway (Sam) -> Router: Solicita datos para comando !historial
    CMD_RESP_HISTORIAL      = 0x06, // Router -> Gateway (Sam): Respuesta con el historial consolidado
} lab_zigbee_cmd_t;

// Tipos de evento para ThingSpeak (field2) y Discord
typedef enum {
    EVENTO_ENTRADA          = 1,
    EVENTO_SALIDA           = 2,
    EVENTO_AUTO_SALIDA      = 3,    // Correccion cuando el equipo anterior olvido hacer check-out
} tipo_evento_lab_t;

// Estructura de un evento recibido en la cola de FreeRTOS
typedef struct {
    uint8_t uid[10];
    uint8_t uid_len;
    uint16_t src_short_addr;
} evento_nfc_msg_t;

// Estructura persistida en NVS del estado actual del laboratorio
typedef struct {
    bool ocupado;
    char uid_hex[21];
    char nombre_equipo[32];
    int64_t timestamp_entrada_seg;
} estado_laboratorio_t;

// Configuracion por defecto del Router Zigbee
#define ESP_ZB_DEFAULT_ROUTER_CONFIG()                                  \
    {                                                                   \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,               \
        .nwk_cfg.zczr_cfg = {                                           \
            .max_children = 10,                                         \
        },                                                              \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                                   \
    {                                                                   \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                             \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                                    \
    {                                                                   \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,           \
    }

#endif // ZIGBEE_ROUTER_H

