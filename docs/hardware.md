# Hardware y configuración de periféricos

Todos los datos provienen de `Room_Control_Final_2025_2.ioc`, `Core/Inc/main.h` y del
código de inicialización generado por CubeMX.

## 1. Placa y alimentación

- **MCU:** STM32L476RGT6 (Cortex-M4F, 1 MB Flash, 128 KB SRAM) en NUCLEO-L476RG.
- **Lógica:** 3,3 V. Los pines no son tolerantes a 5 V salvo que el datasheet lo indique.
- **Ventilador:** 12 V, requiere etapa de potencia externa; PA6 solo entrega la señal PWM.
- **ESP-01:** 3,3 V, con una fuente capaz de entregar picos de corriente; no alimentarlo
  desde el regulador del ST-Link.

## 2. Relojes

| Dominio | Frecuencia | Origen |
| --- | --- | --- |
| SYSCLK | 80 MHz | HSI 16 MHz → PLL (M = 1, N = 10, R = 2) |
| HCLK (AHB) | 80 MHz | SYSCLK / 1 |
| PCLK1 / PCLK2 | 80 MHz | HCLK / 1 |
| Reloj de TIM3 | 80 MHz | PCLK1 |
| Latencia de Flash | 4 wait states | `FLASH_LATENCY_4` |

El firmware no habilita el LSE ni el RTC. Los pines PC14/PC15 aparecen reservados para el
oscilador de 32,768 kHz en el `.ioc`, pero quedan sin uso.

## 3. Pinout

| Pin | Etiqueta | Función | Configuración |
| --- | --- | --- | --- |
| PA0 | `ADC1_IN5` | Sensor NTC | Entrada analógica |
| PA2 | `USART_TX` | USART2_TX → ST-Link VCP | Alterna, 115200 8N1 |
| PA3 | `USART_RX` | USART2_RX ← ST-Link VCP | Alterna, 115200 8N1 |
| PA4 | `DOOR_STATUS` | Cerradura / indicador de puerta | Salida push-pull |
| PA5 | `LD2` | LED verde de la placa (heartbeat) | Salida push-pull |
| PA6 | `FAN_PWM` | TIM3_CH1 → etapa del ventilador | PWM alterno |
| PA8 | `KEYPAD_C2` | Columna 2 del teclado | EXTI flanco de bajada, pull-up |
| PA9 | `KEYPAD_C3` | Columna 3 del teclado | EXTI flanco de bajada, pull-up |
| PA10 | `KEYPAD_R1` | Fila 1 del teclado | Salida push-pull |
| PA13 | `TMS` | SWDIO | Serial Wire |
| PA14 | `TCK` | SWCLK | Serial Wire |
| PB3 | `KEYPAD_R2` | Fila 2 del teclado | Salida push-pull (JTDO) |
| PB4 | `KEYPAD_R4` | Fila 4 del teclado | Salida push-pull (NJTRST) |
| PB5 | `KEYPAD_R3` | Fila 3 del teclado | Salida push-pull |
| PB8 | `I2C1_SCL` | OLED SCL | I²C |
| PB9 | `I2C1_SDA` | OLED SDA | I²C |
| PB10 | `KEYPAD_C1` | Columna 1 del teclado | EXTI flanco de bajada, pull-up |
| PC4 | `USART3_TX` | ESP-01 RX | Alterna, 115200 8N1 |
| PC5 | `USART3_RX` | ESP-01 TX | Alterna, 115200 8N1 |
| PC7 | `KEYPAD_C4` | Columna 4 del teclado | EXTI flanco de bajada, pull-up |
| PC13 | `B1` | Botón azul de la placa | EXTI flanco de bajada, sin pull |
| PC14 / PC15 | `OSC32_IN/OUT` | Reservado para LSE | No habilitado en el firmware |

> **Advertencia:** PB3 y PB4 son JTDO y NJTRST. El firmware los usa como GPIO, así que la
> depuración debe hacerse por **SWD** (PA13/PA14). Un adaptador configurado en JTAG no
> podrá controlar el núcleo sin liberar esos pines.

## 4. Periféricos

### ADC1 — sensor de temperatura

| Parámetro | Valor |
| --- | --- |
| Resolución | 12 bits (0–4095) |
| Canal | 5 (PA0) |
| Tiempo de muestreo | 47,5 ciclos |
| Disparo | Software |
| Modo | Conversión única, sin DMA, sin oversampling |
| Alineación | Derecha |

