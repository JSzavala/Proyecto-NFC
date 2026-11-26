# Router Zigbee (ESP32-C6) - Laboratorio de Redes Cisco

Nodo **Router Zigbee** (`ESP_ZB_DEVICE_TYPE_ROUTER`) para el sistema híbrido IoT del Laboratorio de Redes Cisco.

## Funciones implementadas
- **Rol Zigbee Router (`ZIGBEE_ROUTER`)**: Actúa como router y repetidor en la red Zigbee formada por el Gateway (Coordinador `0x0000`, PAN ID `0x1A62`, Canal `13`), abriendo la red para que los End Devices (Lector NFC con PN532) se unan a través de él.
- **Recepción desde End Device Lector NFC**:
  - Escucha en el Endpoint `10` tanto en el cluster `0xFC00` como en `0xFF00`.
  - Acepta el UID de la tarjeta NFC en bytes crudos (`uint8_t *uid, uint8_t longitud` de `enviar_uid_zigbee`), en cadena hexadecimal ASCII, en texto `v1|...` o como estructura `lab_event_t`.
- **Consolidación de Entrada / Salida / Corrección**:
  - **Entrada (`LAB_EVENT_ENTRADA = 0`)**: Cuando el laboratorio está libre y se lee una tarjeta NFC.
  - **Salida (`LAB_EVENT_SALIDA = 1`)**: Cuando el mismo equipo vuelve a pasar su tarjeta NFC, calculando el tiempo de estancia.
  - **Corrección (`LAB_EVENT_CORRECCION = 2`)**: Si el equipo anterior olvidó hacer check-out y otro equipo pasa su tarjeta, registra automáticamente la salida por corrección del equipo anterior y la entrada del nuevo equipo.
- **Persistencia local**:
  - **NVS (`nvs_flash`)**: Persiste el estado actual del laboratorio (`ocupado`, `uid_hex`, `nombre_equipo`, `nombre_integrante`, `timestamp`).
  - **SPIFFS (`/spiffs/historial.txt`)**: Guarda el historial diario de eventos en la partición `spiffs`.
- **Compatibilidad con el Gateway (Discord + ThingSpeak)**:
  - Envía los eventos consolidados al Gateway (`0x0000`, Endpoint `10`, Cluster `0xFC00`, Comando `0x00` `LAB_ZIGBEE_CMD_EVENT`, dirección `ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV`) usando la estructura binaria empaquetada `lab_event_t` de 64 bytes definida en `lab_protocol.h`.
  - También responde a consultas directas de estado/historial (`CMD_QUERY_EQUIPO` y `CMD_QUERY_HISTORIAL`).

## Compilación y Flasheo
```bash
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```
