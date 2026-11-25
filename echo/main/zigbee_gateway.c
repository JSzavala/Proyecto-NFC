#include "zigbee_gateway.h"

#include <string.h>
#include <time.h>

#include "discord_bot.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "lab_protocol.h"
#include "lab_store.h"
#include "thingspeak.h"

static const char *TAG = "zigbee_gw";

#define LAB_MAX_CHILDREN          10
#define LAB_INSTALL_CODE_POLICY   false
#define LAB_PRIMARY_CHANNEL_MASK  (1UL << CONFIG_LAB_ZIGBEE_CHANNEL)
#define LAB_ZB_TASK_STACK         8192
#define LAB_EVENT_QUEUE_LEN       16
#define LAB_EVENT_TASK_STACK      6144

#define LAB_MANUFACTURER_NAME "\x05" "ITSUR"
#define LAB_MODEL_IDENTIFIER  "\x0D" "LabGateway-C6"

#if CONFIG_LAB_ZIGBEE_END_DEVICE
#if !CONFIG_ZB_ZED
#error "Para el rol End Device selecciona Component config > Zigbee > Zigbee End Device"
#endif
#define LAB_ZB_DEVICE_CONFIG()                                                       \
    {                                                                                \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                                        \
        .install_code_policy = LAB_INSTALL_CODE_POLICY,                              \
        .nwk_cfg.zed_cfg =                                                           \
            {                                                                        \
                .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,                         \
                .keep_alive = 3000,                                                  \
            },                                                                       \
    }
#else
#if !CONFIG_ZB_ZCZR
#error "Para el rol Coordinator selecciona Component config > Zigbee > Zigbee Coordinator or Router"
#endif
#define LAB_ZB_DEVICE_CONFIG()                                                       \
    {                                                                                \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,                               \
        .install_code_policy = LAB_INSTALL_CODE_POLICY,                              \
        .nwk_cfg.zczr_cfg =                                                          \
            {                                                                        \
                .max_children = LAB_MAX_CHILDREN,                                    \
            },                                                                       \
    }
#endif

static QueueHandle_t s_event_queue;
static esp_zb_attribute_list_t *s_custom_attr_list;
static uint32_t s_event_counter;

static void lab_event_task(void *argument)
{
    (void)argument;

    lab_event_t event;
    while (xQueueReceive(s_event_queue, &event, portMAX_DELAY) == pdPASS) {
        ESP_LOGI(TAG, "Evento: %s equipo=%s integrante=%s tarjeta=%s", lab_event_type_name(event.type), event.team,
                 event.member[0] ? event.member : "-", event.card[0] ? event.card : "-");

        lab_store_append(&event);
        thingspeak_enqueue(&event);
        discord_bot_notify_event(&event);
    }

    vTaskDelete(NULL);
}

static esp_err_t handle_custom_command(const esp_zb_zcl_custom_cluster_command_message_t *message)
{
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }

    if (message->info.cluster != LAB_ZIGBEE_CLUSTER_ID || message->info.dst_endpoint != LAB_ZIGBEE_ENDPOINT) {
        return ESP_OK;
    }

    if (message->info.command.id == LAB_ZIGBEE_CMD_PING) {
        ESP_LOGI(TAG, "Ping Zigbee recibido de 0x%04x", message->info.src_address.u.short_addr);
        return ESP_OK;
    }

    if (message->info.command.id != LAB_ZIGBEE_CMD_EVENT) {
        ESP_LOGW(TAG, "Comando Zigbee no soportado: 0x%02x", message->info.command.id);
        return ESP_OK;
    }

    if (!message->data.value || message->data.size == 0) {
        ESP_LOGW(TAG, "Evento Zigbee sin payload");
        return ESP_ERR_INVALID_ARG;
    }

    lab_event_t event;
    if (!lab_event_parse(message->data.value, message->data.size, &event)) {
        ESP_LOGW(TAG, "Payload Zigbee invalido (%u bytes)", message->data.size);
        return ESP_ERR_INVALID_ARG;
    }

    if (event.timestamp == 0) {
        event.timestamp = (uint32_t)time(NULL);
    }

    if (event.team[0] == '\0') {
        ESP_LOGW(TAG, "Evento Zigbee sin equipo");
        return ESP_ERR_INVALID_ARG;
    }

    s_event_counter++;
    if (s_custom_attr_list) {
        esp_zb_cluster_update_attr(s_custom_attr_list, LAB_ZIGBEE_ATTR_EVENT_COUNTER, &s_event_counter);
    }

    if (xQueueSend(s_event_queue, &event, 0) != pdPASS) {
        ESP_LOGW(TAG, "Cola de eventos llena; se descarta el evento de %s", event.team);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
        return handle_custom_command((const esp_zb_zcl_custom_cluster_command_message_t *)message);
    default:
        return ESP_OK;
    }
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    if (esp_zb_bdb_start_top_level_commissioning(mode_mask) != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo iniciar el modo BDB 0x%02x", mode_mask);
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Inicializando stack Zigbee");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status != ESP_OK) {
            ESP_LOGE(TAG, "Fallo al iniciar Zigbee: %s", esp_err_to_name(err_status));
            break;
        }

        ESP_LOGI(TAG, "Gateway iniciado en modo %s", esp_zb_bdb_is_factory_new() ? "nuevo" : "existente");
