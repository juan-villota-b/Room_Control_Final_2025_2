#include "room_control.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "sensor.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

// Default password
static const char DEFAULT_PASSWORD[] = "1111";

// Temperature thresholds for automatic fan control
static const float TEMP_THRESHOLD_LOW = 25.0f;
static const float TEMP_THRESHOLD_MED = 30.0f;  
static const float TEMP_THRESHOLD_HIGH = 40.0f;
extern TIM_HandleTypeDef htim3;
extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart2;
extern adc_sensor_handle_t temp_sensor;
extern uint32_t finish_pwm;

uint32_t pwm_0[1]={0};
uint32_t pwm_0_30[60];
uint32_t pwm_30_0[60];
uint32_t pwm_30_70[80];
uint32_t pwm_70_30[80];
uint32_t pwm_70_100[60];
uint32_t pwm_100_70[60];

// Timeouts in milliseconds
static const uint32_t INPUT_TIMEOUT_MS = 10000;  // 10 seconds
static const uint32_t ACCESS_DENIED_TIMEOUT_MS = 3000;  // 3 seconds
volatile uint32_t state=0;

// Private function prototypes
static void room_control_change_state(room_control_t *room, room_state_t new_state);
static void room_control_update_display(room_control_t *room);
static void room_control_update_door(room_control_t *room);
static void room_control_update_fan(room_control_t *room);
static fan_level_t room_control_calculate_fan_level(float temperature);
static void room_control_clear_input(room_control_t *room);

void room_control_init(room_control_t *room) {
    // Initialize room control structure
    room->current_state = ROOM_STATE_LOCKED;
    strcpy(room->password, DEFAULT_PASSWORD);
    room_control_clear_input(room);
    room->last_input_time = 0;
    room->state_enter_time = HAL_GetTick();
    
    // Initialize door control
    room->door_locked = true;
    
    // Initialize temperature and fan
    room->current_temperature = 22.0f;  // Default room temperature
    room->current_fan_level = FAN_LEVEL_OFF;
    room->manual_fan_override = false;
    room->star_force_time=0;
    room->access_denied=false;
    // Display
    room->display_update_needed = true;
    room->fan_force=false;
    
    // TODO: TAREA - Initialize hardware (door lock, fan PWM, etc.)
    HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_RESET);
}

void room_control_update(room_control_t *room) {
    uint32_t current_time = HAL_GetTick();
    
    // State machine
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            room->display_update_needed = true;
            room->door_locked = true;
            
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:
            if (current_time - room->last_input_time > INPUT_TIMEOUT_MS) {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        case ROOM_STATE_UNLOCKED:
            room->door_locked = false;
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            
            if (current_time - room->state_enter_time > ACCESS_DENIED_TIMEOUT_MS) {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        case ROOM_STATE_EMERGENCY:
            
            break;
    }
    
    // Update subsystems
    room_control_update_temperature(room);
    room_control_update_door(room);
    room_control_update_fan(room);
    room_control_update_display(room); 
        
}

void room_control_process_key(room_control_t *room, char key) {
    room->last_input_time = HAL_GetTick();
    
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            // Start password input
            room_control_clear_input(room);
            room->input_buffer[0] = key;
            room->input_index = 1;
            room_control_change_state(room, ROOM_STATE_INPUT_PASSWORD);
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:
            // TODO: TAREA - Implementar lógica de entrada de teclas
            // - Agregar tecla al buffer de entrada
            // - Verificar si se completaron 4 dígitos
            // - Comparar con contraseña guardada
            // - Cambiar a UNLOCKED o ACCESS_DENIED según resultado
            if (room->input_index < PASSWORD_LENGTH) {
                room->input_buffer[room->input_index++] = key;
            }
            // Check if password is complete
            if (room->input_index == PASSWORD_LENGTH) {
                room->input_buffer[PASSWORD_LENGTH] = '\0';  // Null-terminate
                if (strcmp(room->input_buffer, room->password) == 0) {
                    room_control_change_state(room, ROOM_STATE_UNLOCKED);
                    room_control_clear_input(room);
                } else {
                    room_control_change_state(room, ROOM_STATE_ACCESS_DENIED);
                    room->access_denied=true;
                    room_control_clear_input(room);
                }
            }
            break;
            
        case ROOM_STATE_UNLOCKED:
            // TODO: TAREA - Manejar comandos en estado desbloqueado (opcional)
            // Ejemplo: tecla '*' para volver a bloquear
            if (key == '*') {
                room_control_change_state(room, ROOM_STATE_LOCKED);
            }
            break;
            
        default:
            break;
    }
    
    room->display_update_needed = true;
}

