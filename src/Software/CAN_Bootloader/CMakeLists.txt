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
set(PICO_BOARD bladecore_m54c CACHE STRING "Board type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/incl)
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Processor architecture")
include(pico_sdk_import.cmake)
project(CAN_Bootloader C CXX ASM)
pico_sdk_init()
add_executable(CAN_Bootloader
    main.c
    # === USER SOURCES BEGIN === (managed by Create_M54C_RTOS.ps1 -Update)
    incl/board_can.c
    incl/flash_storage.c
    incl/protocol.c
    incl/update.c
    incl/update_test.c

    # === USER SOURCES END ===
)
target_include_directories(CAN_Bootloader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/incl
    # === USER INCLUDE DIRS BEGIN === (managed by Create_M54C_RTOS.ps1 -Update)
    # === USER INCLUDE DIRS END ===
)
# Host fault-injection harness is kept in incl but never compiled into firmware.
set_source_files_properties(incl/update_test.c PROPERTIES HEADER_FILE_ONLY TRUE)
target_link_libraries(CAN_Bootloader pico_stdlib pico_unique_id hardware_spi hardware_watchdog hardware_flash pico_bootrom)
target_compile_options(CAN_Bootloader PRIVATE -Wall -Wextra -Werror)
# SDK startup copies executable code and ordinary constants into SRAM.
pico_set_binary_type(CAN_Bootloader copy_to_ram)
include(firmware_layout.cmake)
configure_firmware_layout(CAN_Bootloader copy_to_ram bootloader)
pico_enable_stdio_uart(CAN_Bootloader 0)
pico_enable_stdio_usb(CAN_Bootloader 0)
pico_set_program_name(CAN_Bootloader "CAN_Bootloader")
pico_set_program_version(CAN_Bootloader "1.0.0")
set_target_properties(CAN_Bootloader PROPERTIES PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350;--abs-block")
pico_add_extra_outputs(CAN_Bootloader)
