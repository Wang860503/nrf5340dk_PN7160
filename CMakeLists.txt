# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

FILE(GLOB app_sources 
    src/*.c 
    src/cmd/*.c
    src/pn7160/*.c
    src/pn7160/TML/src/*.c
    src/pn7160/NfcLibrary_NCI2.0/NxpNci20/src/*.c
    src/pn7160/NfcLibrary_NCI2.0/NdefLibrary/src/*.c
    src/peripheral_uart/*.c
    src/remote_shell/*.c
)

target_include_directories(app PRIVATE
    src/pn7160
    src/pn7160/inc
    src/pn7160/TML/inc
    src/pn7160/board/inc
    src/pn7160/NfcLibrary_NCI2.0/inc
    src/pn7160/NfcLibrary_NCI2.0/NxpNci20/inc
    src/pn7160/NfcLibrary_NCI2.0/NdefLibrary/inc
    src/peripheral_uart/
    src/remote_shell/
)

target_sources(app PRIVATE 
    ${app_sources}
)

target_compile_definitions(app PRIVATE 
    #BUZZER_SUPPORT
    NCI_DEBUG      # pn7160_LOG
    RW_SUPPORT     # pn7160 
)