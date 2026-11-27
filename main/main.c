#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs_flash.h"
#include "pn532.h"
#include "pn532_driver_hsu.h"
#include "zdo/esp_zigbee_zdo_command.h"

#define HSU_HOST_RX                 4
#define HSU_HOST_TX                 5
#define HSU_UART_PORT               UART_NUM_1
#define HSU_BAUD_RATE               115200
#define RESET_PIN                   (-1)
#define IRQ_PIN                     (-1)

#define ZB_ENDPOINT                 10
#define ZB_NFC_CLUSTER_ID           0xFF00
#define ZB_NFC_UID_COMMAND_ID       0x01
#define ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
#define ZB_CONNECTED_BIT            BIT0
#define ZB_ROUTER_FOUND_BIT         BIT1
#define ZB_TASK_STACK_SIZE          8192
#define ZB_ROUTER_SHORT_ADDR_UNKNOWN 0xFFFF
#define ZB_ROUTER_DISCOVERY_RETRY_MS 6000
#define ZB_MATCH_DESC_BROADCAST_ADDR 0xFFFD
#define NFC_UID_MAX_LENGTH            10

static const char *TAG = "LECTOR_NFC";
static pn532_io_t s_pn532_io;
static EventGroupHandle_t s_zigbee_events;
static uint16_t s_router_short_addr = ZB_ROUTER_SHORT_ADDR_UNKNOWN;
static uint8_t s_router_endpoint;
static uint16_t s_router_cluster_id = ZB_NFC_CLUSTER_ID;

static void buscar_router(void);

static void reintentar_busqueda_router(uint8_t unused)
{
    (void)unused;
    buscar_router();
}

static void router_match_callback(esp_zb_zdp_status_t status, uint16_t address,
                                  uint8_t endpoint, void *user_context)
{
    (void)user_context;

    if (status != ESP_ZB_ZDP_STATUS_SUCCESS || address == ZB_ROUTER_SHORT_ADDR_UNKNOWN ||
        address == 0x0000 || endpoint != ZB_ENDPOINT) {
        ESP_LOGD(TAG, "Respuesta de descubrimiento no corresponde al Router (estado 0x%02x)", status);
        return;
    }

    s_router_short_addr = address;
    s_router_endpoint = endpoint;
    xEventGroupSetBits(s_zigbee_events, ZB_ROUTER_FOUND_BIT);
    ESP_LOGI(TAG, "Router encontrado en 0x%04x, endpoint %u", address, endpoint);
}

static void buscar_router(void)
{
    if ((xEventGroupGetBits(s_zigbee_events) & ZB_CONNECTED_BIT) == 0 ||
        s_router_short_addr != ZB_ROUTER_SHORT_ADDR_UNKNOWN) {
        return;
    }

    esp_zb_zdo_match_desc_req_param_t request = {
        .dst_nwk_addr = ZB_MATCH_DESC_BROADCAST_ADDR,
        .addr_of_interest = ZB_MATCH_DESC_BROADCAST_ADDR,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .num_in_clusters = 1,
        .num_out_clusters = 0,
        .cluster_list = &s_router_cluster_id,
    };
    esp_err_t err = esp_zb_zdo_match_cluster(&request, router_match_callback, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo buscar el Router Zigbee: %s", esp_err_to_name(err));
    }

    esp_zb_scheduler_alarm((esp_zb_callback_t)reintentar_busqueda_router, 0,
                           ZB_ROUTER_DISCOVERY_RETRY_MS);
}

