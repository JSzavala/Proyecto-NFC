# Lector NFC Zigbee (ESP32-C6)

El ESP32-C6 con PN532 funciona como **Zigbee End Device**. Al detectar una
tarjeta ISO14443A, envía el UID en bytes al Router:

| Parámetro | Valor |
| --- | --- |
| Perfil | Home Automation (`0x0104`) |
| Endpoint | `10` |
| Cluster | `0xFF00` |
| Comando | `0x01` |
| Payload | UID como hexadecimal ASCII en mayúsculas (8, 14 o 20 caracteres) |

El Router recibe el comando `0x01` en el endpoint 10, decodifica el UID
hexadecimal y continúa con la lógica de entrada/salida del laboratorio. Enviar
hexadecimal evita que un UID binario cuyos bytes coincidan con caracteres
hexadecimales sea confundido por el parser del Router. El lector descubre
dinámicamente el Router por su cluster `0xFF00`, por lo que no depende de una
dirección Zigbee corta fija.

El UID completo llega al Router. El protocolo Router-Gateway existente reserva
solo 8 bytes para el campo de tarjeta y el Router copia como máximo 7 caracteres;
por tanto, el UID enviado al Gateway queda truncado. Corregir ese tramo requiere
actualizar y reflashear los firmwares del Router y del Gateway.

## Puesta en marcha

1. Enciende el Gateway/coordinador y confirma que formó la red (PAN `0x1A62`,
   canal `13`).
2. Enciende el Router Zigbee y espera a que se una al Gateway. Al unirse, el
   Router abre la red durante 180 segundos para permitir que se unan dispositivos.
3. Enciende el Lector NFC dentro de esa ventana de 180 segundos para su primera
   unión. Esperará a unirse y encontrar el Router antes de inicializar el PN532
   y empezar a leer tarjetas.
4. Conecta el PN532 por UART HSU: TX del PN532 a GPIO 4 (RX del ESP32-C6),
   RX del PN532 a GPIO 5 (TX del ESP32-C6), y GND común.

El lector busca por los canales Zigbee y conserva su asociación en NVS. Si
cambias la red, borra la configuración Zigbee/NVS del dispositivo para que
vuelva a hacer commissioning como dispositivo nuevo. Los tres nodos deben
usar la misma política de códigos de instalación.

La tabla de particiones incluye `zb_storage` y `zb_fct`, requeridas por el
stack Zigbee. Si ya se había cargado una imagen con la tabla anterior, borra y
regraba únicamente la placa LectorNFC para instalar la tabla nueva:

```powershell
idf.py -p COMx erase-flash flash monitor
```
