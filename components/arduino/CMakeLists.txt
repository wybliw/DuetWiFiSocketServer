set(CORE_SRCS
  cores/esp32/base64.cpp
  cores/esp32/cbuf.cpp
  cores/esp32/esp32-hal-adc.c
  cores/esp32/esp32-hal-bt.c
  cores/esp32/esp32-hal-cpu.c
  cores/esp32/esp32-hal-dac.c
  cores/esp32/esp32-hal-gpio.c
  cores/esp32/esp32-hal-i2c.c
  cores/esp32/esp32-hal-ledc.c
  cores/esp32/esp32-hal-matrix.c
  cores/esp32/esp32-hal-misc.c
  cores/esp32/esp32-hal-psram.c
  cores/esp32/esp32-hal-sigmadelta.c
  cores/esp32/esp32-hal-spi.c
  cores/esp32/esp32-hal-time.c
  cores/esp32/esp32-hal-timer.c
  cores/esp32/esp32-hal-tinyusb.c
  cores/esp32/esp32-hal-touch.c
  cores/esp32/esp32-hal-uart.c
  cores/esp32/esp32-hal-rmt.c
  cores/esp32/Esp.cpp
  cores/esp32/FunctionalInterrupt.cpp
  cores/esp32/HardwareSerial.cpp
  cores/esp32/IPAddress.cpp
  cores/esp32/IPv6Address.cpp
  cores/esp32/libb64/cdecode.c
  cores/esp32/libb64/cencode.c
  cores/esp32/MD5Builder.cpp
  cores/esp32/Print.cpp
  cores/esp32/stdlib_noniso.c
  cores/esp32/Stream.cpp
  cores/esp32/StreamString.cpp
  cores/esp32/USB.cpp
  cores/esp32/USBCDC.cpp
  cores/esp32/wiring_pulse.c
  cores/esp32/wiring_shift.c
  cores/esp32/WMath.cpp
  cores/esp32/WString.cpp
  )

set(LIBRARY_SRCS
  libraries/AsyncUDP/src
  libraries/DNSServer/src/DNSServer.cpp
  libraries/EEPROM/src/EEPROM.cpp
  libraries/ESPmDNS/src/ESPmDNS.cpp
  libraries/FS/src/vfs_api.cpp
  libraries/NetBIOS/src/NetBIOS.cpp
  libraries/SPI/src/SPI.cpp
  libraries/WiFi/src/WiFiAP.cpp
  libraries/WiFi/src/WiFi.cpp
  libraries/WiFi/src/WiFiGeneric.cpp
  libraries/WiFi/src/WiFiUdp.cpp
  libraries/WiFi/src/WiFiSTA.cpp
  libraries/WiFi/src/WiFiScan.cpp
  libraries/Wire/src/Wire.cpp
  )


set(includedirs
  variants/esp32/
  cores/esp32/
  libraries/AsyncUDP/src
  libraries/DNSServer/src
  libraries/EEPROM/src
  libraries/ESP32/src
  libraries/ESPmDNS/src
  libraries/NetBIOS/src
  libraries/SPI/src
  libraries/WiFi/src
  )

set(srcs ${CORE_SRCS} ${LIBRARY_SRCS} ${BLE_SRCS})
set(priv_includes cores/esp32/libb64)
set(requires spi_flash mdns esp_adc_cal)
set(priv_requires nvs_flash bootloader_support main)

idf_component_register(INCLUDE_DIRS ${includedirs} PRIV_INCLUDE_DIRS ${priv_includes} SRCS ${srcs} REQUIRES ${requires} PRIV_REQUIRES ${priv_requires})

if(IDF_TARGET STREQUAL "esp32")
target_compile_options(${COMPONENT_TARGET} PUBLIC -DARDUINO=10812 -DARDUINO_ESP32_DEV -DARDUINO_ARCH_ESP32 -DARDUINO_BOARD="ESP32_DEV" -DARDUINO_VARIANT="esp32" -DESP32)
endif()
if(IDF_TARGET STREQUAL "esp32s2")
target_compile_options(${COMPONENT_TARGET} PUBLIC -DARDUINO=10812 -DARDUINO_ESP32S2_DEV -DARDUINO_ARCH_ESP32 -DARDUINO_BOARD="ESP32S2_DEV" -DARDUINO_VARIANT="esp32s2" -DESP32)
endif()
