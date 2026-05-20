cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensegate_collector)

zephyr_include_directories(include)
zephyr_include_directories(../lib)

target_sources(app PRIVATE
    src/main.c
    src/hal_sim.c
    src/modbus_sim.c
    ../lib/aes_ctr.c
    ../lib/payload_packer.c
    ../lib/rolling_buffer.c
    ../lib/store_forward.c
    src/test_runner.c
)

# Bake current Unix timestamp so machine_timestamp reflects today's date.
# Uses Python (always available in the NCS toolchain) for reliable epoch output.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import time; print(int(time.time()))"
        OUTPUT_VARIABLE SIM_BUILD_TS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(SIM_BUILD_TS 1774138000)
endif()
message(STATUS "SenseGate sim base timestamp: ${SIM_BUILD_TS}")
target_compile_definitions(app PRIVATE SIM_BUILD_TS=${SIM_BUILD_TS}U)

# SIM_TX_INTERVAL_MS: ms between packets.
# Change this value and rebuild to adjust cadence.
# 2000 = 2s for testing, 300000 = 5min for production.
set(_tx_interval 2000)
message(STATUS "SenseGate TX interval: ${_tx_interval} ms")
target_compile_definitions(app PRIVATE SIM_TX_INTERVAL_MS=${_tx_interval}U)