/**
 * @file src/Software/BaReTOS/incl/Tasks/init_task.c
 *
 * @version 1.0.0
 * @date 2026-09-20
 *
 * @details
 * One-shot startup task. It runs at the highest priority so it completes before the service
 * tasks do real work, performs all one-time peripheral initialisation, then deletes itself with
 * vTaskDelete to free its stack. Add further startup steps between the marked lines.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#include "application.h"
#include "adc.h"
#include "pico/stdlib.h"

/**
 * @brief Run startup initialisation, then remove this task.
 *
 * @param[in] parameter Unused FreeRTOS task argument; pass NULL.
 * @return None (void); the task never returns - it deletes itself.
 *
 * @details
 * Powers the ADC and calibrates it against the known voltage at the VREF pin, so later readings
 * are gain-corrected. The final vTaskDelete(NULL) releases the task's TCB and stack back to the
 * heap once initialisation is complete.
 */
void init_task(void *parameter)
{
    (void)parameter;

    /* --- startup initialisation --- */
    adc_hal_init();
    adc_hal_calibrate(PIN_ADC_VREF, ADC_CAL_PIN_MV);
    /* --- end startup initialisation --- */

    vTaskDelete(NULL);
}
