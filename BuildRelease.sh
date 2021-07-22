#!/bin/sh

#extract firmware version from header file
VER=`awk 'sub(/.*VERSION_MAIN/,""){print $1}' src/Config.h  | awk 'gsub(/"/, "", $1)'`

OUTPUT=releases/${VER}

mkdir -p releases/${VER}

#Building LPC Firmware
#make BUILD=relbuild clean
#make -j2 BUILD=relbuild HOSTSYS=-DLPCRRF
#if [ -f ./relbuild/DuetWiFiServer.bin ]; then
#	mv ./relbuild/DuetWiFiServer.bin ${OUTPUT}/DuetWiFiServer-lpc.bin
#fi 

#Building LPC Firmware with extended listen
make BUILD=build clean
make -j2 BUILD=build HOSTSYS="-DLPCRRF -DEXTENDED_LISTEN"
if [ -f ./build/DuetWiFiServer.bin ]; then
	mv ./build/DuetWiFiServer.bin ${OUTPUT}/DuetWiFiServer-lpc-el.bin
fi 

#Building STM32F4 Firmware
make BUILD=build clean
make -j2 BUILD=build HOSTSYS=-DSTM32F4
if [ -f ./build/DuetWiFiServer.bin ]; then
	mv ./build/DuetWiFiServer.bin ${OUTPUT}/DuetWiFiServer-stm32f4.bin
fi 

#Building Duet firmware
make BUILD=build clean
make -j2 BUILD=build HOSTSYS=
if [ -f ./relbuild/DuetWiFiServer.bin ]; then
	mv ./build/DuetWiFiServer.bin ${OUTPUT}/DuetWiFiServer-duet.bin
fi 
make BUILD=build clean

# Building esp32 version (ugly!)
cmd << EOF
set MSYSTEM=
%HOMEPATH%\esp\esp-idf\export.bat
.\buildesp32.cmd
EOF
if [ -f ./build/DuetWiFiServer.bin ]; then
	mv ./build/DuetWiFiServer.bin ${OUTPUT}/DuetWiFiServer-esp32.bin
fi

make BUILD=build clean

