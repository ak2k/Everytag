# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(everytag_native_gatt_test)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# Test-specific sources
target_sources(app PRIVATE
    src/main.cpp
    src/native_sim_hardware.cpp
)

# Real firmware sources: GATT service, BLE adv data, config, state machine, NVS
target_sources(app PRIVATE
    ${SRC_DIR}/gatt_glue.c
    ${SRC_DIR}/ble_glue.c
    ${SRC_DIR}/beacon_logic.cpp
    ${SRC_DIR}/accel_data.cpp
    ${SRC_DIR}/beacon_config.cpp
    ${SRC_DIR}/beacon_state.cpp
    ${SRC_DIR}/zephyr_nvs.cpp
)

target_include_directories(app PRIVATE ${SRC_DIR})

# C++ flags matching production
target_compile_options(app PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wall -Wextra -Werror
        -Wno-missing-field-initializers
        -fno-threadsafe-statics -fno-use-cxa-atexit
    >
)
