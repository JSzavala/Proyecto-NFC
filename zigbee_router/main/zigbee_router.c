#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"

#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_command.h"

#include "zigbee_router.h"

static const char *TAG = "ROUTER_ZIGBEE";

#define NVS_NAMESPACE           "lab_cisco"
#define NVS_KEY_ESTADO          "estado_actual"
#define HISTORIAL_FILE_PATH     "/spiffs/historial.txt"

// Cola de FreeRTOS para procesar los UIDs que llegan desde el End Device de Jose
static QueueHandle_t cola_eventos_nfc = NULL;

// Estado consolidado actual del laboratorio
static estado_laboratorio_t estado_actual = {0};

// Bandera para saber si el Router ya esta unido a la red Zigbee
static bool red_zigbee_conectada = false;

// Direccion corta del Gateway de Sam (por defecto 0x0000 = Coordinador, se actualiza si envia consulta)
static uint16_t gateway_short_addr = ZB_GATEWAY_SHORT_ADDR;

// ============================================================================
// TABLA DE EQUIPOS / TARJETAS NFC (CONFIGURABLE)
// ============================================================================

typedef struct {
    const char *uid_hex;
    const char *nombre_equipo;
} registro_equipo_t;

// Aqui pueden agregar los UIDs reales que imprime el lector PN532 de Jose
static const registro_equipo_t tabla_equipos[] = {
    { "04A1B2C3", "Equipo 1" },
    { "04D4E5F6", "Equipo 2" },
    { "04112233", "Equipo 3" },
    { "04998877", "Equipo 4" },
};

static void uid_bytes_a_hex(const uint8_t *uid, uint8_t longitud, char *salida_hex, size_t max_len)
{
    size_t pos = 0;
    for (uint8_t i = 0; i < longitud && (pos + 2) < max_len; i++) {
        pos += snprintf(salida_hex + pos, max_len - pos, "%02X", uid[i]);
    }
    salida_hex[pos] = '\0';
}

static void obtener_nombre_equipo(const char *uid_hex, char *nombre_out, size_t max_len)
{
    size_t num_equipos = sizeof(tabla_equipos) / sizeof(tabla_equipos[0]);
    for (size_t i = 0; i < num_equipos; i++) {
        if (strcmp(uid_hex, tabla_equipos[i].uid_hex) == 0) {
            snprintf(nombre_out, max_len, "%s", tabla_equipos[i].nombre_equipo);
            return;
        }
    }
    // Si el UID no esta en la tabla fija, se identifica por su UID hexadecimal
    snprintf(nombre_out, max_len, "Equipo-%s", uid_hex);
}

// ============================================================================
// PERSISTENCIA EN NVS (ESTADO ACTUAL) Y SPIFFS (HISTORIAL DIARIO)
// ============================================================================

static void guardar_estado_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_ESTADO, &estado_actual, sizeof(estado_actual));
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Estado actual persistido en NVS correctamente");
    } else {
        ESP_LOGE(TAG, "Error guardando blob en NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

static void cargar_estado_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Sin estado previo en NVS (Laboratorio inicialmente libre)");
        memset(&estado_actual, 0, sizeof(estado_actual));
        return;
    }

    size_t required_size = sizeof(estado_actual);
    err = nvs_get_blob(handle, NVS_KEY_ESTADO, &estado_actual, &required_size);
    if (err == ESP_OK && estado_actual.ocupado) {
        ESP_LOGI(TAG, "Estado recuperado de NVS -> Laboratorio ocupado por: %s (UID: %s)",
                 estado_actual.nombre_equipo, estado_actual.uid_hex);
    } else {
        memset(&estado_actual, 0, sizeof(estado_actual));
        ESP_LOGI(TAG, "Estado recuperado de NVS -> Laboratorio libre");
    }

    nvs_close(handle);
}

static void iniciar_spiffs(void)
{
    ESP_LOGI(TAG, "Inicializando SPIFFS para historial diario...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al montar o formatear SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS listo: %d bytes usados de %d bytes", (int)used, (int)total);
    }
}

