# NODISKEMU-pico: petSD2+IEC (RP2350B, Olimex PICO2-XXL)
#   mkdir build && cd build && cmake .. && make            echte Hardware
#   mkdir build-emu && cd build-emu && cmake -DPSD2_EMU=1 .. && make
#                                                           fuer picoem/psd2sim
#   -DPSD2_CONFIG=../configs/config-petSD2 waehlt die NODISKEMU-Konfiguration
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD olimex_pico2_xxl)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(petsd2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(NDE ${CMAKE_CURRENT_LIST_DIR}/../src)
set(TOP ${CMAKE_CURRENT_LIST_DIR}/..)

# autoconf.h aus der NODISKEMU-Konfiguration erzeugen (wie das Makefile)
if(NOT PSD2_CONFIG)
  set(PSD2_CONFIG ${TOP}/configs/config-petSD2)
endif()
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
execute_process(
  COMMAND perl ${TOP}/scripts/configparser.pl --genfiles
          --makeinc ${GEN}/make.inc --header ${GEN}/autoconf.h ${PSD2_CONFIG}
  RESULT_VARIABLE cfg_rc)
if(NOT cfg_rc EQUAL 0)
  message(FATAL_ERROR "configparser.pl fehlgeschlagen fuer ${PSD2_CONFIG}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PSD2_CONFIG})
add_executable(petsd2
  ${NDE}/buffers.c ${NDE}/fatops.c ${NDE}/fileops.c ${NDE}/main.c
  ${NDE}/errormsg.c ${NDE}/doscmd.c ${NDE}/ff.c ${NDE}/d64ops.c
  ${NDE}/eeprom-conf.c ${NDE}/parser.c ${NDE}/utils.c ${NDE}/led.c
  ${NDE}/diskio.c ${NDE}/timer.c ${NDE}/fastloader.c ${NDE}/sdcard.c
  ${NDE}/ieee.c ${NDE}/iecrelay.c
  ${NDE}/rp2350/arch-timer.c ${NDE}/rp2350/spi.c ${NDE}/rp2350/system.c
  ${NDE}/rp2350/uart.c ${NDE}/rp2350/uart_hw.c ${NDE}/rp2350/crc.c
  ${NDE}/rp2350/ieee-ll.c ${NDE}/rp2350/iec-host.c
)
pico_generate_pio_header(petsd2 ${NDE}/rp2350/atn.pio)
pico_generate_pio_header(petsd2 ${NDE}/rp2350/ar6put.pio)
# -iquote: src/time.h usw. duerfen die <time.h> der C-Bibliothek nicht verdecken
target_compile_options(petsd2 PRIVATE
  "SHELL:-iquote ${GEN}" "SHELL:-iquote ${NDE}" "SHELL:-iquote ${NDE}/rp2350")
target_compile_definitions(petsd2 PRIVATE
  VERSION="0.0-petSD2"
  LONGVERSION=""
  PICO_USE_GPIO_COPROCESSOR=0
)
target_compile_options(petsd2 PRIVATE -Wall -Wno-unused-function $<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign> -Wno-char-subscripts)
if(PSD2_EMU)
  target_compile_definitions(petsd2 PRIVATE
    PSD2_EMU=1
    PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET=1
    PICO_RUNTIME_SKIP_INIT_PER_CORE_BOOTROM_RESET=1
    PICO_BOOTROM_LOCKING_ENABLED=0
    PICO_USE_SW_SPIN_LOCKS=0
    PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
  )
endif()
target_link_libraries(petsd2 pico_stdlib hardware_pio hardware_spi hardware_uart hardware_watchdog)
pico_enable_stdio_uart(petsd2 0)
pico_enable_stdio_usb(petsd2 0)
pico_add_extra_outputs(petsd2)
