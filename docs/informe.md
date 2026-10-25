# INFORME DEL SISTEMA DE CONTROL DE SALA  
**Placa:** Nucleo STM32L476RG  
**Conectividad:** ESP-01 por USART3  
**Sensado:** NTC por ADC  
**Interfaz:** Teclado 4x4 + OLED SSD1306  
**Actuador:** Ventilador de 12V  
**Autores :** Juan David Villota, Sebastián Tovar

---

# 1. Introducción


El sistema administra una puerta, un ventilador PWM, un teclado matricial 4x4, un sensor de temperatura por ADC, y se comunica inalámbricamente con un módulo **ESP01**. La finalidad del informe es dejar por escrito la **arquitectura**, **señales**, **secuencias**, **máquina de estados**, y **lógica empleada**, sin alteraciones ni correcciones al código original.


---

# 2. Arquitectura General del Proyecto

El sistema está dividido en tres módulos principales:

### ✔ **`main.c`**  
Coordina la ejecución completa: inicialización de periféricos, comunicación UART, lectura del sensor, temporización, actualización de pantalla y llamada al módulo central `room_control`.

### ✔ **`room_control.c`**  
Incluye la lógica central del sistema:
- Máquina de estados del control de sala.
- Procesamiento del teclado.
- Control de la puerta (apertura/cierre).
- Control del ventilador mediante PWM y tablas DMA.
- Control manual y forzado.
- Manejo de contraseña.
- Actualización del display OLED.

### ✔ **`sensor.c`**  
Encargado exclusivamente de:
- Inicialización del ADC.
- Lectura del NTC.
- Conversión ADC → voltaje → resistencia → temperatura en °C.

---

# 3. Descripción del Funcionamiento por Archivo

---

# 3.1. `main.c` — Lógica Principal

El archivo `main.c` actúa como **núcleo del sistema**. Sus responsabilidades incluyen:

---

## 3.1.1 Inicialización de periféricos
Se inicializan:

- HAL y clock system.
- GPIO.
- ADC.
- UART2 (PC) y UART3 (ESP01).
- TIM3 para PWM.
- I2C1 para OLED SSD1306.
- Interrupciones EXTI para teclado.

---

## 3.1.2 Comunicación UART

### ● USART2  
Usado para monitoreo por PC.  
Recibe caracteres y los almacena en un buffer circular administrado por interrupción:

### ● USART3

Conecta el STM32 al ESP01.  
Se reciben comandos y se procesan mediante:

- `Procesar_Comandos_WiFi()`
- `FLAG_NewCommand_ESP01`

Los comandos reconocidos incluyen:

- `GET_TEMP` Muestra la temperatura
- `GET_STATUS` Muestra el temperatura, estado y PWM
- `FORCE_FAN` Cambia el nivel de PWM
- `SET_PASS` Cambia la contraseña actual.


---

## 3.1.3 Bucle principal (`while(1)`)

Dentro del loop principal:

- Se lee la temperatura mediante `temperature_sensor_read(&temp_sensor)`.
- Se actualiza la pantalla OLED.
- Se envía periódicamente (500 ms) un frame tipo “dashboard”.
- Se procesa cualquier comando WiFi recibido.
- Se llama a:

Este módulo gestiona el comportamiento según el estado del sistema.  
La estructura del loop es no bloqueante y depende totalmente de interrupciones para UART y teclado.

---

# 3.2. `room_control.c` — Lógica Central del Control de Sala

Este archivo contiene todo el comportamiento funcional del sistema.

---

## 3.2.1 Máquina de Estados

Los estados definidos son:

- `ROOM_STATE_LOCKED`
- `ROOM_STATE_INPUT_PASSWORD`
- `ROOM_STATE_UNLOCKED`
- `ROOM_STATE_ACCESS_DENIED`


---

## 3.2.2 Estado: **LOCKED**

- La puerta permanece cerrada.
- Se espera ingreso de contraseña desde el teclado.
- La temperatura sigue actualizándose.
- El ventilador opera en modo automático usando la lectura del sensor.

**Transiciones:**
- `INPUT_PASSWORD` si se detecta digitación válida.


---

## 3.2.3 Estado: **INPUT_PASSWORD**

En este estado:

- Se construye la contraseña ingresada.
- Cada tecla presionada agrega un dígito al buffer.
- El sistema compara el buffer con la contraseña almacenada cuando alcanza la longitud máxima.

**Transiciones:**

- Correcta → `UNLOCKED`
- Incorrecta → `ACCESS_DENIED`

---

## 3.2.4 Estado: **UNLOCKED**

- Se activa la apertura de la puerta mediante GPIO.
- Se muestra información en la pantalla OLED.
- El ventilador permanece habilitado.

**Retorna a `LOCKED` cuando:**

- Expira un tiempo definido.
- Se recibe orden remota.

---

## 3.2.5 Estado: **ACCESS_DENIED**

