# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tictactoe C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(tictactoe tictactoe.c )

# Criação explícita do diretório 'generated' para armazenar os arquivos gerados
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

# Gera o cabeçalho a partir do arquivo ws2812.pio e coloca em /generated
pico_generate_pio_header(tictactoe ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_set_program_name(tictactoe "tictactoe")
pico_set_program_version(tictactoe "0.1")


target_sources(tictactoe PRIVATE tictactoe.c ssd1306/ssd1306.c)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(tictactoe 0)
pico_enable_stdio_usb(tictactoe 1)

# Add the standard library to the build
target_link_libraries(tictactoe
    pico_stdlib
    hardware_pio
    hardware_adc
    hardware_pwm
    hardware_i2c
    pico_multicore
)

# Add the standard include files to the build
target_include_directories(tictactoe PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(tictactoe 
        
        )

pico_add_extra_outputs(tictactoe)

