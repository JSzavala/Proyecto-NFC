# Router Zigbee – Sistema de Control del Laboratorio Cisco

## 1. Descripción del proyecto

Esta placa ESP32-C6 funciona como un **Router Zigbee** dentro de un sistema de control de acceso y monitoreo de ocupación del Laboratorio Cisco.

Su función es recibir información de las tarjetas NFC leídas por otra placa, procesar los registros de entrada y salida de los equipos, almacenar la información y transmitir los eventos a una placa Gateway encargada de la comunicación con Discord y ThingSpeak.

La placa permite mantener actualizado el estado del laboratorio y consultar el historial de actividad.

## 2. Funciones principales

- **Recepción de UID NFC:** recibe los identificadores de las tarjetas NFC enviados por el dispositivo End Device.
- **Registro de entradas:** cuando el laboratorio está libre, registra al equipo que ingresa.
- **Registro de salidas:** cuando el mismo equipo vuelve a pasar su tarjeta, registra su salida y calcula el tiempo de estancia.
- **Salida automática:** si ingresa un equipo diferente y el anterior no registró su salida, el sistema realiza un cierre automático del registro anterior.
- **Almacenamiento de información:** conserva el estado actual del laboratorio mediante NVS y guarda los eventos en SPIFFS.
- **Comunicación Zigbee:** envía notificaciones y recibe solicitudes de información desde la placa Gateway.
- **Consulta del laboratorio:** permite conocer si el laboratorio está libre u ocupado y qué equipo se encuentra registrado.
- **Consulta del historial:** proporciona los registros recientes de entradas y salidas.

## 3. Funcionamiento del sistema

El funcionamiento de la placa se divide en las siguientes etapas:

1. La placa recibe el UID de una tarjeta NFC desde el dispositivo End Device mediante Zigbee.
2. Convierte el identificador recibido a formato hexadecimal.
3. Compara el UID con una tabla de equipos registrados.
4. Determina si corresponde registrar una entrada, una salida o una salida automática.
5. Actualiza el estado de ocupación del laboratorio.
6. Guarda la información en la memoria NVS y registra el evento en SPIFFS.
7. Envía una notificación a la placa Gateway mediante Zigbee.

## 4. Arquitectura de comunicación

El proyecto utiliza tres dispositivos con funciones diferentes:

- **End Device:** realiza la lectura de las tarjetas NFC mediante el lector PN532 y envía los UID.
- **Router (esta placa):** recibe los UID, procesa los eventos y administra el estado del laboratorio.
- **Gateway:** recibe las notificaciones del Router y permite consultar la información mediante Discord y ThingSpeak.

La comunicación entre los dispositivos se realiza mediante una red Zigbee utilizando un clúster personalizado.

## 5. Tecnologías utilizadas

| Tecnología | Función |
|---|---|
| ESP32-C6 | Microcontrolador de la placa |
| ESP-IDF | Entorno de desarrollo |
| Zigbee | Comunicación inalámbrica entre dispositivos |
| FreeRTOS | Administración de tareas y eventos |
| NVS | Almacenamiento del estado actual |
| SPIFFS | Almacenamiento del historial |
| C | Lenguaje de programación |

## 6. Almacenamiento de información

La placa utiliza dos sistemas de almacenamiento:

**NVS (Non-Volatile Storage)**

Se utiliza para conservar el estado actual del laboratorio, incluyendo:
- Estado de ocupación.
- UID del equipo registrado.
- Nombre del equipo.
- Tiempo de entrada.

Esto permite recuperar el estado anterior cuando la placa se reinicia.

**SPIFFS (SPI Flash File System)**

Se utiliza para guardar un historial de eventos en el archivo:

`/spiffs/historial.txt`

En este archivo se registran:
- Entradas de equipos.
- Salidas de equipos.
- Salidas automáticas.
- Tiempo de estancia cuando se registra una salida normal.

## 7. Comandos Zigbee

La placa utiliza un clúster personalizado con identificador `0xFF00`.

Los comandos implementados son:

| Comando | Identificador | Función |
|---|---|---|
| CMD_NFC_UID_EVENT | 0x01 | Recibir UID de una tarjeta NFC |
| CMD_NOTIFY_GATEWAY | 0x02 | Notificar eventos al Gateway |
| CMD_QUERY_EQUIPO | 0x03 | Solicitar el estado del laboratorio |
| CMD_RESP_EQUIPO | 0x04 | Responder con el estado actual |
| CMD_QUERY_HISTORIAL | 0x05 | Solicitar el historial |
| CMD_RESP_HISTORIAL | 0x06 | Enviar el historial al Gateway |

## 8. Organización de archivos

- `zigbee_router.c`: contiene la lógica principal del Router, recepción de mensajes, procesamiento de eventos NFC, almacenamiento y comunicación Zigbee.
- `zigbee_router.h`: contiene las configuraciones de red, identificadores de comandos, estructuras de datos y definiciones compartidas.
- `CMakeLists.txt`: configura la compilación del componente.
- `idf_component.yml`: especifica las dependencias necesarias para ESP-IDF.

## 9. Requisitos

Para compilar y ejecutar el proyecto se necesita:

- Una placa compatible con ESP32-C6 y Zigbee.
- ESP-IDF versión 5.1 o superior.
- Extensión ESP-IDF para Visual Studio Code o entorno de compilación ESP-IDF.
- Bibliotecas:
  - `espressif/esp-zboss-lib` versión 1.6.0.
  - `espressif/esp-zigbee-lib` versión 1.6.0.

## 10. Configuración y ejecución

1. Instalar y configurar ESP-IDF.
2. Abrir el proyecto en Visual Studio Code.
3. Seleccionar el objetivo de compilación ESP32-C6.
4. Verificar la configuración de particiones para incluir el almacenamiento SPIFFS.
5. Conectar la placa mediante USB.
6. Compilar el proyecto.
7. Grabar el firmware en la placa.
8. Abrir el monitor serial para observar los mensajes de inicialización y comunicación Zigbee.

Comandos básicos desde la terminal de ESP-IDF:

```bash
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

Reemplazar `COMx` por el puerto COM correspondiente a la placa.

## 11. Resultado esperado

Al iniciar, la placa configura sus sistemas de almacenamiento, crea las tareas de FreeRTOS e inicializa la comunicación Zigbee como Router.

Una vez conectada a la red, puede recibir los identificadores NFC, procesar las entradas y salidas de los equipos, guardar los registros y comunicar los cambios a la placa Gateway.

De esta manera, funciona como el dispositivo intermedio encargado de administrar y consolidar la información del sistema de control del Laboratorio Cisco.

---

**Proyecto:** Sistema de control de acceso y monitoreo del Laboratorio Cisco.

**Dispositivo:** Router Zigbee.

**Plataforma:** ESP32-C6.

**Lenguaje:** C.

**Entorno de desarrollo:** ESP-IDF.