static void registrar_en_historial_spiffs(const char *linea)
{
    FILE *f = fopen(HISTORIAL_FILE_PATH, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "No se pudo abrir %s para escritura", HISTORIAL_FILE_PATH);
        return;
    }

    fprintf(f, "%s\n", linea);
    fclose(f);
    ESP_LOGI(TAG, "Guardado en historial SPIFFS: %s", linea);
}

static void leer_historial_reciente(char *salida, size_t max_len)
{
    FILE *f = fopen(HISTORIAL_FILE_PATH, "r");
    if (f == NULL) {
        snprintf(salida, max_len, "Sin registros en el historial del dia.");
        return;
    }

    // Leer los ultimos bytes del archivo que quepan en el payload Zigbee/Discord
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);

    if (file_size <= 0) {
        fclose(f);
        snprintf(salida, max_len, "Sin registros en el historial del dia.");
        return;
    }

    long read_len = (file_size < (long)(max_len - 1)) ? file_size : (long)(max_len - 1);
    fseek(f, file_size - read_len, SEEK_SET);

    size_t leidos = fread(salida, 1, read_len, f);
    salida[leidos] = '\0';
    fclose(f);

    // Si se corto a mitad de la primera linea, avanzar al siguiente salto de linea
    if (file_size > read_len) {
        char *primer_salto = strchr(salida, '\n');
        if (primer_salto && *(primer_salto + 1) != '\0') {
            memmove(salida, primer_salto + 1, strlen(primer_salto + 1) + 1);
        }
    }
}

// ============================================================================
// ENVIO DE DATOS POR ZIGBEE HACIA LA PLACA DE SAM (GATEWAY DISCORD/THINGSPEAK)
// ============================================================================

static void enviar_comando_zigbee_a_sam(uint16_t dst_addr, lab_zigbee_cmd_t cmd_id, const char *payload_str)
{
    if (!red_zigbee_conectada) {
        ESP_LOGW(TAG, "Red Zigbee aun no conectada; el evento quedo guardado en NVS/SPIFFS");
        return;
    }

    uint16_t len = (uint16_t)(strlen(payload_str) + 1);

    esp_zb_zcl_custom_cluster_cmd_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = ZB_GATEWAY_ENDPOINT,
            .src_endpoint = ZB_ROUTER_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .cluster_id = LAB_CUSTOM_CLUSTER_ID,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .custom_cmd_id = (uint16_t)cmd_id,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
            .size = len,
            .value = (void *)payload_str,
        },
    };

    ESP_LOGI(TAG, "Enviando a placa de Sam (0x%04X) [CMD 0x%02X]: %s", dst_addr, cmd_id, payload_str);

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    esp_zb_lock_release();
}

// ============================================================================
// LOGICA DE CONSOLIDACION DE ENTRADA / SALIDA (CHECK-IN / CHECK-OUT)
// ============================================================================

