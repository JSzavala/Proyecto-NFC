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

// Canal primario Zigbee (Canal 13 por defecto en el Gateway, mascara abierta para escaneo)
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

// Direccion corta del Coordinador / Gateway en la red Zigbee (0x0000 por estandar)
#define ZB_GATEWAY_SHORT_ADDR           0x0000

// ============================================================================
// CLUSTERS Y PROTOCOLOS ZIGBEE COMPATIBLES (GATEWAY Y END DEVICE NFC)
// ============================================================================

// Cluster 0xFC00 usado por el Gateway (Gateway/echo/main/lab_protocol.h)
#define LAB_ZIGBEE_CLUSTER_ID           0xFC00
#define LAB_ZIGBEE_CMD_EVENT            0x00
#define LAB_ZIGBEE_CMD_PING             0x01
#define LAB_ZIGBEE_CMD_ACK              0x02

// Cluster 0xFF00 adicional para compatibilidad con End Device / comandos legacy
#define LAB_CUSTOM_CLUSTER_ID           0xFF00

#define LAB_PROTOCOL_VERSION            1
#define LAB_TEAM_LEN                    24
#define LAB_MEMBER_LEN                  24
#define LAB_CARD_LEN                    8

// Tipos de evento de lab_protocol.h (Gateway)
typedef enum {
    LAB_EVENT_ENTRADA    = 0,
    LAB_EVENT_SALIDA     = 1,
    LAB_EVENT_CORRECCION = 2,
} lab_event_type_t;

// Origen del evento de lab_protocol.h (Gateway)
typedef enum {
    LAB_SOURCE_NFC    = 0,
    LAB_SOURCE_MANUAL = 1,
    LAB_SOURCE_ROUTER = 2,
} lab_event_source_t;

// Estructura binaria exacta de 64 bytes esperada por el Gateway (lab_event_t)
typedef struct __attribute__((packed)) {
    uint8_t  version;                 /* LAB_PROTOCOL_VERSION = 1 */
    uint8_t  type;                    /* lab_event_type_t: 0=ENTRADA, 1=SALIDA, 2=CORRECCION */
    uint8_t  source;                  /* lab_event_source_t: 0=NFC, 1=MANUAL, 2=ROUTER */
    uint8_t  reserved;
    uint32_t timestamp;               /* Unix UTC; 0 = usar la hora SNTP del Gateway */
    char     team[LAB_TEAM_LEN];      /* Nombre del equipo */
    char     member[LAB_MEMBER_LEN];  /* Nombre del integrante */
    char     card[LAB_CARD_LEN];      /* UID de la tarjeta NFC en hex (ej. "04A1B2C") */
} lab_event_t;

_Static_assert(sizeof(lab_event_t) == 64, "lab_event_t debe medir 64 bytes");

// Comandos ZCL personalizados adicionales (compatibilidad con End Device y consultas)
typedef enum {
    CMD_NFC_UID_EVENT       = 0x01, // End Device -> Router: Envia UID leido por PN532
    CMD_NOTIFY_GATEWAY      = 0x02, // Router -> Gateway: Notifica entrada/salida
    CMD_QUERY_EQUIPO        = 0x03, // Gateway -> Router: Solicita estado para comando !equipo
    CMD_RESP_EQUIPO         = 0x04, // Router -> Gateway: Respuesta con el equipo actual
    CMD_QUERY_HISTORIAL     = 0x05, // Gateway -> Router: Solicita datos para comando !historial
    CMD_RESP_HISTORIAL      = 0x06, // Router -> Gateway: Respuesta con el historial consolidado
} lab_zigbee_cmd_t;

// Tipos de evento para historial/compatibilidad
typedef enum {
    EVENTO_ENTRADA          = 1,
    EVENTO_SALIDA           = 2,
    EVENTO_AUTO_SALIDA      = 3,    // Correccion cuando el equipo anterior olvido hacer check-out
} tipo_evento_lab_t;

// Estructura de un evento recibido desde el End Device NFC en la cola de FreeRTOS
typedef struct {
    uint8_t  uid[10];
    uint8_t  uid_len;
    uint16_t src_short_addr;
    bool     es_evento_estructurado;
    lab_event_t evento_recibido;
} evento_nfc_msg_t;

// Estructura persistida en NVS del estado actual del laboratorio
typedef struct {
    bool    ocupado;
    char    uid_hex[21];
    char    nombre_equipo[LAB_TEAM_LEN];
    char    nombre_integrante[LAB_MEMBER_LEN];
    int64_t timestamp_entrada_seg;
} estado_laboratorio_t;

// Configuracion por defecto del Router Zigbee (rol ZIGBEE_ROUTER)
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
