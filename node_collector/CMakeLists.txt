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
)