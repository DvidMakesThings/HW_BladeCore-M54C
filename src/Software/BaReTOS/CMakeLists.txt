cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# BladeCore-M54C uses the RP2354B (RP2350B package, 48 GPIOs, QFN-80). The
# stock "pico2" board header targets RP2350A (30 GPIOs); override before
# pico_sdk_init() so GPIO34-47 (CAN control, I2C0, heartbeat LED) pass the
# SDK's GPIO bounds checks.
add_compile_definitions(PICO_RP2350A=0)

set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Processor architecture")

# FreeRTOS kernel path
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in FreeRTOS RP2350 Cortex-M33 NTZ port
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)

project(BaReTOS C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(BaReTOS
    main.c
    # === USER SOURCES BEGIN === (managed by Create_M54C_RTOS.ps1 -Update)
    incl/Tasks/can_task.c
    incl/can_router/can_router.c
    incl/hal/adc/adc.c
    incl/Tasks/init_task.c
    incl/Tasks/example_task.c
    incl/Tasks/heartbeat.c
    incl/board_can/board_can.c
    incl/protocol/protocol.c
    # === USER SOURCES END ===
)

pico_set_program_name(BaReTOS "BaReTOS")
pico_set_program_version(BaReTOS "1.0.0")

pico_enable_stdio_uart(BaReTOS 0)
pico_enable_stdio_usb(BaReTOS 0)

target_include_directories(BaReTOS PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/incl
    # === USER INCLUDE DIRS BEGIN === (managed by Create_M54C_RTOS.ps1 -Update)
    ${CMAKE_CURRENT_LIST_DIR}/incl/Tasks
    ${CMAKE_CURRENT_LIST_DIR}/incl/can_router
    ${CMAKE_CURRENT_LIST_DIR}/incl/hal/adc
    ${CMAKE_CURRENT_LIST_DIR}/incl/board_can
    ${CMAKE_CURRENT_LIST_DIR}/incl/protocol
    # === USER INCLUDE DIRS END ===
)

target_link_libraries(BaReTOS
    pico_stdlib
    hardware_spi
    pico_unique_id
    hardware_gpio
    hardware_watchdog
    hardware_clocks
    hardware_timer
    hardware_adc
    FreeRTOS-Kernel-Heap4
)

include(${CMAKE_CURRENT_LIST_DIR}/firmware_layout.cmake)
configure_firmware_layout(BaReTOS default application)
target_compile_options(BaReTOS PRIVATE -Wall -Wextra -Werror)
set_target_properties(BaReTOS PROPERTIES PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350;--abs-block")
pico_add_extra_outputs(BaReTOS)