static void consolidar_evento_nfc(const evento_nfc_msg_t *msg)
{
    char uid_hex[21];
    char nombre_equipo[32];
    char linea_historial[160];
    char payload_gateway[200];

    uid_bytes_a_hex(msg->uid, msg->uid_len, uid_hex, sizeof(uid_hex));
    obtener_nombre_equipo(uid_hex, nombre_equipo, sizeof(nombre_equipo));

    int64_t ahora_seg = esp_timer_get_time() / 1000000ULL;
    int min_transcurridos = (int)(ahora_seg / 60);

    ESP_LOGI(TAG, "Procesando UID: %s (%s) desde End Device 0x%04X",
             uid_hex, nombre_equipo, msg->src_short_addr);

    // CASO 1: El laboratorio estaba libre -> Registrar ENTRADA
    if (!estado_actual.ocupado) {
        estado_actual.ocupado = true;
        snprintf(estado_actual.uid_hex, sizeof(estado_actual.uid_hex), "%s", uid_hex);
        snprintf(estado_actual.nombre_equipo, sizeof(estado_actual.nombre_equipo), "%s", nombre_equipo);
        estado_actual.timestamp_entrada_seg = ahora_seg;

        guardar_estado_nvs();

        snprintf(linea_historial, sizeof(linea_historial),
                 "[Min %d] ENTRADA: %s (UID %s)", min_transcurridos, nombre_equipo, uid_hex);
        registrar_en_historial_spiffs(linea_historial);

        // Formato para Sam: field1 (equipo) | field2 (codigo evento) | mensaje para Discord
        snprintf(payload_gateway, sizeof(payload_gateway),
                 "%s|%d|Entrada registrada: **%s** (UID `%s`) ingreso al Laboratorio Cisco.",
                 nombre_equipo, EVENTO_ENTRADA, nombre_equipo, uid_hex);
        enviar_comando_zigbee_a_sam(gateway_short_addr, CMD_NOTIFY_GATEWAY, payload_gateway);
        return;
    }

    // CASO 2: El mismo equipo vuelve a pasar su tarjeta -> Registrar SALIDA normal
    if (strcmp(estado_actual.uid_hex, uid_hex) == 0) {
        int duracion_min = (int)((ahora_seg - estado_actual.timestamp_entrada_seg) / 60);

        snprintf(linea_historial, sizeof(linea_historial),
                 "[Min %d] SALIDA: %s (UID %s, estancia: %d min)",
                 min_transcurridos, nombre_equipo, uid_hex, duracion_min);
        registrar_en_historial_spiffs(linea_historial);

        memset(&estado_actual, 0, sizeof(estado_actual));
        guardar_estado_nvs();

        snprintf(payload_gateway, sizeof(payload_gateway),
                 "%s|%d|Salida registrada: **%s** (UID `%s`) desocupo el Laboratorio Cisco (estancia: %d min).",
                 nombre_equipo, EVENTO_SALIDA, nombre_equipo, uid_hex, duracion_min);
        enviar_comando_zigbee_a_sam(gateway_short_addr, CMD_NOTIFY_GATEWAY, payload_gateway);
        return;
    }

    // CASO 3: Un equipo distinto pasa tarjeta pero el anterior olvido hacer check-out
    // Se corrige cerrando automaticamente al equipo anterior y registrando al nuevo
    char equipo_anterior[32];
    char uid_anterior[21];
    snprintf(equipo_anterior, sizeof(equipo_anterior), "%s", estado_actual.nombre_equipo);
    snprintf(uid_anterior, sizeof(uid_anterior), "%s", estado_actual.uid_hex);

    ESP_LOGW(TAG, "Correccion automatica: %s olvido hacer check-out. Registrando salida automatica.",
             equipo_anterior);

    snprintf(linea_historial, sizeof(linea_historial),
             "[Min %d] AUTO-SALIDA: %s (UID %s olvido check-out)",
             min_transcurridos, equipo_anterior, uid_anterior);
    registrar_en_historial_spiffs(linea_historial);

    snprintf(payload_gateway, sizeof(payload_gateway),
             "%s|%d|Aviso: **%s** olvido registrar salida. Se aplico check-out automatico.",
             equipo_anterior, EVENTO_AUTO_SALIDA, equipo_anterior);
    enviar_comando_zigbee_a_sam(gateway_short_addr, CMD_NOTIFY_GATEWAY, payload_gateway);

    // Ahora registramos la entrada del nuevo equipo
    estado_actual.ocupado = true;
    snprintf(estado_actual.uid_hex, sizeof(estado_actual.uid_hex), "%s", uid_hex);
    snprintf(estado_actual.nombre_equipo, sizeof(estado_actual.nombre_equipo), "%s", nombre_equipo);
    estado_actual.timestamp_entrada_seg = ahora_seg;

    guardar_estado_nvs();

    snprintf(linea_historial, sizeof(linea_historial),
             "[Min %d] ENTRADA: %s (UID %s)", min_transcurridos, nombre_equipo, uid_hex);
    registrar_en_historial_spiffs(linea_historial);

    snprintf(payload_gateway, sizeof(payload_gateway),
             "%s|%d|Entrada registrada: **%s** (UID `%s`) ingreso al Laboratorio Cisco.",
             nombre_equipo, EVENTO_ENTRADA, nombre_equipo, uid_hex);
    enviar_comando_zigbee_a_sam(gateway_short_addr, CMD_NOTIFY_GATEWAY, payload_gateway);
}

