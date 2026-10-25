#ifndef SENSOR_H
#define SENSOR_H

#include "main.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define ADC_RESOLUTION 4095.0f  // 12-bit
#define VREF 3.3f

#define BETA 3200.0f
#define TEMPERATURE_25_K 298.15f
#define R_THERMISTOR_25 10000.0f
#define R_DIVISOR 9900.0f

typedef struct {
    ADC_HandleTypeDef *hadc;
    uint32_t channel;
} adc_sensor_handle_t;

void adc_sensor_init(adc_sensor_handle_t *sensor);
float temperature_sensor_read(adc_sensor_handle_t *sensor);

#endif // SENSOR_H