- Se muestra mensaje de acceso negado.
- Después de un intervalo configurable, regresa a `LOCKED`.


---

## 3.2.7 Procesamiento del Teclado Matricial

El teclado es manejado por interrupciones EXTI:

- Cada evento genera `keypad_process_key()`.
- `room_control_process_key()` recibe el carácter traducido.

**Acciones del teclado:**

- Confirmar.
- Ingresar al modo de bloqueado oprimiendo *

---

## 3.2.8 Control del Ventilador (PWM con DMA)

El ventilador opera mediante **TIM3 CH1**, usando tablas de transición suave con 60 u 80 valores.

**Tablas incluidas:**

- `pwm_0_30`
- `pwm_30_70`
- `pwm_70_100`
- `pwm_30_0`
- `pwm_100_70`
- `pwm_70_30`

**Lógica general:**

1. El ventilador se regula automáticamente según la temperatura.
2. Se usan banderas globales:
   - `state` → estado de transición PWM actual.
   - `finish_pwm` → marca cuando terminó una transferencia DMA.
3. Cada salto (0→30, 30→70, 70→100, etc.) ejecuta:

Existe un modo **`fan_force`** activado por comandos WiFi.

### 3.2.9 Control de la Puerta

Las funciones encargadas de la gestión del acceso físico son:

Estas funciones manipulan el GPIO asignado, permitiendo la activación de un actuador físico según la implementación del hardware con el ventilador

### 3.2.10 Manejo de Contraseña

El sistema de seguridad se basa en las siguientes variables principales dentro de la estructura de control:

* `room->password`: Almacena la contraseña actual válida.
* `room->input_buffer`: Almacena temporalmente la contraseña que está ingresando el usuario.

**Flujo del proceso:**

1.  El usuario ingresa caracteres uno a uno mediante el teclado matricial.
2.  Cuando el buffer de entrada se llena (4 dígitos), el sistema realiza una comparación:
    * **Si coincide** con la contraseña almacenada $\rightarrow$ **Puerta Desbloqueada**.
    * **Si no coincide** $\rightarrow$ **Acceso Denegado**.
3.  Adicionalmente, el comando remoto `SET_PASS` permite modificar la contraseña almacenada desde el módulo ESP-01 sin necesidad de acceso físico.

---

## 3.3. Análisis del Módulo `sensor.c` (Lector de Temperatura NTC)

Este módulo gestiona la conversión de magnitudes físicas a digitales.

### Inicialización del ADC

Se utilizan las funciones de la HAL de STM32 para preparar el periférico:

* **HAL_ADCEx_Calibration_Start:** Calibración automática para reducir errores de offset.
* **HAL_ADC_Start:** Inicia el muestreo.
* **HAL_ADC_PollForConversion:** Espera a que el hardware termine la conversión.

### Procesamiento Matemático

El código implementa la física del termistor NTC en tres pasos:

**Conversión ADC $\rightarrow$ Voltaje**
Se transforma el valor crudo del ADC (0-4095) a voltaje real mediante el cálculo de la NTC, porque no es lineal.


# 4. Diagrama General del Sistema 

[Teclado 4x4] → Interrupciones EXTI  
[NTC + ADC] → Sensor.c  
[OLED SSD1306] ← I2C1  
[ESP01] ←→ USART3  
[Fan PWM] ← TIM3 + DMA  
[Puerta] ← GPIO  
[room_control.c] ← Lógica central  
[main.c] ← Controlador 


---

# 5. Espacios para Imágenes

- Arquitectura del Firmware  
- Diagrama de Máquina de Estados  
- Señales PWM capturadas  
- Interfaz OLED  
- Conexión ESP01 – Nucleo  
![Diagrama de flujo](![Diagrama](ROOM_CONTROL_FINAL_2025_2/img1.png))

---

# 6. Conclusión

El sistema de control de sala desarrollado integra de manera eficiente sensado, actuadores, interfaz local y comunicación inalámbrica, logrando un funcionamiento estable y seguro. La arquitectura basada en la Nucleo STM32L476RG permite aprovechar periféricos como ADC, timers con PWM-DMA, interrupciones EXTI y UART para implementar un firmware no bloqueante y estructurado.

La lógica central, organizada mediante una máquina de estados, asegura un control claro del acceso, de la puerta y del ventilador, mientras que el sensor NTC proporciona mediciones confiables que permiten ajustar automáticamente la ventilación. La inclusión del módulo ESP01 añade capacidades de control remoto, ampliando el alcance del sistema y permitiendo futuras integraciones IoT.

En conjunto, el proyecto demuestra una integración correcta de hardware y software, logrando una solución robusta, funcional y escalable para el control inteligente de una sala.


# 7. Video demostrativo - Funcionamiento de Dashboard (Node-RED)
Link - Youtube: https://youtu.be/DbfoEAb-rX4?si=t2fPlXAObiiwzrSR
