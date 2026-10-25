# Protocolo serie

El firmware habla dos protocolos de línea: uno **de control** (comandos y respuestas) y uno
**de telemetría** (trama periódica hacia el dashboard). Ambos usan el mismo formato de
transporte.

## 1. Transporte

| Parámetro | Valor |
| --- | --- |
| Puerto del ESP-01 | USART3, PC4 (TX) / PC5 (RX) |
| Puerto de depuración | USART2, PA2 (TX) / PA3 (RX) |
| Baudrate | 115200 |
| Formato | 8N1, sin control de flujo |
| Fin de línea | `\n`; también se acepta `\r` y se ignoran las líneas vacías |
| Tamaño máximo de línea | 199 caracteres; una línea más larga se descarta |
| Codificación | ASCII |

Cada comando se acumula byte a byte por interrupción hasta recibir `\n` o `\r`. En ese
momento se marca como listo y se procesa en el bucle principal.

> En la implementación actual, **USART2 y USART3 comparten el mismo buffer de recepción** y
> la misma bandera. Además, todas las respuestas se transmiten siempre por **USART3**. Si se
> escribe un comando por la consola del PC, se procesará, pero la respuesta saldrá por el
> ESP-01. Ver `arquitectura.md` §9.

## 2. Comandos de control

La comparación de `GET_TEMP` y `GET_STATUS` usa `strstr`, de modo que el texto puede
aparecer en cualquier posición de la línea. `FORCE_FAN:` y `SET_PASS:` se comparan como
prefijo.

| Comando | Respuesta | Efecto |
| --- | --- | --- |
| `GET_TEMP` | `Temperatura: 25.50C\r\n` | Devuelve la última temperatura medida |
| `GET_STATUS` | `ESTADO: BLOQUEADO\r\n` o `ESTADO: DESBLOQUEADO FAN: 30 TEMPERATURA: 25.50C\r\n` | El detalle de FAN y temperatura solo se incluye si la sala está desbloqueada |
| `FORCE_FAN:N` | `OK: FAN LEVEL N\r\n` | Inicia la rampa hacia el nivel N (0 = 0 %, 1 = 30 %, 2 = 70 %, 3 = 100 %) |
| `SET_PASS:XXXX` | `OK: PASS CAMBIADO\r\n` | Cambia la contraseña si `XXXX` tiene exactamente 4 caracteres |

### 2.1 `GET_TEMP`

```text
→ GET_TEMP\n
← Temperatura: 25.50C\r\n
```

La parte decimal se calcula como `(int)(temperatura * 100) % 100` sin relleno de dos
dígitos. Por eso una lectura de 25,05 °C se responde como `25.5C`, y una temperatura
negativa no se representa correctamente.

### 2.2 `GET_STATUS`

Sala bloqueada:

```text
→ GET_STATUS\n
← ESTADO: BLOQUEADO\r\n
```

Sala desbloqueada (ejemplo a 25,50 °C y 30 % de PWM):

```text
→ GET_STATUS\n
← ESTADO: DESBLOQUEADO FAN: 30 TEMPERATURA: 25.50C\r\n
```

Cualquier estado distinto de `ROOM_STATE_UNLOCKED` (`INPUT_PASSWORD`, `ACCESS_DENIED`)
se reporta como `BLOQUEADO`.

### 2.3 `FORCE_FAN:N`

```text
→ FORCE_FAN:2\n
← OK: FAN LEVEL 2\r\n
```

Solo se interpreta **el primer carácter después de los dos puntos**
(`nivel = linea[10] - '0'`). Por eso `FORCE_FAN:12` se interpreta como nivel 1 y el `2`
restante se ignora. Un carácter fuera de `0`–`3` no ejecuta ninguna rampa, pero el firmware
responde igualmente `OK: FAN LEVEL n`.

La intención del diseño es mantener el nivel forzado durante 5 s y volver después al
control automático. En el código actual esa temporización está invertida por un desbordamiento
de `uint32_t` y el forzado se cancela en la iteración siguiente (ver deuda técnica §9-1 y §9-4
en `docs/arquitectura.md`).

### 2.4 `SET_PASS:XXXX`

```text
→ SET_PASS:4321\n
← OK: PASS CAMBIADO\r\n
```

Solo se acepta una cadena de longitud exactamente 4. No se valida que los caracteres sean
dígitos. Si la longitud no es 4, el comando se ignora **sin respuesta**.

## 3. Telemetría

Cada 500 ms el firmware envía por USART3:

```text
T=25.50;STATE=LOCKED;FAN=0\r\n
T=26.10;STATE=UNLOCKED;FAN=30\r\n
```

| Campo | Formato | Significado |
| --- | --- | --- |
| `T` | `%d.%02d` | Temperatura en °C, dos decimales (con la misma truncación del resto del firmware) |
| `STATE` | `LOCKED` / `UNLOCKED` | `UNLOCKED` solo en estado desbloqueado; los demás estados se reportan como `LOCKED` |
| `FAN` | entero 0–100 | Porcentaje de PWM leído de `TIM3->CCR1 / 10` |

La trama termina en `\r\n`. El período efectivo puede ser algo mayor que 500 ms porque el
bucle principal está dominado por el redibujado de la OLED.

## 4. Mensajes espontáneos

| Mensaje | Cuándo | Puerto |
| --- | --- | --- |
| `ACCESO DENEGADO\r\n` | Una vez por cada contraseña incorrecta | USART3 y USART2 |
| `\r\nComando recibido\r\n` | Al procesar cualquier comando | USART2 (solo depuración) |
| `Comando desconocido.\r\n` | Línea sin comando reconocido | USART2 (solo depuración) |
| `Accion ejecutada: Fan a N\r\n` | Tras `FORCE_FAN` | USART2 (solo depuración) |
| `Password actualizado remotamente\r\n` | Tras `SET_PASS` válido | USART2 (solo depuración) |

El dashboard no debe asumir que la única entrada por USART3 es la trama `T=…`: también
puede recibir `ACCESO DENEGADO` cuando alguien falla la contraseña.

## 5. Secuencia típica con el dashboard

```mermaid
sequenceDiagram
    participant D as Node-RED
    participant E as ESP-01
    participant S as STM32
    S->>E: T=25.50;STATE=LOCKED;FAN=0 (cada 500 ms)
    D->>E: GET_STATUS
    E->>S: GET_STATUS\n
    S->>E: ESTADO: BLOQUEADO
    D->>E: FORCE_FAN:3
    E->>S: FORCE_FAN:3\n
    S->>E: OK: FAN LEVEL 3
```

## 6. Limitaciones del protocolo

1. No hay verificación de integridad (CRC) ni acuse de recibo a nivel de trama.
2. La contraseña viaja en texto plano por el enlace serie.
3. No existe respuesta de error: los comandos mal formados se ignoran o se confirman sin
   efecto.
4. `GET_TEMP` y `GET_STATUS` se buscan como subcadena, así que una línea con texto extra
   puede disparar el comando.
5. La telemetría no distingue `INPUT_PASSWORD` ni `ACCESS_DENIED` de `LOCKED`.

Estas limitaciones describen el comportamiento actual; no son cambios pendientes en este
documento.
