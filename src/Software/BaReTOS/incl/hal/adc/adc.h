/**
 * @file src/Software/BaReTOS/incl/hal/adc/adc.h
 *
 * @version 1.0.0
 * @date 2026-09-20
 *
 * @details
 * Thin HAL over the RP2354B on-chip ADC. It exposes only chip-level operations: power the ADC
 * and read the voltage present on an analog GPIO. It knows nothing about CAN or the board's
 * sense dividers; callers apply any board scaling.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
#include <stdint.h>

/**
 * @brief Power up the on-chip ADC.
 *
 * @par Parameters
 * None (void).
 * @return None (void).
 */
void adc_hal_init(void);

/**
 * @brief Read the voltage on an analog GPIO.
 *
 * @param[in] gpio Analog-capable GPIO (GPIO40..47 on the RP2354B).
 * @return Voltage at the pin in millivolts.
 *
 * @details
 * Configures the pin for analog input and selects its channel, discards a few settling
 * conversions, then averages several and scales the result to millivolts using the calibrated
 * reference.
 */
uint16_t adc_hal_read_mv(unsigned gpio);

/**
 * @brief Single-point gain calibration against a known pin voltage.
 *
 * @param[in] gpio Analog GPIO carrying the calibration reference.
 * @param[in] known_pin_mv True voltage at that pin, in millivolts.
 * @return None (void).
 *
 * @details
 * Samples the reference pin with the nominal reference, then adjusts the effective full-scale so
 * the reading matches known_pin_mv. The correction applies to every later adc_hal_read_mv call,
 * cancelling the ADC's gain and reference error. Call once after adc_hal_init.
 */
void adc_hal_calibrate(unsigned gpio, uint16_t known_pin_mv);
