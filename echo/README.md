# Gateway de laboratorio IoT (ESP32-C6 + Discord + Zigbee + ThingSpeak)

Gateway para el laboratorio de NFC basado en el ejemplo
[esp-discord](https://github.com/abobija/esp-discord) para ESP-IDF. El ESP32-C6
concentra la red Zigbee, recibe los chequeos de NFC, los muestra por Discord y
los publica en ThingSpeak.

## Arquitectura

```
 End Device (NFC)          Router Zigbee                 Gateway (ESP32-C6)
 ─────────────────         ─────────────                 ──────────────────
 Lee tarjeta NFC  ──────►  Consolida y persiste  ──────►  Coordinador Zigbee
 y envia el evento         los eventos del dia           Wi-Fi + Discord
 por Zigbee                                              ThingSpeak + SNTP
                                                         Historial en NVS
```

- **End Device**: lee la tarjeta NFC y genera el evento de entrada/salida.
- **Router Zigbee**: consolida y persiste los eventos y los reenvia al Gateway.
- **Gateway (este proyecto)**: forma la red Zigbee (coordinador, direccion
  corta `0x0000`), recibe los eventos, los guarda en su propio historial NVS,
  responde a los comandos de Discord y publica en ThingSpeak.

El Gateway puede configurarse como **End Device** si el Router ya forma la red
(ver [Configuracion](#configuracion)).

## Comandos de Discord

| Comando | Descripcion |
| --- | --- |
| `!equipo` | Lista los equipos que estan dentro del laboratorio y la hora de su ultima entrada. |
| `!equipo <nombre>` | Muestra el ultimo evento, integrante y tarjeta del equipo indicado. |
| `!historial [N]` | Muestra los ultimos `N` eventos (1-30, por defecto `CONFIG_LAB_HISTORY_MAX`). |
| `!ayuda` / `!help` | Muestra la ayuda. |

Cada evento recibido tambien puede notificarse en tiempo real en un canal de
Discord (`CONFIG_LAB_DISCORD_NOTIFY_CHANNEL`). Si se deja vacio, se usa el
ultimo canal donde el bot recibio un mensaje.

## Protocolo Zigbee

Cluster ZCL personalizado, sin conflicto con clusters estandar:

| Campo | Valor |
| --- | --- |
| Perfil | `0x0104` (Home Automation) |
| Endpoint | `10` |
| Cluster | `0xFC00` |
| Comando de evento | `0x00` (`LAB_ZIGBEE_CMD_EVENT`) |
| Comando ping | `0x01` (`LAB_ZIGBEE_CMD_PING`) |
| Direccion destino | `0x0000` (coordinador) |

El payload puede ser binario (recomendado) o texto:

- **Binario**: `lab_event_t` empaquetado de 64 bytes (`main/lab_protocol.h`):

  ```c
  typedef struct __attribute__((packed)) {
      uint8_t  version;      /* LAB_PROTOCOL_VERSION = 1 */
      uint8_t  type;         /* 0=ENTRADA, 1=SALIDA, 2=CORRECCION */
      uint8_t  source;       /* 0=NFC, 1=MANUAL, 2=ROUTER */
      uint8_t  reserved;
      uint32_t timestamp;    /* Unix UTC; 0 = usar la hora del Gateway */
      char     team[24];
      char     member[24];
      char     card[8];
  } lab_event_t;
  ```

- **Texto** (util para pruebas):
  `v1|EQUIPO|INTEGRANTE|ENTRADA|1762812345|A1B2C3D4`

### Envio desde el Router Zigbee

```c
#include "esp_zigbee_core.h"

lab_event_t event = {0};
event.version = 1;
event.type = 0;                       /* ENTRADA */
event.source = 2;                     /* ROUTER */
event.timestamp = (uint32_t)time(NULL);
strncpy(event.team, "Equipo 1", sizeof(event.team) - 1);
strncpy(event.member, "Ana", sizeof(event.member) - 1);
strncpy(event.card, "A1B2C3D4", sizeof(event.card) - 1);

esp_zb_zcl_custom_cluster_cmd_req_t req = {
    .zcl_basic_cmd = {
        .dst_addr_u.addr_short = 0x0000,
        .dst_endpoint = 10,
        .src_endpoint = 10,
    },
    .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
    .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
    .cluster_id = 0xFC00,
    .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
    .custom_cmd_id = 0x00,
    .data = {
        .type = ESP_ZB_ZCL_ATTR_TYPE_SET,
        .size = sizeof(event),
        .value = &event,
    },
};

esp_zb_zcl_custom_cluster_cmd_req(&req);
```

El Router debe unirse a la PAN del Gateway (PAN ID `0x1A62`, canal `13` por
defecto) antes de enviar eventos.

## ThingSpeak

Cada chequeo NFC se encola y se publica por HTTP POST respetando el limite de
ThingSpeak (una actualizacion cada 15 s como minimo). Campos:

| Campo | Contenido |
| --- | --- |
| `field1` | Equipo |
| `field2` | Evento (`ENTRADA` / `SALIDA` / `CORRECCION`) |
| `field3` | Integrante |
| `field4` | Fecha y hora local (`YYYY-MM-DD HH:MM:SS`) |
| `field5` | UID de la tarjeta NFC |
| `field6` | Origen (`NFC` / `MANUAL` / `ROUTER`) |

La publicacion se desactiva automaticamente si `CONFIG_LAB_THINGSPEAK_API_KEY`
esta vacio.

## Configuracion

Requisitos: ESP-IDF **v5.5.6** y target **esp32c6**.

```bash
source ~/.espressif/tools/activate_idf_v5.5.6.sh
idf.py set-target esp32c6
idf.py menuconfig
```

Opciones del proyecto en **Laboratorio IoT (Gateway)**:

- `Rol Zigbee del Gateway`: `Coordinador` (por defecto) o `End Device`.
- `Canal Zigbee primario` (11-26, por defecto 13).
- `PAN ID del Gateway` (por defecto `0x1A62`).
- `URL de actualizacion de ThingSpeak` y `Write API Key`.
- `Segundos minimos entre publicaciones a ThingSpeak` (15-300).
- `ID de canal de Discord para notificaciones`.
- `Eventos que devuelve !historial por defecto`.
- `Servidor NTP` y `Zona horaria POSIX` (por defecto `CST6`).

Ademas, en **Example Connection Configuration** configura el SSID y password
de Wi-Fi, y en **Component Config > Discord > Token** el token del bot.

> Para usar el Gateway como **End Device** selecciona tambien
> **Component Config > Zigbee > Zigbee End Device** (`CONFIG_ZB_ZED`); el
> firmware no compila si el rol y el stack no coinciden.

## Compilar y flashear

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

La tabla de particiones `partitions.csv` asigna ~1.94 MB a la aplicacion
(flash de 2 MB), porque Wi-Fi + TLS de Discord + Zigbee superan el tamano de la
tabla `Single app (large)` por defecto.

## Estructura del codigo

| Archivo | Funcion |
| --- | --- |
| `main/echo.c` | Arranque: NVS, Wi-Fi, SNTP, ThingSpeak, Discord y Zigbee. |
| `main/zigbee_gateway.c` | Coordinador/End Device Zigbee, endpoint y cluster `0xFC00`. |
| `main/lab_protocol.c` | Estructura y parser del evento (binario/texto). |
| `main/lab_store.c` | Historial circular de 64 eventos persistido en NVS. |
| `main/discord_bot.c` | Comandos `!equipo` / `!historial` y notificaciones. |
| `main/thingspeak.c` | Cola de publicacion y limite de 15 s. |
| `main/Kconfig.projbuild` | Opciones de configuracion del proyecto. |

## Notas

- El historial del Gateway es independiente del que persiste el Router; sirve
  para que `!equipo` y `!historial` funcionen incluso tras reiniciar el Gateway.
- `CORRECCION` se trata como salida para el conteo de equipos dentro.
- Los nombres de equipo/integrante no deben contener el caracter `|` cuando se
  usa el formato de texto.
