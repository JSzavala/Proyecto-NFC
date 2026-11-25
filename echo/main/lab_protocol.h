#pragma once

/*
 * Protocolo Zigbee propio entre el Router Zigbee y el Gateway.
 *
 * El Router envia un comando ZCL personalizado al Gateway cada vez que
 * resuelve una entrada/salida de NFC. El Gateway es el coordinador de la
 * red, por lo que su direccion corta siempre es 0x0000.
 *
 * Cluster:  0xFC00 (personalizado, no choca con clusters ZCL estandar)
 * Endpoint: 10
 * Comando:  0x00  LAB_ZIGBEE_CMD_EVENT
 *
 * El payload puede enviarse de dos formas:
 *
 * 1) Binaria (recomendada). Usar:
 *      data.type  = ESP_ZB_ZCL_ATTR_TYPE_SET;
 *      data.size  = sizeof(lab_event_t);
 *      data.value = &evento;
 *
 * 2) Texto (util para pruebas):
 *      "v1|EQUIPO|INTEGRANTE|ENTRADA|1762812345|A1B2C3D4"
 *
 * Los nombres no deben contener el caracter '|'.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB_ZIGBEE_ENDPOINT          10
#define LAB_ZIGBEE_CLUSTER_ID        0xFC00
#define LAB_ZIGBEE_PROFILE_ID        0x0104 /* ESP_ZB_AF_HA_PROFILE_ID */
#define LAB_ZIGBEE_CMD_EVENT         0x00
#define LAB_ZIGBEE_CMD_PING          0x01
#define LAB_ZIGBEE_CMD_ACK           0x02
#define LAB_ZIGBEE_ATTR_EVENT_COUNTER 0x0000

#define LAB_PROTOCOL_VERSION         1

#define LAB_TEAM_LEN                 24
#define LAB_MEMBER_LEN               24
#define LAB_CARD_LEN                 8

typedef enum {
    LAB_EVENT_ENTRADA = 0,
    LAB_EVENT_SALIDA = 1,
    LAB_EVENT_CORRECCION = 2,
} lab_event_type_t;

typedef enum {
    LAB_SOURCE_NFC = 0,
    LAB_SOURCE_MANUAL = 1,
    LAB_SOURCE_ROUTER = 2,
} lab_event_source_t;

/*
 * Tamano fijo de 64 bytes para que quepa en un solo frame Zigbee
 * (el payload util de APS ronda los 80 bytes).
 */
typedef struct __attribute__((packed)) {
    uint8_t version;                  /* LAB_PROTOCOL_VERSION */
    uint8_t type;                     /* lab_event_type_t */
    uint8_t source;                   /* lab_event_source_t */
    uint8_t reserved;
    uint32_t timestamp;               /* Unix UTC, 0 = usar hora del Gateway */
    char team[LAB_TEAM_LEN];          /* Equipo que hace el chequeo */
    char member[LAB_MEMBER_LEN];      /* Integrante (opcional) */
    char card[LAB_CARD_LEN];          /* UID de la tarjeta NFC (opcional) */
} lab_event_t;

_Static_assert(sizeof(lab_event_t) == 64, "lab_event_t debe medir 64 bytes");

/* Devuelve true si pudo interpretar el payload (binario o texto). */
bool lab_event_parse(const void *data, size_t size, lab_event_t *out);

const char *lab_event_type_name(uint8_t type);
bool lab_event_type_from_name(const char *name, uint8_t *out);
const char *lab_event_source_name(uint8_t source);
