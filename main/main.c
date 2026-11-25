#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/uart.h"

#include "pn532.h"
#include "pn532_driver_hsu.h"

// CONFIGURACION DEL PN532

#define HSU_HOST_RX    4
#define HSU_HOST_TX    5

#define HSU_UART_PORT  UART_NUM_1
#define HSU_BAUD_RATE  115200

#define RESET_PIN      (-1)
#define IRQ_PIN        (-1)


static const char *TAG = "LECTOR_NFC";

static pn532_io_t pn532_io;

// Funcion que despues utilizaremos
// para enviar el UID mediante Zigbee.
void enviar_uid_zigbee(uint8_t *uid, uint8_t longitud)
{
    ESP_LOGI(TAG, "Preparando UID para Zigbee:");

    ESP_LOG_BUFFER_HEX_LEVEL(
        TAG,
        uid,
        longitud,
        ESP_LOG_INFO
    );

    // Aqui integraremos el envio Zigbee.
    
}

// INICIALIZAR PN532

static void iniciar_pn532(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Inicializando PN532...");

    err = pn532_new_driver_hsu(
        HSU_HOST_RX,
        HSU_HOST_TX,
        RESET_PIN,
        IRQ_PIN,
        HSU_UART_PORT,
        HSU_BAUD_RATE,
        &pn532_io
    );

    ESP_ERROR_CHECK(err);

    // Inicializar comunicacion
    do
    {
        err = pn532_init(&pn532_io);

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "No se pudo inicializar PN532");

            pn532_release(&pn532_io);

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

    } while (err != ESP_OK);

    ESP_LOGI(TAG, "PN532 inicializado correctamente");

    // Obtener version del firmware
    uint32_t version_data = 0;

    do
    {
        err = pn532_get_firmware_version(
            &pn532_io,
            &version_data
        );

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "No se detecta el PN532");

            pn532_reset(&pn532_io);

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

    } while (err != ESP_OK);

    ESP_LOGI(
        TAG,
        "Firmware PN532: %d.%d",
        (int)((version_data >> 16) & 0xFF),
        (int)((version_data >> 8) & 0xFF)
    );
}

// LEER LAS TARJETAS NFC

static void leer_tarjetas_nfc(void)
{
    esp_err_t err;

    uint8_t uid[7];

    uint8_t uid_length;

    ESP_LOGI(TAG, "Esperando tarjeta NFC...");

    while (1)
    {
        uid_length = 0;

        err = pn532_read_passive_target_id(
            &pn532_io,
            PN532_BRTY_ISO14443A_106KBPS,
            uid,
            &uid_length,
            0
        );

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Tarjeta NFC detectada");

            ESP_LOGI(
                TAG,
                "Longitud del UID: %d bytes",
                uid_length
            );

            ESP_LOGI(TAG, "UID de la tarjeta:");

            ESP_LOG_BUFFER_HEX_LEVEL(
                TAG,
                uid,
                uid_length,
                ESP_LOG_INFO
            );

            // Preparar envio por Zigbee
            enviar_uid_zigbee(uid, uid_length);

            // Evitar lecturas repetidas inmediatas
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "INICIANDO LECTOR NFC ZIGBEE");

    iniciar_pn532();

    leer_tarjetas_nfc();
}