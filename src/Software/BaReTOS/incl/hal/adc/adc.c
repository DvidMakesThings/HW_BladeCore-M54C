/**
 * @file src/Software/BaReTOS/incl/hal/adc/adc.c
 *
 * @version 1.0.0
 * @date 2026-09-20
 *
 * @details
 * Chip-level ADC HAL for the RP2354B. Wraps the Pico SDK ADC calls so the rest of the firmware
 * reads a pin voltage in millivolts without touching hardware registers. Board-specific scaling
 * (for example sense dividers) is the caller's job.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "adc.h"
#include "hardware/adc.h"

#define ADC_GPIO_BASE 40u   /**< RP2354B maps ADC channel N to GPIO(40 + N). */
#define ADC_MAX_COUNT 4096u /**< 12-bit conversion range. */
#define ADC_REF_MV 3000u    /**< ADC full-scale reference: on-board 3.00 V precision ref. */
#define ADC_DUMMY_NUM 2u    /**< First conversions after a channel switch are discarded. */
#define AVG_ADC_NUM 4u      /**< Conversions averaged per reading. */

/** @brief Effective full-scale reference; adjusted by adc_hal_calibrate. */
static uint16_t adc_ref_mv = ADC_REF_MV;

/**
 * @brief Power up the on-chip ADC.
 *
 * @par Parameters
 * None (void).
 * @return None (void).
 *
 * @details
 * Enables the ADC block through the Pico SDK. Call once before any read; the init task runs it
 * at startup. Per-channel pin setup and selection happen inside each read.
 */
void adc_hal_init(void)
{
    adc_init();
}

/**
 * @brief Read the voltage on an analog GPIO.
 *
 * @param[in] gpio Analog-capable GPIO (GPIO40..47 on the RP2354B).
 * @return Voltage at the pin in millivolts.
 *
 * @details
 * Configures the pin for analog input and selects its ADC channel, discards ADC_DUMMY_NUM
 * settling conversions, then averages AVG_ADC_NUM conversions. The averaged count is scaled to
 * millivolts using the calibrated reference maintained by adc_hal_calibrate.
 */
uint16_t adc_hal_read_mv(unsigned gpio)
{
    adc_gpio_init(gpio);
    adc_select_input(gpio - ADC_GPIO_BASE);
    for (unsigned i = 0; i < ADC_DUMMY_NUM; ++i)
    {
        (void)adc_read();
    }
    uint32_t sum = 0; /**< Accumulated conversions for averaging. */
    for (unsigned i = 0; i < AVG_ADC_NUM; ++i)
    {
        sum += adc_read();
    }
    const uint16_t raw = (uint16_t)(sum / AVG_ADC_NUM); /**< Averaged 12-bit result. */
    return (uint16_t)((uint32_t)raw * adc_ref_mv / ADC_MAX_COUNT);
}

/**
 * @brief Single-point gain calibration against a known pin voltage.
 *
 * @param[in] gpio Analog GPIO carrying the calibration reference.
 * @param[in] known_pin_mv True voltage at that pin, in millivolts.
 * @return None (void).
 *
 * @details
 * Reads the reference pin with the nominal reference, then sets the effective full-scale so the
 * reading equals known_pin_mv. Every later adc_hal_read_mv uses that corrected reference, which
 * cancels the ADC's gain and reference error across all channels. A zero reading is ignored so a
 * disconnected reference cannot divide by zero.
 */
void adc_hal_calibrate(unsigned gpio, uint16_t known_pin_mv)
{
    adc_ref_mv = ADC_REF_MV; /* Measure against the nominal reference first. */
    const uint16_t measured = adc_hal_read_mv(gpio);
    if (measured != 0)
    {
        adc_ref_mv = (uint16_t)((uint32_t)ADC_REF_MV * known_pin_mv / measured);
    }
}