static void bdb_start_commissioning_cb(uint8_t mode_mask)
{
    esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode_mask);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo iniciar commissioning 0x%02x: %s", mode_mask, esp_err_to_name(err));
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    if (signal_struct == NULL || signal_struct->p_app_signal == NULL) {
        ESP_LOGE(TAG, "Señal Zigbee inválida");
        return;
    }

    esp_zb_app_signal_type_t signal = (esp_zb_app_signal_type_t)*signal_struct->p_app_signal;
    esp_err_t status = signal_struct->esp_err_status;

    switch (signal) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Inicializando stack Zigbee");
        status = esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo inicializar BDB: %s", esp_err_to_name(status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "Fallo al iniciar Zigbee: %s", esp_err_to_name(status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            break;
        }

        if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Buscando la red del Gateway/Router Zigbee...");
            status = esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            if (status != ESP_OK) {
                ESP_LOGW(TAG, "No se pudo iniciar network steering: %s", esp_err_to_name(status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_commissioning_cb,
                                       ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
        } else {
            xEventGroupSetBits(s_zigbee_events, ZB_CONNECTED_BIT);
            ESP_LOGI(TAG, "Red Zigbee recuperada (PAN 0x%04hx, canal %d, dirección 0x%04hx)",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            buscar_router();
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (status == ESP_OK) {
            xEventGroupSetBits(s_zigbee_events, ZB_CONNECTED_BIT);
            ESP_LOGI(TAG, "Unido a la red Zigbee (PAN 0x%04hx, canal %d, dirección 0x%04hx)",
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            buscar_router();
        } else {
            xEventGroupClearBits(s_zigbee_events, ZB_CONNECTED_BIT);
            xEventGroupClearBits(s_zigbee_events, ZB_ROUTER_FOUND_BIT);
            s_router_short_addr = ZB_ROUTER_SHORT_ADDR_UNKNOWN;
            ESP_LOGW(TAG, "Network steering falló (%s); reintentando", esp_err_to_name(status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;

    default:
        ESP_LOGD(TAG, "Señal Zigbee 0x%x: %s", signal, esp_err_to_name(status));
        break;
    }
}

static void zigbee_task(void *argument)
{
    (void)argument;

    esp_zb_cfg_t config = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&config);
    ESP_ERROR_CHECK(esp_zb_set_primary_network_channel_set(ZB_PRIMARY_CHANNEL_MASK));

    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster,
                                                          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    esp_zb_attribute_list_t *nfc_cluster = esp_zb_zcl_attr_list_create(ZB_NFC_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cluster_list, nfc_cluster,
                                                           ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

    esp_zb_ep_list_t *endpoint_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZB_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0,
    };
    ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(endpoint_list, cluster_list, endpoint_config));
    ESP_ERROR_CHECK(esp_zb_device_register(endpoint_list));

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();

    vTaskDelete(NULL);
}

static void iniciar_pn532(void)
{
    ESP_LOGI(TAG, "Inicializando PN532...");
    ESP_ERROR_CHECK(pn532_new_driver_hsu(HSU_HOST_RX, HSU_HOST_TX, RESET_PIN, IRQ_PIN,
                                         HSU_UART_PORT, HSU_BAUD_RATE, &s_pn532_io));

    esp_err_t err;
    do {
        err = pn532_init(&s_pn532_io);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo inicializar PN532: %s", esp_err_to_name(err));
            pn532_release(&s_pn532_io);
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_ERROR_CHECK(pn532_new_driver_hsu(HSU_HOST_RX, HSU_HOST_TX, RESET_PIN, IRQ_PIN,
                                                 HSU_UART_PORT, HSU_BAUD_RATE, &s_pn532_io));
        }
    } while (err != ESP_OK);

    uint32_t version_data = 0;
    do {
        err = pn532_get_firmware_version(&s_pn532_io, &version_data);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No se detecta el PN532: %s", esp_err_to_name(err));
            pn532_reset(&s_pn532_io);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } while (err != ESP_OK);

    ESP_LOGI(TAG, "PN532 listo; firmware %d.%d", (int)((version_data >> 16) & 0xFF),
             (int)((version_data >> 8) & 0xFF));
}

static void enviar_uid_zigbee(const uint8_t *uid, uint8_t uid_length)
{
    if (uid == NULL || uid_length == 0 || uid_length > NFC_UID_MAX_LENGTH) {
        ESP_LOGE(TAG, "Longitud de UID inválida (%u); no se envía", uid_length);
        return;
    }

    EventBits_t connection_bits = xEventGroupGetBits(s_zigbee_events);
    if ((connection_bits & (ZB_CONNECTED_BIT | ZB_ROUTER_FOUND_BIT)) !=
        (ZB_CONNECTED_BIT | ZB_ROUTER_FOUND_BIT)) {
        ESP_LOGW(TAG, "Red Zigbee o Router desconectado; UID no enviado");
        return;
    }

    static const char hex_digits[] = "0123456789ABCDEF";
    char uid_hex[NFC_UID_MAX_LENGTH * 2 + 1];
    uint8_t uid_hex_length = (uint8_t)(uid_length * 2);
    for (uint8_t i = 0; i < uid_length; i++) {
        uid_hex[i * 2] = hex_digits[uid[i] >> 4];
        uid_hex[i * 2 + 1] = hex_digits[uid[i] & 0x0F];
    }
    uid_hex[uid_hex_length] = '\0';

    esp_zb_zcl_custom_cluster_cmd_t request = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = s_router_short_addr,
            .dst_endpoint = s_router_endpoint,
            .src_endpoint = ZB_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .cluster_id = ZB_NFC_CLUSTER_ID,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .custom_cmd_id = ZB_NFC_UID_COMMAND_ID,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_SET,
            .size = uid_hex_length,
            .value = uid_hex,
        },
    };

    if (!esp_zb_lock_acquire(portMAX_DELAY)) {
        ESP_LOGE(TAG, "No se pudo obtener el lock de Zigbee; UID no enviado");
        return;
    }
    uint8_t transaction_sequence = esp_zb_zcl_custom_cluster_cmd_req(&request);
    esp_zb_lock_release();

    ESP_LOGI(TAG, "UID %s enviado al Router 0x%04x (cluster 0x%04x, comando 0x%02x, TSN %u)",
             uid_hex, s_router_short_addr, ZB_NFC_CLUSTER_ID, ZB_NFC_UID_COMMAND_ID, transaction_sequence);
}

static void leer_tarjetas_nfc(void)
{
    uint8_t uid[NFC_UID_MAX_LENGTH];
    uint8_t uid_length;

    ESP_LOGI(TAG, "Esperando tarjeta NFC...");
    while (true) {
        uid_length = 0;
        esp_err_t err = pn532_read_passive_target_id(&s_pn532_io, PN532_BRTY_ISO14443A_106KBPS,
                                                      uid, &uid_length, 0);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Tarjeta detectada (%u bytes)", uid_length);
            enviar_uid_zigbee(uid, uid_length);
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_zigbee_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_zigbee_events == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));
    ESP_ERROR_CHECK(xTaskCreate(zigbee_task, "zigbee_main", ZB_TASK_STACK_SIZE, NULL, 5, NULL) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "Esperando que el End Device se una a la red y encuentre el Router Zigbee...");
    while ((xEventGroupWaitBits(s_zigbee_events, ZB_ROUTER_FOUND_BIT, pdFALSE, pdTRUE,
                                pdMS_TO_TICKS(5000)) & ZB_ROUTER_FOUND_BIT) == 0) {
        EventBits_t bits = xEventGroupGetBits(s_zigbee_events);
        if ((bits & ZB_CONNECTED_BIT) == 0) {
            ESP_LOGI(TAG, "Aún buscando la red del Gateway (PAN 0x1A62, canal 13)");
        } else {
            ESP_LOGI(TAG, "Unido a la red; buscando el Router que recibe el cluster 0x%04x", ZB_NFC_CLUSTER_ID);
        }
    }

    iniciar_pn532();
    leer_tarjetas_nfc();
}