#if CONFIG_LAB_ZIGBEE_END_DEVICE
        if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Buscando la red del Router Zigbee...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "Gateway reincorporado a la red Zigbee");
        }
#else
        if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Formando red Zigbee...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        } else {
            ESP_LOGI(TAG, "Red Zigbee existente; abriendo 180 s para que se una el Router");
            esp_zb_bdb_open_network(180);
        }
#endif
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t ieee_address;
            esp_zb_get_long_address(ieee_address);
            ESP_LOGI(TAG,
                     "Red formada (PAN 0x%04hx, canal %d, addr 0x%04hx, extended PAN "
                     "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address(), ieee_address[7],
                     ieee_address[6], ieee_address[5], ieee_address[4], ieee_address[3], ieee_address[2],
                     ieee_address[1], ieee_address[0]);
            esp_zb_bdb_open_network(180);
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGW(TAG, "No se pudo formar la red (%s); reintentando", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Red Zigbee lista (PAN 0x%04hx, canal %d, addr 0x%04hx)", esp_zb_get_pan_id(),
                     esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGW(TAG, "Network steering fallo (%s); reintentando", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *params =
            (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (params) {
            ESP_LOGI(TAG, "Dispositivo Zigbee unido/reincorporado: 0x%04hx", params->device_short_addr);
        }
    } break;

    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (duration > 0) {
            ESP_LOGI(TAG, "Red abierta para unirse durante %u s", duration);
        } else {
            ESP_LOGW(TAG, "Red cerrada; no se aceptan nuevos dispositivos");
        }
    } break;

    default:
        ESP_LOGI(TAG, "Senal ZDO: %s (0x%x), estado: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static void zigbee_task(void *argument)
{
    (void)argument;

    esp_zb_cfg_t config = LAB_ZB_DEVICE_CONFIG();
    esp_zb_init(&config);
    esp_zb_set_primary_network_channel_set(LAB_PRIMARY_CHANNEL_MASK);

#if !CONFIG_LAB_ZIGBEE_END_DEVICE
    esp_zb_set_pan_id(CONFIG_LAB_ZIGBEE_PAN_ID);
#endif

    esp_zb_ep_list_t *endpoint_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                  (void *)LAB_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                  (void *)LAB_MODEL_IDENTIFIER);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL),
                                             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    s_custom_attr_list = esp_zb_zcl_attr_list_create(LAB_ZIGBEE_CLUSTER_ID);
    esp_zb_custom_cluster_add_custom_attr(s_custom_attr_list, LAB_ZIGBEE_ATTR_EVENT_COUNTER,
                                          ESP_ZB_ZCL_ATTR_TYPE_U32, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
                                          &s_event_counter);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, s_custom_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = LAB_ZIGBEE_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_HOME_GATEWAY_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(endpoint_list, cluster_list, endpoint_config);

    esp_zb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = LAB_ZIGBEE_CLUSTER_ID,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .check_value_cb = NULL,
        .write_attr_cb = NULL,
    };
    esp_zb_zcl_custom_cluster_handlers_update(handlers);

    ESP_ERROR_CHECK(esp_zb_device_register(endpoint_list));
    esp_zb_core_action_handler_register(zb_action_handler);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();

    vTaskDelete(NULL);
}

esp_err_t zigbee_gateway_start(void)
{
    s_event_queue = xQueueCreate(LAB_EVENT_QUEUE_LEN, sizeof(lab_event_t));
    if (!s_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(lab_event_task, "lab_event", LAB_EVENT_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(zigbee_task, "zigbee_main", LAB_ZB_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Gateway Zigbee iniciado (endpoint %d, cluster 0x%04x)", LAB_ZIGBEE_ENDPOINT,
             LAB_ZIGBEE_CLUSTER_ID);
    return ESP_OK;
}