La calibración se ejecuta una vez con `HAL_ADCEx_Calibration_Start()`. Cada lectura hace
`HAL_ADC_Start()`, `HAL_ADC_PollForConversion()` con `HAL_MAX_DELAY` y `HAL_ADC_GetValue()`.

### TIM3 — PWM del ventilador

| Parámetro | Valor |
| --- | --- |
| Prescaler | 799 |
| Periodo (ARR) | 999 |
| Frecuencia | 100 Hz |
| Resolución | 1000 cuentas (0,1 % por cuenta) |
| Modo | PWM 1, canal 1, polaridad alta |
| DMA | DMA1_Channel6, request 5, memoria → periférico, 32 bits, modo normal, prioridad alta |

El duty se actualiza una vez por periodo (10 ms). Cada tabla de rampa es un arreglo de
`uint32_t` en RAM (no `uint16_t`) porque el DMA está configurado a 32 bits por transferencia.

### I2C1 — OLED SSD1306

| Parámetro | Valor |
| --- | --- |
| Dirección | `0x3C << 1` (`0x78` de 8 bits) |
| Timing | `0x10D19CE4` ≈ 100 kHz |
| Modo | 7 bits, filtro analógico activo, filtro digital 0 |
| Pines | PB8 (SCL), PB9 (SDA) |

A 100 kHz, una trama completa de 128×64 (1024 bytes) tarda del orden de 100 ms.

### USART2 y USART3

| Parámetro | Valor |
| --- | --- |
| Baudrate | 115200 |
| Formato | 8 bits, sin paridad, 1 bit de parada |
| Flujo | Sin control de hardware |
| Oversampling | 16 |

- **USART2** (PA2/PA3): consola de depuración. `printf` se redirige aquí con `_write()`.
- **USART3** (PC4/PC5): enlace con el ESP-01. Recibe comandos y envía respuestas y la trama
  de telemetría.

### Interrupciones

| IRQ | Preferencia | Uso |
| --- | --- | --- |
| `USART2_IRQn` | 0 | Recepción byte a byte del PC |
| `USART3_IRQn` | 0 | Recepción byte a byte del ESP-01 |
| `DMA1_Channel6_IRQn` | 2 | Fin de la transferencia PWM |
| `EXTI9_5_IRQn` | 3 | Columnas C2, C3 y C4 |
| `EXTI15_10_IRQn` | 3 | Columna C1 y botón B1 |

Agrupación de prioridades: `NVIC_PRIORITYGROUP_4` (4 bits de preferencia, 0 de subprioridad).

## 5. Notas eléctricas

### Divisor del NTC

El código de `sensor.c` calcula:

```
V    = ADC / 4095 * 3.3
Rntc = R_serie * (3.3 - V) / V
T    = 1 / (1/298.15 + ln(Rntc/10000) / 3200) - 273.15
```

Esa expresión corresponde a un **NTC conectado entre 3,3 V y el nodo de medida**, con la
resistencia de serie (9,9 kΩ) del nodo a GND. Si el montaje real tiene el NTC a GND y la
resistencia hacia 3,3 V, la fórmula debe invertirse. Conviene confirmarlo contra el
esquemático antes de calibrar.

Parámetros usados: β = 3200 K, R25 = 10 kΩ, R_serie = 9,9 kΩ, VREF = 3,3 V.

### Cerradura

PA4 es una salida lógica de 3,3 V; no puede accionar una cerradura directamente. En el
firmware, `GPIO_PIN_RESET` significa **cerrado** y `GPIO_PIN_SET`, **abierto**.

### Ventilador

PA6 entrega una señal PWM de 3,3 V y 100 Hz. La etapa de potencia debe incluir el
accionamiento del motor y el diodo de recirculación para la carga inductiva. Un MOSFET o
driver de ventilador de 12 V con entrada lógica de 3,3 V es suficiente.

### Teclado

Las filas se manejan como salidas y se activan de a una; las columnas son entradas con
pull-up y generan una interrupción por flanco de bajada. El mapa de teclas está en
`Drivers/keypad/keypad.c`:

```
1 2 3 A
4 5 6 B
7 8 9 C
* 0 # D
```

Si el módulo físico tiene otra disposición de filas o columnas, hay que ajustar el arreglo
`keypad_map` y, si cambia el cableado, las tablas de pines de `main.c`.

### ESP-01

El módulo debe compartir niveles de 3,3 V con USART3. La alimentación del ESP-01 es
independiente de la del ST-Link y debe soportar los picos de transmisión Wi-Fi.
