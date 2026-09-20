/**
 * @file src/Software/BaReTOS/FreeRTOSConfig.h
 *
 * @version 1.0.0
 * @date 2026-09-18
 *
 * @details
 * Owns the FreeRTOS kernel configuration for the RP2354B Cortex-M33 secure port: interrupt
 * vector mapping, scheduler, synchronization, memory, hooks, software timers, Cortex-M33 required
 * defines, Pico SDK interop, the assertion handler, and the optional API includes. Application
 * task priorities, board pins, and protocol macros are defined separately in CONFIG.h. The
 * assertion declaration is guarded from the port assembler.
 *
 * @project BaReTOS - BladeCore-M54C firmware
 * @github https://github.com/DvidMakesThings/HW_BladeCore-M54C
 */
#pragma once
/* Board and project macros (PICO_* SDK config, pins, protocol) must be visible to the
 * FreeRTOS and SDK translation units that include only this file. */
#include "CONFIG.h"

/* ISR handlers -- use Pico SDK ISR names */
/** @brief Map the RTOS supervisor call handler to the SDK vector name. */
#define vPortSVCHandler isr_svcall
/** @brief Map the RTOS context-switch handler to the SDK vector name. */
#define xPortPendSVHandler isr_pendsv
/** @brief Map the scheduler tick handler to the SDK vector name. */
#define xPortSysTickHandler isr_systick

/* -------------------------------------------------------------------------- */
/*  Scheduler                                                                 */
/* -------------------------------------------------------------------------- */
/** @brief Allow a higher-priority ready task to preempt the running task. */
#define configUSE_PREEMPTION 1
/** @brief Share CPU ticks between ready tasks at equal priority. */
#define configUSE_TIME_SLICING 1
/** @brief Use the portable task selection implementation. */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
/** @brief Keep the periodic tick enabled during idle. */
#define configUSE_TICKLESS_IDLE 0
/** @brief Processor frequency used to configure the scheduler tick. */
#define configCPU_CLOCK_HZ 150000000UL
/** @brief One scheduler tick per millisecond. */
#define configTICK_RATE_HZ ((TickType_t)1000)
/** @brief Number of task priority levels, starting at zero. */
#define configMAX_PRIORITIES 16
/** @brief Idle task stack allocation in stack words. */
#define configMINIMAL_STACK_SIZE ((configSTACK_DEPTH_TYPE)256)
/** @brief Use a 32-bit tick counter. */
#define configUSE_16_BIT_TICKS 0
/** @brief Let the idle task yield to the priority-zero heartbeat. */
#define configIDLE_SHOULD_YIELD 1
/** @brief Task name storage length including its terminator. */
#define configMAX_TASK_NAME_LEN 16
/** @brief Independent notification slots allocated per task. */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3

/* -------------------------------------------------------------------------- */
/*  Synchronization                                                           */
/* -------------------------------------------------------------------------- */
/** @brief Enable mutexes with priority inheritance. */
#define configUSE_MUTEXES 1
/** @brief Exclude recursive mutex support from this template. */
#define configUSE_RECURSIVE_MUTEXES 0
/** @brief Enable counting semaphores for future application modules. */
#define configUSE_COUNTING_SEMAPHORES 1
/** @brief Enable direct notification from the CAN interrupt. */
#define configUSE_TASK_NOTIFICATIONS 1
/** @brief Exclude queue-set support. */
#define configUSE_QUEUE_SETS 0
/** @brief Number of queues that may be named for debugging. */
#define configQUEUE_REGISTRY_SIZE 8
/** @brief Avoid allocating a separate Newlib reentrancy structure per task. */
#define configUSE_NEWLIB_REENTRANT 0
/** @brief Require the current FreeRTOS API names. */
#define configENABLE_BACKWARD_COMPATIBILITY 0
/** @brief Per-task pointer slots, including Pico SDK interoperability. */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5

/* -------------------------------------------------------------------------- */
/*  System types                                                              */
/* -------------------------------------------------------------------------- */
/** @brief Type used to express task stack depths. */
#define configSTACK_DEPTH_TYPE uint32_t
/** @brief Type used to encode message buffer payload lengths. */
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t

/* -------------------------------------------------------------------------- */
/*  Memory allocation                                                         */
/* -------------------------------------------------------------------------- */
/** @brief Use the generated dynamic allocation model. */
#define configSUPPORT_STATIC_ALLOCATION 0
/** @brief Enable heap-backed task and queue allocation. */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
/** @brief Let heap_4 own its heap storage. */
#define configAPPLICATION_ALLOCATED_HEAP 0
/** @brief Bytes reserved for the FreeRTOS heap_4 allocator. */
#define configTOTAL_HEAP_SIZE (64 * 1024)

/* -------------------------------------------------------------------------- */
/*  Hooks                                                                     */
/* -------------------------------------------------------------------------- */
/** @brief No application callback is installed in the idle task. */
#define configUSE_IDLE_HOOK 0
/** @brief No application callback is installed in the tick interrupt. */
#define configUSE_TICK_HOOK 0
/** @brief Check task stack bounds and the stack sentinel pattern. */
#define configCHECK_FOR_STACK_OVERFLOW 2
/** @brief Call the reset hook if an RTOS allocation fails. */
#define configUSE_MALLOC_FAILED_HOOK 1
/** @brief No timer-daemon startup callback is installed. */
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* -------------------------------------------------------------------------- */
/*  Runtime stats / trace                                                     */
/* -------------------------------------------------------------------------- */
/** @brief Disable runtime profiling counters. */
#define configGENERATE_RUN_TIME_STATS 0
/** @brief Disable optional scheduler trace metadata. */
#define configUSE_TRACE_FACILITY 0
/** @brief Exclude formatted scheduler statistics helpers. */
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* -------------------------------------------------------------------------- */
/*  Co-routines (disabled)                                                    */
/* -------------------------------------------------------------------------- */
/** @brief Disable legacy cooperative co-routines. */
#define configUSE_CO_ROUTINES 0
/** @brief Placeholder priority count while co-routines are disabled. */
#define configMAX_CO_ROUTINE_PRIORITIES 1

