cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensegate_gateway)

zephyr_include_directories(include)
zephyr_include_directories(../lib)

target_sources(app PRIVATE
    src/main.c
    src/hal_lora.c
    src/role_election.c
    ../lib/aes_ctr.c
    ../lib/payload_packer.c
    ../lib/rolling_buffer.c
    ../lib/store_forward.c
)