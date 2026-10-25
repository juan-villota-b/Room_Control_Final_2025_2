#include "sensor.h"

void adc_sensor_init(adc_sensor_handle_t *sensor) {
    // Inicialización si es necesaria
    HAL_ADCEx_Calibration_Start(sensor-> hadc, ADC_SINGLE_ENDED);
}

float temperature_sensor_read(adc_sensor_handle_t *sensor) {
    HAL_ADC_Start(sensor->hadc);
    HAL_ADC_PollForConversion(sensor->hadc, HAL_MAX_DELAY);
    uint32_t adc_value = HAL_ADC_GetValue(sensor->hadc);
    // Conversión simplificada (ajusta según tu termistor)
    float voltage = (adc_value / ADC_RESOLUTION) * VREF;
    float Rntc = (VREF*R_DIVISOR)/voltage-R_DIVISOR;
    float temperature =1.0f/(1.0f/TEMPERATURE_25_K +logf(Rntc/R_THERMISTOR_25)/BETA)-273.15f;

    return temperature;
}