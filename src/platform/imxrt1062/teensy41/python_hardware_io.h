#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
// Kinds: GPIO=0, ADC=1, PWM=2, I2C=3, SPI slot=4, UART slot=5.
esp_err_t sk_py_hw_open(int kind,int id,int option,int value,uint32_t *handle);
esp_err_t sk_py_hw_close(uint32_t handle);
esp_err_t sk_py_hw_value(uint32_t handle,int operation,int value,int *result);
esp_err_t sk_py_hw_transfer(uint32_t handle,int address,const uint8_t *tx,size_t txlen,uint8_t *rx,size_t rxlen,size_t *received);
void sk_py_hw_reset(void);
uint32_t sk_py_ticks(int microseconds);
void sk_py_delay(uint32_t ms);
#ifdef __cplusplus
}
#endif
