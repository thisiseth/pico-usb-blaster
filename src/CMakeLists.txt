cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pico_usb_blaster)

add_compile_options(-O3)
add_compile_options(-Wall)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of your project

add_executable(pico_usb_blaster
    main.c
    usb_descriptors.c
    blaster.c
)

# pico_generate_pio_header(pico_usb_blaster ${CMAKE_CURRENT_LIST_DIR}/blaster.pio)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(pico_usb_blaster PUBLIC
    .
)

target_link_libraries(pico_usb_blaster PUBLIC 
    pico_stdlib 
    pico_unique_id 
    tinyusb_device 
    tinyusb_board
    pico_platform
)

pico_set_binary_type(pico_usb_blaster copy_to_ram)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(pico_usb_blaster)