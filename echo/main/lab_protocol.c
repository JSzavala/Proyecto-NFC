#include "lab_protocol.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_field(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    if (dst_len == 0) {
        return;
    }
    if (src_len >= dst_len) {
        src_len = dst_len - 1;
    }
    if (src_len > 0) {
        memcpy(dst, src, src_len);
    }
    dst[src_len] = '\0';
}

static void sanitize_event(lab_event_t *event)
{
    event->team[LAB_TEAM_LEN - 1] = '\0';
    event->member[LAB_MEMBER_LEN - 1] = '\0';
    event->card[LAB_CARD_LEN - 1] = '\0';
}

const char *lab_event_type_name(uint8_t type)
{
    switch (type) {
    case LAB_EVENT_ENTRADA:
        return "ENTRADA";
    case LAB_EVENT_SALIDA:
        return "SALIDA";
    case LAB_EVENT_CORRECCION:
        return "CORRECCION";
    default:
        return "DESCONOCIDO";
    }
}

bool lab_event_type_from_name(const char *name, uint8_t *out)
{
    if (!name || !out) {
        return false;
    }
    if (strcasecmp(name, "ENTRADA") == 0 || strcasecmp(name, "IN") == 0 || strcasecmp(name, "CHECKIN") == 0) {
        *out = LAB_EVENT_ENTRADA;
        return true;
    }
    if (strcasecmp(name, "SALIDA") == 0 || strcasecmp(name, "OUT") == 0 || strcasecmp(name, "CHECKOUT") == 0) {
        *out = LAB_EVENT_SALIDA;
        return true;
    }
    if (strcasecmp(name, "CORRECCION") == 0 || strcasecmp(name, "CORRECCION_SALIDA") == 0 ||
        strcasecmp(name, "FIX") == 0) {
        *out = LAB_EVENT_CORRECCION;
        return true;
    }
    return false;
}

const char *lab_event_source_name(uint8_t source)
{
    switch (source) {
    case LAB_SOURCE_NFC:
        return "NFC";
    case LAB_SOURCE_MANUAL:
        return "MANUAL";
    case LAB_SOURCE_ROUTER:
        return "ROUTER";
    default:
        return "DESCONOCIDO";
    }
}

static bool parse_text_event(const char *text, size_t len, lab_event_t *out)
{
    char buffer[160];
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, text, len);
    buffer[len] = '\0';

    char *fields[6] = {0};
    int field_count = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(buffer, "|", &saveptr); token && field_count < 6;
         token = strtok_r(NULL, "|", &saveptr)) {
        fields[field_count++] = token;
    }

    /* version|equipo|integrante|evento|timestamp|tarjeta */
    if (field_count < 5) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->version = LAB_PROTOCOL_VERSION;
    out->source = LAB_SOURCE_NFC;

    copy_field(out->team, sizeof(out->team), fields[1], strlen(fields[1]));
    copy_field(out->member, sizeof(out->member), fields[2], strlen(fields[2]));

    if (!lab_event_type_from_name(fields[3], &out->type)) {
        return false;
    }

    out->timestamp = (uint32_t)strtoul(fields[4], NULL, 10);

    if (field_count >= 6) {
        copy_field(out->card, sizeof(out->card), fields[5], strlen(fields[5]));
    }

    sanitize_event(out);
    return out->team[0] != '\0';
}

bool lab_event_parse(const void *data, size_t size, lab_event_t *out)
{
    if (!data || !out || size == 0) {
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)data;

    /* Formato binario: version == LAB_PROTOCOL_VERSION. */
    if (size == sizeof(lab_event_t) && bytes[0] == LAB_PROTOCOL_VERSION) {
        memcpy(out, data, sizeof(*out));
        sanitize_event(out);
        return out->team[0] != '\0';
    }

    /* Algunos emisores agregan el byte de longitud de los strings ZCL. */
    if (size > 2 && bytes[0] == (uint8_t)(size - 1) && isprint(bytes[1])) {
        bytes++;
        size--;
    }

    if (isprint(bytes[0])) {
        return parse_text_event((const char *)bytes, size, out);
    }

    return false;
}