// Tarea de FreeRTOS encargada de procesar la recepcion y almacenamiento
static void procesar_eventos_task(void *pvParameters)
{
    evento_nfc_msg_t msg;

    while (1) {
        if (xQueueReceive(cola_eventos_nfc, &msg, portMAX_DELAY) == pdTRUE) {
            consolidar_evento_nfc(&msg);
        }
    }
}

// ============================================================================
// RECEPCION DE COMANDOS ZIGBEE (DESDE JOSE Y DESDE SAM)
// ============================================================================

static esp_err_t manejar_comando_custom_zigbee(const esp_zb_zcl_custom_cluster_command_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Mensaje Zigbee vacio");
    if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Comando recibido con estado de error: 0x%x", message->info.status);
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t src_addr = message->info.src_address.u.short_addr;
    uint16_t cmd_id = message->info.command.id;

    ESP_LOGI(TAG, "Comando Zigbee recibido [0x%02X] desde nodo 0x%04X (len=%d)",
             cmd_id, src_addr, message->data.size);

    switch (cmd_id) {
        case CMD_NFC_UID_EVENT: {
            // Datos enviados por la placa de Jose (NFC.c -> enviar_uid_zigbee)
            if (message->data.value && message->data.size > 0) {
                evento_nfc_msg_t ev = {0};
                ev.src_short_addr = src_addr;
                ev.uid_len = (message->data.size > sizeof(ev.uid))
                             ? sizeof(ev.uid)
                             : (uint8_t)message->data.size;
                memcpy(ev.uid, message->data.value, ev.uid_len);

                ESP_LOGI(TAG, "UID recibido de la placa de Jose:");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, ev.uid, ev.uid_len, ESP_LOG_INFO);

                xQueueSend(cola_eventos_nfc, &ev, 0);
            }
        } break;

        case CMD_QUERY_EQUIPO: {
            // Solicitud enviada por la placa de Sam cuando en Discord escriben !equipo
            gateway_short_addr = src_addr;
            char respuesta[160];

            if (estado_actual.ocupado) {
                int64_t ahora_seg = esp_timer_get_time() / 1000000ULL;
                int min_uso = (int)((ahora_seg - estado_actual.timestamp_entrada_seg) / 60);
                snprintf(respuesta, sizeof(respuesta),
                         "Laboratorio ocupado por **%s** (UID: `%s`, tiempo en sesion: %d min).",
                         estado_actual.nombre_equipo, estado_actual.uid_hex, min_uso);
            } else {
                snprintf(respuesta, sizeof(respuesta),
                         "El Laboratorio de Redes Cisco se encuentra actualmente **LIBRE**.");
            }

            enviar_comando_zigbee_a_sam(src_addr, CMD_RESP_EQUIPO, respuesta);
        } break;

        case CMD_QUERY_HISTORIAL: {
            // Solicitud enviada por la placa de Sam cuando en Discord escriben !historial
            gateway_short_addr = src_addr;
            char historial_buf[200];
            leer_historial_reciente(historial_buf, sizeof(historial_buf));

            enviar_comando_zigbee_a_sam(src_addr, CMD_RESP_HISTORIAL, historial_buf);
        } break;

        default:
            ESP_LOGW(TAG, "Comando personalizado desconocido: 0x%02X", cmd_id);
            break;
    }

    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;

    switch (callback_id) {
        case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
            ret = manejar_comando_custom_zigbee((const esp_zb_zcl_custom_cluster_command_message_t *)message);
            break;

        default:
            ESP_LOGD(TAG, "Callback Zigbee recibido: 0x%x", callback_id);
            break;
    }

    return ret;
}

