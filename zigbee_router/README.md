# Router Zigbee (ESP32-C6) - Laboratorio de Redes Cisco

Componente Router Zigbee para el sistema híbrido IoT del Laboratorio de Redes Cisco.

## Funciones
- **Rol Zigbee Router (`ESP_ZB_DEVICE_TYPE_ROUTER`)**: Actúa como router y repetidor en la red Zigbee.
- **Recepción NFC**: Recibe los UIDs leídos por el End Device (PN532 de José) a través del Cluster ZCL `0xFF00`.
- **Consolidación y Auto-Corrección**:
  - Registra eventos de **Entrada** (Check-in).
  - Registra eventos de **Salida** (Check-out) y calcula minutos de sesión.
  - Corrección automática: Si un equipo olvidó hacer check-out y un nuevo equipo ingresa, cierra la sesión del equipo anterior y abre la nueva automáticamente.
- **Persistencia**:
  - Estado actual del laboratorio persistido en **NVS** (`nvs_flash`).
  - Historial diario en archivo de texto en **SPIFFS** (`/spiffs/historial.txt`).
- **Comunicación con Gateway (Sam)**:
  - Notifica en tiempo real cada entrada/salida para Discord y ThingSpeak.
  - Responde a las consultas de los comandos de Discord `!equipo` y `!historial`.

## Compilación y Flasheo (ESP-IDF v5.1+)
```bash
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```
