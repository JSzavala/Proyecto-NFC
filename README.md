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

# Anexo técnico detallado del Router Zigbee

Este anexo describe el firmware que se encuentra en `zigbee_router/main/`, no el Router encontrado dentro de dependencias generadas o ejemplos. El objetivo del firmware es convertir lecturas NFC aisladas en una sola fuente de verdad sobre el estado del laboratorio.

## 1. Responsabilidad dentro del sistema completo

El sistema se divide en tres funciones:

1. **End Device lector NFC:** utiliza el PN532 y envía el UID leído al Router.
2. **Router Zigbee:** acepta el UID, identifica el equipo, decide si ocurre entrada, salida o corrección automática y persiste el resultado.
3. **Gateway:** recibe el resultado consolidado y lo publica o expone a Discord y ThingSpeak.

La cadena completa es:

```text
Tarjeta NFC
  -> PN532 del End Device
  -> UID en bytes por Zigbee
  -> Endpoint 10 / cluster personalizado 0xFF00 del Router
  -> Cola de eventos FreeRTOS
  -> Identificación del equipo y consolidación del estado
  -> NVS + SPIFFS
  -> Notificación Zigbee al Gateway
  -> Discord / ThingSpeak
```

El Router debe considerarse el dueño del estado lógico del laboratorio. El End Device no decide si el laboratorio está ocupado; solo envía el UID. El Gateway no modifica directamente el estado: recibe eventos y consultas para presentar la información.

## 2. Inicio del firmware

`app_main()` realiza, en este orden, las siguientes operaciones:

1. Inicializa NVS, que también usa el stack Zigbee.
2. Monta la partición SPIFFS y apunta al archivo `/spiffs/historial.txt`.
3. Recupera el estado ocupado o libre desde NVS.
4. Configura la radio nativa y el modo host del ESP32-C6.
5. Crea una cola FreeRTOS de diez mensajes `evento_nfc_msg_t`.
6. Crea la tarea de consolidación de eventos.
7. Crea la tarea principal del stack Zigbee con rol Router.

Al iniciarse, `esp_zb_task()` registra el Endpoint 10, el cluster personalizado `0xFF00` en roles servidor y cliente, y arranca el stack. El callback de señales Zigbee busca o reconstruye la red con *network steering*, marca la red como conectada y abre la red durante 180 segundos para permitir la incorporación de End Devices.

## 3. Entrada a la red Zigbee

El dispositivo se configura con:

- Rol: `ESP_ZB_DEVICE_TYPE_ROUTER`.
- Endpoint propio: 10.
- Perfil: perfil HA (`0x0104`).
- Cluster personalizado: `0xFF00`.
- Dirección corta Gateway esperada: `0x0000`.
- Red disponible en todos los canales definidos por la máscara primaria del firmware.

Un Router no reemplaza al Coordinador/Gateway. Debe existir una red formada por el Coordinador, y el Router debe incorporarse mediante *network steering*. Una vez unido, puede reenviar mensajes y permitir que un End Device se una a través de él.

La política actual de instalación (`INSTALLCODE_POLICY_ENABLE`) está desactivada. Por ello, las placas prueban la unión por los mecanismos normales de la red configurada. La persistencia de credenciales en NVS permite que un dispositivo previamente unido vuelva a incorporarse sin ejecutar nuevamente el primer emparejamiento.

## 4. Recepción del UID NFC

Cuando el End Device lee una tarjeta, envía el UID al Endpoint 10 del Router con el comando personalizado:

| Elemento | Valor |
|---|---|
| Cluster | `0xFF00` |
| Comando | `CMD_NFC_UID_EVENT = 0x01` |
| Dirección origen | Dirección corta del End Device |
| Payload | UID en bytes |

El callback `manejar_comando_custom_zigbee()` obtiene la dirección corta origen, el ID del comando y el contenido recibido. Convierte el UID recibido, de hasta diez bytes, a una estructura `evento_nfc_msg_t` y la coloca en la cola FreeRTOS. Esta separación permite que el procesamiento HTTP/almacenamiento no bloquee el manejo rápido del stack Zigbee.

La tarea `procesar_eventos_task()` consume la cola y llama a `consolidar_evento_nfc()`.

La conversión de bytes a texto usa mayúsculas sin separadores. Por ejemplo:

```text
[0x04, 0xA1, 0xB2, 0xC3] -> 04A1B2C3
```

