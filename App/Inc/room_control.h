#ifndef ROOM_CONTROL_H
#define ROOM_CONTROL_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

#define PASSWORD_LENGTH 4
#define MAX_TEMP_READINGS 5

typedef enum {
    ROOM_STATE_LOCKED,
    ROOM_STATE_UNLOCKED,
    ROOM_STATE_INPUT_PASSWORD,
    ROOM_STATE_ACCESS_DENIED,
    ROOM_STATE_EMERGENCY
} room_state_t;

typedef enum {
    FAN_LEVEL_OFF = 0,    // 0% PWM
    FAN_LEVEL_LOW = 30,   // 30% PWM  
    FAN_LEVEL_MED = 70,   // 70% PWM
    FAN_LEVEL_HIGH = 100  // 100% PWM
} fan_level_t;

typedef struct {
    room_state_t current_state;
    char password[PASSWORD_LENGTH + 1];
    char input_buffer[PASSWORD_LENGTH + 1];
    uint8_t input_index;
    uint32_t last_input_time;
    uint32_t state_enter_time;
    uint32_t star_force_time;
    
    // Door control
    bool door_locked;
    
    // Temperature and fan control  
    float current_temperature;
    fan_level_t current_fan_level;
    bool manual_fan_override;
    uint32_t fan_state;
    bool access_denied;
    
    // Display update flags
    bool display_update_needed;
    bool fan_force;
} room_control_t;

// Public functions
void room_control_init(room_control_t *room);
void room_control_update(room_control_t *room);
void room_control_process_key(room_control_t *room, char key);
void room_control_update_temperature(room_control_t *room);
void room_control_force_fan_level(room_control_t *room,int level);
void room_control_change_password(room_control_t *room, const char *new_password);

void calculate_pwm_tables(void);
void delay_ms(uint32_t ms);

// Status getters
room_state_t room_control_get_state(room_control_t *room);
bool room_control_is_door_locked(room_control_t *room);
int room_control_get_fan_level(void);
float room_control_get_temperature(room_control_t *room);

#endif