# Shared library — used by collector and gateway
zephyr_library()
zephyr_library_sources(
    aes_ctr.c
    payload_packer.c
    rolling_buffer.c
    store_forward.c
)
zephyr_library_include_directories(.)