`obtener_nombre_equipo()` busca ese texto en `tabla_equipos`. Si no existe, el equipo se identifica como `Equipo-<UID>`. Los UIDs reales deben agregarse a la tabla antes de que se conozca un nombre fijo.

## 5. Consolidación de entradas y salidas

El estado actual se mantiene en memoria y se sincroniza con NVS:

```c
bool ocupado;
char uid_hex[21];
char nombre_equipo[32];
int64_t timestamp_entrada_seg;
```

`timestamp_entrada_seg` usa la base de tiempo del ESP-IDF mediante `esp_timer_get_time()`. Actualmente representa tiempo de ejecución del sistema en microsegundos convertido a segundos; el cálculo de duración se obtiene restando tiempos, no requiere una hora wall-clock.

### Entrada

Si el laboratorio está libre:

1. Se marca `ocupado = true`.
2. Se guarda el UID y el nombre del equipo.
3. Se registra el instante de entrada.
4. Se guarda el estado completo en NVS.
5. Se agrega una línea de entrada a `/spiffs/historial.txt`.
6. Se envía la notificación al Gateway.

### Salida normal

Si el UID recibido coincide con el UID almacenado:

1. Se calcula la duración en minutos.
2. Se agrega la salida al historial.
3. Se limpia el estado actual.
4. Se persiste el estado libre en NVS.
5. Se envía una notificación de salida al Gateway.

### Corrección automática

Si el laboratorio está ocupado por un equipo y pasa un UID distinto:

1. Se trata como salida automática del equipo anterior.
2. Se registra `AUTO-SALIDA` en SPIFFS.
3. Se notifica al Gateway el olvido de check-out.
4. Se reemplaza el estado por el nuevo equipo.
5. Se registra la entrada del nuevo equipo y se vuelve a notificar.

Esto evita dejar el laboratorio ocupado indefinidamente cuando un equipo inicia la sesión de otro sin completar primero la salida.

## 6. Persistencia y reinicios

### NVS

NVS conserva el estado consolidado dentro del namespace `lab_cisco`, clave `estado_actual`. Contiene únicamente el estado lógico del laboratorio; no contiene todo el historial.

Ventajas:

- Recupera el equipo ocupado después de reiniciar la placa.
- Evita que el laboratorio aparezca libre si ya existía una sesión activa.
- Garantiza que el estado y el historial sean coherentes después de un reinicio cuando SPIFFS está disponible.

### SPIFFS

SPIFFS monta la partición `spiffs` en `/spiffs`. El archivo generado es:

```text
/spiffs/historial.txt
```

Cada línea contiene una marca aproximada basada en el tiempo de ejecución, el tipo de evento, el equipo, el UID y, en salida normal, la duración. La consulta de historial devuelve los últimos datos que caben en el payload Zigbee reservado por el firmware.

## 7. Comunicación con el Gateway

La ruta implementada por el código del Router usa:

- Dirección Gateway: `0x0000` por defecto.
- Endpoint destino: 10.
- Cluster: `0xFF00`.
- Dirección corta: se actualiza con el origen de las consultas recibidas.
- Perfil: HA `0x0104`.

Los comandos definidos en `zigbee_router.h` son:

| Comando | Valor | Flujo |
|---|---:|---|
| `CMD_NFC_UID_EVENT` | `0x01` | End Device -> Router |
| `CMD_NOTIFY_GATEWAY` | `0x02` | Router -> Gateway |
| `CMD_QUERY_EQUIPO` | `0x03` | Gateway -> Router |
| `CMD_RESP_EQUIPO` | `0x04` | Router -> Gateway |
| `CMD_QUERY_HISTORIAL` | `0x05` | Gateway -> Router |
| `CMD_RESP_HISTORIAL` | `0x06` | Router -> Gateway |

Las notificaciones actuales usan una cadena con el formato:

```text
Equipo|CódigoEvento|Mensaje
```

Los códigos son:

- `1`: entrada.
- `2`: salida normal.
- `3`: salida automática/corrección.

El Gateway puede solicitar el estado con `!equipo`; el Router responde `CMD_RESP_EQUIPO`. El Gateway puede solicitar el historial con `!historial`; el Router responde `CMD_RESP_HISTORIAL` leyendo SPIFFS.

## 8. Integración que debe verificarse antes de desplegar

Existe una diferencia de protocolo entre dos implementaciones presentes en el repositorio:

- `zigbee_router/` actualmente comunica eventos mediante el cluster `0xFF00` y comandos `0x01`–`0x06`.
- `echo/main/` contiene un protocolo nuevo mediante el cluster `0xFC00`, endpoint 10 y comandos `0x00` para evento, `0x01` ping y `0x02` ACK.

Para que las placas se comuniquen, Gateway, Router y End Device deben usar exactamente el mismo perfil, canal, PAN/red Zigbee, endpoint y cluster. Antes de conectar el `echo/` Gateway definitivo con este Router, hay que elegir una única capa de integración:

- **Opción compatible con el Router actual:** adaptar el Gateway al cluster `0xFF00` y al comando `CMD_NOTIFY_GATEWAY`.
- **Opción más nueva:** ampliar el Router para enviar el `lab_event_t` de 64 bytes al cluster `0xFC00`, comando `0x00`, y retirar o reemplazar el protocolo anterior.

No es suficiente tener ambos nombres de proyecto abiertos en ESP-IDF: solo el firmware compilado y grabado en cada placa determina el protocolo activo.

## 9. Relación con los demás componentes del repositorio

| Carpeta o archivo | Papel |
|---|---|
| `zigbee_router/main/zigbee_router.c` | Lógica principal del Router, FreeRTOS, NVS, SPIFFS y ZCL |
| `zigbee_router/main/zigbee_router.h` | Configuración del Router y comandos del protocolo heredado |
| `main/main.c` | Implementación del End Device lector PN532 que descubre el Router |
| `echo/main/zigbee_gateway.c` | Gateway y coordinador alternativo/integradable con Discord y ThingSpeak |
| `echo/main/lab_protocol.h` | Formato estructurado nuevo de evento para el cluster `0xFC00` |
| `echo/main/lab_store.h` / `lab_store.c` | Almacenamiento usado por la implementación del Gateway |
| `echo/main/discord_bot.h` / `discord_bot.c` | Presentación de eventos y respuestas a comandos Discord |
| `echo/main/thingspeak.h` / `thingspeak.c` | Publicación de eventos al canal ThingSpeak |

## 10. Compilación del Router

Desde la carpeta `zigbee_router`:

```bash
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```

Reemplazar `COMx` por el puerto asignado al ESP32-C6. En Visual Studio Code, el proyecto puede compilarse con las tareas y configuraciones del SDK de ESP-IDF.

Antes de grabar:

1. Confirmar la partición `spiffs` en `partitions.csv`.
2. Confirmar que el Gateway esté formado y listo antes de abrir la unión del Router.
3. Confirmar el canal y el protocolo Zigbee compartido.
4. Configurar los UIDs reales en `tabla_equipos`.
5. Usar el monitor serie para revisar el rol, PAN ID, canal y dirección corta asignada.

## 11. Flujo de prueba recomendado

1. Grabar y encender el Gateway/Coordinador.
2. Confirmar que el Gateway abra la formación o haya creado la red.
3. Grabar y encender el Router.
4. Verificar por el monitor serie que se convirtió en `ROUTER` y obtuvo una dirección corta.
5. Abrir la red al End Device durante el periodo configurado.
6. Grabar el End Device y confirmar que descubre el Router.
7. Leer una tarjeta registrada: debe aparecer entrada y persistirse.
8. Reiniciar el Router: debe recuperar el estado ocupado.
9. Leer la misma tarjeta: debe aparecer salida y calcular estancia.
10. Leer otro equipo con una sesión activa: debe aparecer salida automática del anterior y entrada del nuevo.
11. Ejecutar consultas desde el Gateway y verificar tanto estado como historial.

## 12. Diagnóstico rápido

- **El Router no se une:** comprobar que el Coordinador esté activo, que la red permita miembros y que el canal coincida.
- **El End Device no encuentra el Router:** confirmar Endpoint 10 y cluster compatible en ambos extremos.
- **Se pierde el estado al reiniciar:** revisar NVS y cualquier borrado de flash en el proceso de flasheo.
- **No aparece el historial:** revisar que la partición `spiffs` esté grabada y que `/spiffs/historial.txt` pueda abrirse.
- **El Gateway no recibe eventos:** confirmar que el cluster y el comando coincidan; verificar también que el Router tenga red Zigbee activa.
- **No reconoce el equipo:** agregar el UID hexadecimal real a `tabla_equipos`.
- **El equipo no cambia de libre a ocupado:** revisar el tamaño y contenido del UID recibido por Zigbee.
