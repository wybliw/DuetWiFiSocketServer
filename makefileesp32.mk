OS ?=Windows_NT
ESP_ROOT?=../esp32
CHIP?=esp32
BUILD_DIR?=build
SKETCH?=src/SocketServer.cpp
BUILD_EXTRA_FLAGS?=-DDEBUG
include ../makeEspArduino/makeEspArduino.mk