void room_control_update_temperature(room_control_t *room) {
    
    float temperature = temperature_sensor_read(&temp_sensor);
    room->current_temperature = temperature;
    
    // Update fan level automatically if not in manual override
    if (!room->manual_fan_override) {
        fan_level_t new_level = room_control_calculate_fan_level(temperature);
        if (new_level != room->current_fan_level) {
            room->current_fan_level = new_level;
            room->display_update_needed = true;
        }
    }
}

void room_control_force_fan_level(room_control_t* room, int level) {
    switch (level)
    {
    case 0:
        HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_0,1);      
        break;
    case 1:
        HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_0_30,60);
        break;    
    case 2:
        HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_30_70,80);
        break;    
    case 3:
        HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_70_100,60);
        break;
 
    default:
        break;
    }
    finish_pwm=1;
    room->fan_force=true;
    room->star_force_time=HAL_GetTick();
}

void room_control_change_password(room_control_t *room, const char *new_password) {
    if (strlen(new_password) == PASSWORD_LENGTH) {
        strcpy(room->password, new_password);
    }
}

// Status getters
room_state_t room_control_get_state(room_control_t *room) {
    return room->current_state;
}

bool room_control_is_door_locked(room_control_t *room) {
    return room->door_locked;
}

int room_control_get_fan_level(void) {
    return (TIM3->CCR1)/10;
}

float room_control_get_temperature(room_control_t *room) {
    return room->current_temperature;
}

// Private functions
static void room_control_change_state(room_control_t *room, room_state_t new_state) {
    room->current_state = new_state;
    room->state_enter_time = HAL_GetTick();
    room->display_update_needed = true;
    
    // State entry actions
    switch (new_state) {
        case ROOM_STATE_LOCKED:
            room->door_locked = true;
            room_control_clear_input(room);
            break;
            
        case ROOM_STATE_UNLOCKED:
            room->door_locked = false;
            room->manual_fan_override = false;  // Reset manual override
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            room_control_clear_input(room);
            break;
            
        default:
            break;
    }
}

static void room_control_update_display(room_control_t *room) {
    char display_buffer[32];
    
    ssd1306_Fill(Black);
    
    // TODO: TAREA - Implementar actualización de pantalla según estado
    switch (room->current_state) {
        case ROOM_STATE_LOCKED:
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("SISTEMA", Font_7x10, White);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString("BLOQUEADO", Font_7x10, White);
            break;
            
        case ROOM_STATE_INPUT_PASSWORD:
            // TODO: Mostrar asteriscos según input_index
            switch (room->input_index)
            {
            case 1:
                ssd1306_SetCursor(10, 10);
                ssd1306_WriteString("CLAVE: *", Font_7x10, White);
                break;
            case 2:
                ssd1306_SetCursor(10, 10);
                ssd1306_WriteString("CLAVE: **", Font_7x10, White);
                break;
            case 3:
                ssd1306_SetCursor(10, 10);
                ssd1306_WriteString("CLAVE: ***", Font_7x10, White);
                break;  
            case 4:
                ssd1306_SetCursor(10, 10);
                ssd1306_WriteString("CLAVE: ****", Font_7x10, White);
                break;
            default:
                break;
            }
            // Ejemplo: mostrar asteriscos
            break;
            
        case ROOM_STATE_UNLOCKED:
            // TODO: Mostrar estado del sistema (temperatura, ventilador)
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("ACCESO OK", Font_7x10, White);
            
            snprintf(display_buffer, sizeof(display_buffer), "Temp: %d.%dC", (int)room->current_temperature,(int)(room->current_temperature*100)%100);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString(display_buffer, Font_7x10, White);
            
            snprintf(display_buffer, sizeof(display_buffer), "Fan: %d%%", (int)(TIM3->CCR1)/10);
            ssd1306_SetCursor(10, 40);
            ssd1306_WriteString(display_buffer, Font_7x10, White);
            break;
            
        case ROOM_STATE_ACCESS_DENIED:
            ssd1306_SetCursor(10, 10);
            ssd1306_WriteString("ACCESO", Font_7x10, White);
            ssd1306_SetCursor(10, 25);
            ssd1306_WriteString("DENEGADO", Font_7x10, White);
            if (room->access_denied==true){
                HAL_UART_Transmit(&huart3,(uint8_t*)"ACCESO DENEGADO\r\n",strlen("ACCESO DENEGADO\r\n"),100);
                HAL_UART_Transmit(&huart2,(uint8_t*)"ACCESO DENEGADO\r\n",strlen("ACCESO DENEGADO\r\n"),100);
                room->access_denied=false;
            }
            break;
            
        default:
            break;
    }
    
    ssd1306_UpdateScreen();
}