// ============================================================================
// MANEJO DE SENALES Y CONEXION DE LA RED ZIGBEE (ROL ROUTER)
// ============================================================================

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK,
                        , TAG, "No se pudo iniciar el comisionamiento Zigbee");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Inicializando stack Zigbee...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "Dispositivo iniciado en modo %s",
                         esp_zb_bdb_is_factory_new() ? "Factory-New" : "Con red previa en NVS");
                if (esp_zb_bdb_is_factory_new()) {
                    ESP_LOGI(TAG, "Buscando red Zigbee para unirse (Network Steering)...");
                    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                } else {
                    red_zigbee_conectada = true;
                    ESP_LOGI(TAG, "Router Zigbee reconectado! ShortAddr: 0x%04X, PAN ID: 0x%04X, Canal: %d",
                             esp_zb_get_short_address(), esp_zb_get_pan_id(), esp_zb_get_current_channel());
                    // Abrir la red para que el End Device de Jose pueda unirse a traves de este Router
                    esp_zb_bdb_open_network(180);
                }
            } else {
                ESP_LOGW(TAG, "Reintentando inicializacion Zigbee (status: %s)...", esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                       ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            }
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err_status == ESP_OK) {
                red_zigbee_conectada = true;
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                ESP_LOGI(TAG, "Unido exitosamente a la red Zigbee como ROUTER!");
                ESP_LOGI(TAG, "PAN ID: 0x%04X, Canal: %d, Direccion Corta: 0x%04X",
                         esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
                // Permitir que los End Devices (como la placa de Jose) se unan a traves del Router
                esp_zb_bdb_open_network(180);
            } else {
                ESP_LOGW(TAG, "No se encontro red Zigbee aun (%s). Reintentando en 1 segundo...",
                         esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                       ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;

        case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            esp_zb_zdo_signal_device_annce_params_t *dev_annce_params =
                (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
            ESP_LOGI(TAG, "Nuevo nodo Zigbee detectado en la red! ShortAddr: 0x%04X",
                     dev_annce_params->device_short_addr);
        } break;

        default:
            ESP_LOGD(TAG, "Senal ZDO: %s (0x%x), estado: %s",
                     esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
            break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    // Inicializar configuracion como ROUTER ZIGBEE (rol ZIGBEE_ROUTER del PDF)
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_DEFAULT_ROUTER_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // Crear lista de clusters para el Endpoint del Router
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    // Cluster Basico y de Identificacion estandar
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    // Cluster personalizado (0xFF00) en rol SERVER (para recibir de Jose y Sam) y CLIENT (para enviar a Sam)
    esp_zb_attribute_list_t *custom_server_attr = esp_zb_zcl_attr_list_create(LAB_CUSTOM_CLUSTER_ID);
    esp_zb_attribute_list_t *custom_client_attr = esp_zb_zcl_attr_list_create(LAB_CUSTOM_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_server_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_client_attr, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

    // Registrar Endpoint del Router
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZB_ROUTER_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config));
    ESP_ERROR_CHECK(esp_zb_device_register(ep_list));

    // Registrar el callback para procesar los comandos Zigbee entrantes
    esp_zb_core_action_handler_register(zb_action_handler);

    // Configurar mascara de canales
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK));

    // Iniciar stack Zigbee y bucle principal
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

// ============================================================================
// FUNCION PRINCIPAL (APP_MAIN)
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "INICIANDO ROUTER ZIGBEE - LABORATORIO CISCO");

    // 1. Inicializar NVS (para el stack Zigbee y para persistir el estado actual)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Inicializar SPIFFS para guardar el historial diario y recuperar estado NVS
    iniciar_spiffs();
    cargar_estado_nvs();

    // 3. Configurar plataforma de radio y host para Zigbee en ESP32-C6
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    // 4. Crear cola de eventos y tareas de FreeRTOS con stack holgado para Zigbee ZCL
    cola_eventos_nfc = xQueueCreate(10, sizeof(evento_nfc_msg_t));
    xTaskCreate(procesar_eventos_task, "procesar_eventos", 4096, NULL, 5, NULL);
    xTaskCreate(esp_zb_task, "zigbee_router_main", 8192, NULL, 5, NULL);
}

