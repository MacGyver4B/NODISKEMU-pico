# NODISKEMU-pico: petSD2+IEC (RP2350B)
#   mkdir build && cd build && cmake .. && make            echte Hardware
#   -DPICO_BOARD=olimex_rp2350_pico2_xl (bzw. _bb48, _bb48r) waehlt ein anderes
#   Board aus boards/; Vorgabe ist olimex_rp2350_pico2_xxl
#   mkdir build-emu && cd build-emu && cmake -DPSD2_EMU=1 .. && make
#                                                           fuer picoem/psd2sim
#   -DPSD2_CONFIG=../configs/config-petSD2 waehlt die NODISKEMU-Konfiguration
#   petSD2 im DIP40-Sockel des petSD+ (LCD, Tasten, DS3231, ATtiny25):
#     cmake -DPICO_BOARD=petsd2_dip40 ..   (Konfiguration config-petSD2-dip40)
cmake_minimum_required(VERSION 3.13)
if(NOT PICO_BOARD)
  set(PICO_BOARD olimex_rp2350_pico2_xxl)
endif()
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(petsd2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(NDE ${CMAKE_CURRENT_LIST_DIR}/../src)
set(TOP ${CMAKE_CURRENT_LIST_DIR}/..)

# autoconf.h aus der NODISKEMU-Konfiguration erzeugen (wie das Makefile)
if(NOT PSD2_CONFIG)
  if(PICO_BOARD STREQUAL "petsd2_dip40")
    set(PSD2_CONFIG ${TOP}/configs/config-petSD2-dip40)
  else()
    set(PSD2_CONFIG ${TOP}/configs/config-petSD2)
  endif()
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
file(STRINGS ${PSD2_CONFIG} PSD2_MD REGEX "^CONFIG_MULTIDRIVE=y")
file(STRINGS ${PSD2_CONFIG} PSD2_HWNAME REGEX "^CONFIG_HARDWARE_NAME=")
string(REPLACE "CONFIG_HARDWARE_NAME=" "" PSD2_HWNAME "${PSD2_HWNAME}")
file(STRINGS ${PSD2_CONFIG} PSD2_LCD REGEX "^CONFIG_ONBOARD_DISPLAY=y")
file(STRINGS ${PSD2_CONFIG} PSD2_DSRTC REGEX "^CONFIG_RTC_DSRTC=y")
add_executable(petsd2
  ${NDE}/buffers.c ${NDE}/fatops.c ${NDE}/fileops.c ${NDE}/main.c
  ${NDE}/errormsg.c ${NDE}/doscmd.c ${NDE}/ff.c ${NDE}/d64ops.c
  ${NDE}/eeprom-conf.c ${NDE}/parser.c ${NDE}/utils.c ${NDE}/led.c
  ${NDE}/diskio.c ${NDE}/timer.c ${NDE}/fastloader.c ${NDE}/sdcard.c
  ${NDE}/ieee.c ${NDE}/iecrelay.c
  ${NDE}/rp2350/arch-timer.c ${NDE}/rp2350/spi.c ${NDE}/rp2350/system.c
  ${NDE}/rp2350/uart.c ${NDE}/rp2350/uart_hw.c ${NDE}/rp2350/crc.c
  ${NDE}/rp2350/ieee-ll.c ${NDE}/rp2350/iec-host.c
  ${NDE}/rp2350/i2c.c ${NDE}/rp2350/i2c_hw.c
)
if(PSD2_MD)
  target_sources(petsd2 PRIVATE ${NDE}/multidrive.c)
endif()
# Bedienung wie beim petSD+ (LCD 20x4, Menue, Tasten an ADC7)
if(PSD2_LCD)
  target_sources(petsd2 PRIVATE ${NDE}/menu.c ${NDE}/rp2350/diagnose.c ${NDE}/rp2350/lcd.c
                 ${NDE}/rp2350/analog-buttons.c)
  if(PSD2_MD)
    target_sources(petsd2 PRIVATE ${NDE}/menu-drives.c)
  endif()
endif()
if(PSD2_DSRTC)
  target_sources(petsd2 PRIVATE ${NDE}/rtc.c ${NDE}/ds1307-3231.c)
endif()
pico_generate_pio_header(petsd2 ${NDE}/rp2350/atn.pio)
pico_generate_pio_header(petsd2 ${NDE}/rp2350/ar6put.pio)
# -iquote: src/time.h usw. duerfen die <time.h> der C-Bibliothek nicht verdecken
target_compile_options(petsd2 PRIVATE
  "SHELL:-iquote ${GEN}" "SHELL:-iquote ${NDE}" "SHELL:-iquote ${NDE}/rp2350"
  "SHELL:-iquote ${TOP}/lcd-i2c-pwm-io")
# Version wie beim AVR aus scripts/Makefile.main (MAJOR.MINOR.PATCHLEVEL)
file(STRINGS ${TOP}/scripts/Makefile.main _mk REGEX "^(MAJOR|MINOR|PATCHLEVEL) = ")
foreach(_l ${_mk})
  string(REGEX MATCH "^([A-Z]+) = ([0-9]+)" _m "${_l}")
  set(NDE_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
set(NDE_VERSION "${NDE_MAJOR}.${NDE_MINOR}.${NDE_PATCHLEVEL}")
message(STATUS "NODISKEMU ${NDE_VERSION}")
target_compile_definitions(petsd2 PRIVATE
  VERSION="${NDE_VERSION}"
  HWNAME="${PSD2_HWNAME}"
  LONGVERSION="-${PSD2_HWNAME}"
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
target_link_libraries(petsd2 pico_stdlib hardware_pio hardware_spi hardware_uart hardware_watchdog
  hardware_i2c hardware_adc)
pico_enable_stdio_uart(petsd2 0)
pico_enable_stdio_usb(petsd2 0)
pico_add_extra_outputs(petsd2)