static void room_control_update_door(room_control_t *room) {
    // TODO: TAREA - Implementar control físico de la puerta
    // Ejemplo usando el pin DOOR_STATUS:
    if (room->door_locked) {
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(DOOR_STATUS_GPIO_Port, DOOR_STATUS_Pin, GPIO_PIN_SET);
    }
}

void delay_ms(uint32_t ms){
  uint32_t start = HAL_GetTick();
  while(HAL_GetTick() - start < ms);
}

static void room_control_update_fan(room_control_t *room) {
    uint32_t start_fan_time=HAL_GetTick();
    if (room->fan_force==true){
        if (room->star_force_time-start_fan_time>5000){
            finish_pwm=0;
            room->fan_force=false;
        }
    }
    uint32_t level=room->current_temperature;
    if (finish_pwm==0){
        if (level<=TEMP_THRESHOLD_LOW){
            if (state>0){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_30_0,60);
                finish_pwm=1;
                state=0;
            }
        }else if ((level>TEMP_THRESHOLD_LOW) && (level<TEMP_THRESHOLD_MED)){ 
            if (state<1){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_0_30,60);
                finish_pwm=1;
                state=1;
            }else if(state>1){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_70_30,80);
                finish_pwm=1;
                state=1;
            }
        }else if((level>=TEMP_THRESHOLD_MED) && (level<TEMP_THRESHOLD_HIGH)){
            if (state<2){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_30_70,80);
                finish_pwm=1;
                state=2;
            }else if(state>2){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_100_70,60);
                finish_pwm=1;
                state=2;
            }
        }else if((level>=TEMP_THRESHOLD_HIGH)){
            if (state<3){
                HAL_TIM_PWM_Start_DMA(&htim3,TIM_CHANNEL_1,pwm_70_100,60);
                finish_pwm=1;
                state=3;
            }
        }
    }
}

static fan_level_t room_control_calculate_fan_level(float temperature) {
    // TODO: TAREA - Implementar lógica de niveles de ventilador
    if (temperature < TEMP_THRESHOLD_LOW) {
        return FAN_LEVEL_OFF;
    } else if (temperature < TEMP_THRESHOLD_MED) {
        return FAN_LEVEL_LOW;
    } else if (temperature < TEMP_THRESHOLD_HIGH) {
        return FAN_LEVEL_MED;
    } else {
        return FAN_LEVEL_HIGH;
    }
}

static void room_control_clear_input(room_control_t *room) {
    memset(room->input_buffer, 0, sizeof(room->input_buffer));
    room->input_index = 0;
}

void calculate_pwm_tables(void){
    uint32_t j=0;
  for(uint32_t i=0; i<30; i++)
  {
    pwm_0_30[j]=i*10;
    pwm_0_30[j+1]=i*10;
    j=j+2;
  }
  j=0;  
  pwm_0_30[59]=300;

  for(uint32_t i=30; i>0; i--)
  {
    pwm_30_0[j]=i*10-10;
    pwm_30_0[j+1]=i*10-10;
    j=j+2;
  }
  j=0;
  //xx
  for(uint32_t i=30; i<70; i++)
  {
    pwm_30_70[j]=i*10;
    pwm_30_70[j+1]=i*10;
    j=j+2;
  }
  j=0;
  pwm_30_70[79]=700;
  for(uint32_t i=70; i>30; i--)
  {
    pwm_70_30[j]=i*10-10;
    pwm_70_30[j+1]=i*10-10;
    j=j+2;
  }
  j=0;
  //xx
  for(uint32_t i=70; i<100; i++)
  {
    pwm_70_100[j]=i*10;
    pwm_70_100[j+1]=i*10;
    j=j+2;
  }
  j=0;
  pwm_70_100[59]=1000;
  for(uint32_t i=100; i>70; i--)
  {
    pwm_100_70[j]=i*10-10;
    pwm_100_70[j+1]=i*10-10;
    j=j+2;
  }
  j=0;
}