/* -------------------------------------------------------------------------- */
/*  Software timers                                                           */
/* -------------------------------------------------------------------------- */
/** @brief Retain the generated software timer service. */
#define configUSE_TIMERS 1
/** @brief Priority of the generated software timer service task. */
#define configTIMER_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
/** @brief Number of pending software timer commands. */
#define configTIMER_QUEUE_LENGTH 10
/** @brief Timer service task stack allocation in words. */
#define configTIMER_TASK_STACK_DEPTH 512

/* -------------------------------------------------------------------------- */
/*  Cortex-M33 (RP2350/RP2354) required defines                               */
/* -------------------------------------------------------------------------- */
/** @brief Save floating-point context when tasks use the Cortex-M33 FPU. */
#define configENABLE_FPU 1
/** @brief Run the generated port without MPU task regions. */
#define configENABLE_MPU 0
/** @brief Do not allocate non-secure task contexts. */
#define configENABLE_TRUSTZONE 0
/** @brief Run the scheduler entirely in secure state. */
#define configRUN_FREERTOS_SECURE_ONLY 1
/** @brief Schedule application tasks only on core zero. */
#define configNUMBER_OF_CORES 1

/** @brief Implemented Cortex-M33 interrupt priority bits. */
#define configPRIO_BITS 4
/** @brief Highest encoded IRQ priority permitted to call FromISR APIs. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 16

/* -------------------------------------------------------------------------- */
/*  RP2350 / Pico SDK interop                                                 */
/* -------------------------------------------------------------------------- */
/** @brief Integrate SDK synchronisation with FreeRTOS task scheduling. */
#define configSUPPORT_PICO_SYNC_INTEROP 1
/** @brief Integrate SDK timed waits with FreeRTOS task scheduling. */
#define configSUPPORT_PICO_TIME_INTEROP 1

/* -------------------------------------------------------------------------- */
/*  Assert                                                                    */
/* -------------------------------------------------------------------------- */
#ifndef __ASSEMBLER__
/**
 * @brief Reset following a failed runtime assertion.
 *
 * @param[in] file Source filename reporting the failure.
 * @param[in] line Source line reporting the failure.
 * @return None (void); this function does not return.
 *
 * @details
 * Disables task interrupts and requests a short watchdog reset instead of continuing with a
 * potentially corrupted scheduler state. The source location is supplied by configASSERT and
 * remains available to a debugger at the call site. The function then stays in a tight loop
 * until reset; it must never return to the failed operation.
 */
void vAssertCalled(const char *file, int line);
#endif
/** @brief Reset through vAssertCalled when the supplied condition is false. */
#define configASSERT(x)                                                                            \
    if ((x) == 0)                                                                                  \
    {                                                                                              \
        vAssertCalled(__FILE__, __LINE__);                                                         \
    }

/* -------------------------------------------------------------------------- */
/*  Optional API includes                                                     */
/* -------------------------------------------------------------------------- */
/** @brief Include the vTaskPrioritySet API in the generated RTOS build. */
#define INCLUDE_vTaskPrioritySet 1
/** @brief Include the uxTaskPriorityGet API in the generated RTOS build. */
#define INCLUDE_uxTaskPriorityGet 1
/** @brief Include the vTaskDelete API in the generated RTOS build. */
#define INCLUDE_vTaskDelete 1
/** @brief Include the vTaskSuspend API in the generated RTOS build. */
#define INCLUDE_vTaskSuspend 1
/** @brief Include the vTaskDelayUntil API in the generated RTOS build. */
#define INCLUDE_vTaskDelayUntil 1
/** @brief Include the vTaskDelay API in the generated RTOS build. */
#define INCLUDE_vTaskDelay 1
/** @brief Include the xTimerPendFunctionCall API in the generated RTOS build. */
#define INCLUDE_xTimerPendFunctionCall 1
/** @brief Include the xTaskGetSchedulerState API in the generated RTOS build. */
#define INCLUDE_xTaskGetSchedulerState 1
/** @brief Include the xTaskGetCurrentTaskHandle API in the generated RTOS build. */
#define INCLUDE_xTaskGetCurrentTaskHandle 1
/** @brief Include the uxTaskGetStackHighWaterMark API in the generated RTOS build. */
#define INCLUDE_uxTaskGetStackHighWaterMark 1
/** @brief Include the uxTaskGetStackHighWaterMark2 API in the generated RTOS build. */
#define INCLUDE_uxTaskGetStackHighWaterMark2 1
/** @brief Include the xTaskGetIdleTaskHandle API in the generated RTOS build. */
#define INCLUDE_xTaskGetIdleTaskHandle 1
/** @brief Include the eTaskGetState API in the generated RTOS build. */
#define INCLUDE_eTaskGetState 1
/** @brief Include the xTaskAbortDelay API in the generated RTOS build. */
#define INCLUDE_xTaskAbortDelay 1
/** @brief Include the xTaskGetHandle API in the generated RTOS build. */
#define INCLUDE_xTaskGetHandle 1
/** @brief Include the xTaskResumeFromISR API in the generated RTOS build. */
#define INCLUDE_xTaskResumeFromISR 1
/** @brief Include the xQueueGetMutexHolder API in the generated RTOS build. */
#define INCLUDE_xQueueGetMutexHolder 1
/** @brief Include the xResumeFromISR API in the generated RTOS build. */
#define INCLUDE_xResumeFromISR 1
