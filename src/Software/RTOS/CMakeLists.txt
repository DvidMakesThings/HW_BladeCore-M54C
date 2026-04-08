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

# FreeRTOS kernel path
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in FreeRTOS RP2350 Cortex-M33 NTZ port
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)

project(BladeCore_CAN C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# --------------------------------------------------------------------------
# Source files
# --------------------------------------------------------------------------
add_executable(BladeCore_CAN
    main.c

    # Drivers
    drivers/mcp2515.c

    # USB / PCAN protocol
    usb/pcan_protocol.c
    usb/pcan_usb_class.c
    usb/usb_descriptors.c

    # HAL / bridge
    hal/can_bridge.c

    # Bootloader
    bootloader/bootloader.c
)

pico_set_program_name(BladeCore_CAN "BladeCore-M54C CAN")
pico_set_program_version(BladeCore_CAN "1.0.0")

# TinyUSB managed directly -- no pico_stdio_usb
pico_enable_stdio_uart(BladeCore_CAN 0)
pico_enable_stdio_usb(BladeCore_CAN 0)

# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(BladeCore_CAN PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# --------------------------------------------------------------------------
# Link libraries
# --------------------------------------------------------------------------
target_link_libraries(BladeCore_CAN
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_watchdog
    hardware_flash
    hardware_timer
    hardware_resets
    pico_unique_id
    FreeRTOS-Kernel-Heap4
    tinyusb_device
    tinyusb_board
)

# --------------------------------------------------------------------------
# Build outputs
# --------------------------------------------------------------------------
pico_add_extra_outputs(BladeCore_CAN)

