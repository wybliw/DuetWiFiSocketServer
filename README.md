Fork of the Duet WiFi Socket Server for LPC/STM32/RRF and updated ESP SDK/Framework
===================================================================================

# Duet WiFi Socket Server for LPC1768 and STM32 versions of RRF
This fork of the Duet WiFi Socket Server is intended for use with the LPC1768 and STM32 versions of RRF.
The LPC version supports a smaller number of active connections than the standard server (to match the LPC
RRF port). It is also built using a more recent ESP8266 toolkit and framework.

# ESP32 version
The ESP32 version of this fork is built with a slightly modified version of the standard esp32 SDK. This
can be found at:
    https://github.com/gloomyandy/esp-idf

# Build Instructions

Checkout and build the ESP8266 framework from here:
    https://github.com/gloomyandy/Arduino.git

Checkout and setup the ESP32 toolkit from here:
    https://github.com/gloomyandy/esp-idf

Note in both cases the repository will be tagged to match the DuetWiFiSocketServer release.

Checkout the this branch/tag on your computer

    git clone https://github.com/gloomyandy/DuetWiFiSocketServer.git
    cd DuetWiFiSocketServer
    git checkout <tag name>

Build the firmware

    ./BuildRelease.sh

To upload the firmware please see your board documentation
