/*
 SPI.cpp - SPI library for esp8266

 Copyright (c) 2015 Hristo Gochkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "HSPI.h"
#if ESP32
#include "config.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#define DMA_CHAN    2

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
static spi_device_handle_t spi;
static uint32_t currentClock = 0;

HSPIClass::HSPIClass() {
}

void HSPIClass::InitMaster(uint8_t mode, uint32_t clockReg, bool msbFirst)
{
    pinMode(PIN_NUM_MOSI, OUTPUT);
    pinMode(PIN_NUM_CLK, OUTPUT);
    pinMode(PIN_NUM_MISO, INPUT);
    uint32_t dma = DMA_CHAN;
    int clock = SPI_MASTER_FREQ_20M;
    switch(clockReg)
    {
    case spi26MHzDMA:
        dma = DMA_CHAN;
        clock = SPI_MASTER_FREQ_26M;
        break;
    case spi40MHzDMA:
        dma = DMA_CHAN;
        clock = SPI_MASTER_FREQ_40M;
        break;
    case spi20MHzPoll:
        dma = 0;
        clock = SPI_MASTER_FREQ_20M;
        break;
    case spi26MHzPoll:
        dma = 0;
        clock = SPI_MASTER_FREQ_26M;
        break;
    case spi40MHzPoll:
        dma = 0;
        clock = SPI_MASTER_FREQ_40M;
        break;
    case spi20MHzDMA:
    default:
        dma = DMA_CHAN;
        clock = SPI_MASTER_FREQ_20M;
        break;
    }
    debugPrintf("Init SPI mode %d clockReg %x speed %d dma %d\n", mode, (unsigned)clockReg, clock, (int)dma);
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .mosi_io_num=PIN_NUM_MOSI,
        .miso_io_num=PIN_NUM_MISO,
        .sclk_io_num=PIN_NUM_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=4094,
        .flags = SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_IOMUX_PINS,
        .intr_flags = ESP_INTR_FLAG_IRAM
    };
    spi_device_interface_config_t devcfg={
        .mode=1,
        .clock_speed_hz=clock,
        .spics_io_num=-1,
        .flags = SPI_DEVICE_NO_DUMMY,
        .queue_size=4,
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(VSPI_HOST, &buscfg, dma);
    ESP_ERROR_CHECK(ret);
    //Attach to the SPI bus
    ret=spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
    spi_device_acquire_bus(spi, portMAX_DELAY);
    ESP_ERROR_CHECK(ret);
    currentClock = clockReg;
}

void HSPIClass::end() {
    spi_device_release_bus(spi);
    esp_err_t ret = spi_bus_remove_device(spi);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_free(VSPI_HOST);
    ESP_ERROR_CHECK(ret);
}

// Begin a transaction without changing settings
void ICACHE_RAM_ATTR HSPIClass::beginTransaction() {
    //spi_device_acquire_bus(spi, portMAX_DELAY);
}

void ICACHE_RAM_ATTR HSPIClass::endTransaction() {
    //spi_device_release_bus(spi);
}

// clockDiv is NOT the required division ratio, it is the value to write to the SPI1CLK register
void HSPIClass::setClockDivider(uint32_t clockDiv)
{
    if (clockDiv != 0 && clockDiv != currentClock)
    {
        end();
        InitMaster(SPI_MODE1, clockDiv, true);
    }
}

uint32_t HSPIClass::getClockDivider()
{
    return currentClock;
}

void HSPIClass::setDataBits(uint16_t bits)
{
}

uint32_t ICACHE_RAM_ATTR HSPIClass::transfer32(uint32_t data)
{
    esp_err_t ret;
    spi_transaction_t t;
    uint32_t reply;

    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=32;
    t.tx_buffer=&data;               //The data is the cmd itself
    t.rx_buffer=&reply;
    t.rxlength = 32;
    ret=spi_device_polling_transmit(spi, &t);  //Transmit!
    ESP_ERROR_CHECK(ret);
    return reply;
}

uint32_t dummy[maxSpiFileData];
/**
 * @param out uint32_t *
 * @param in  uint32_t *
 * @param size uint32_t
 */
void ICACHE_RAM_ATTR HSPIClass::transferDwords(const uint32_t * out, uint32_t * in, uint32_t size) {
    // Ignore zero length transfers
    if (size == 0) 
        return;
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    // Get transfer size in bits
    t.length=8*4*size;
    if (out)
        t.tx_buffer=out;
    else
        t.tx_buffer=dummy;
    if (in)
    {
        t.rx_buffer=in;
        t.rxlength =8*4*size;
    }
    else
        t.rxlength = 0;
    ret=spi_device_polling_transmit(spi, &t);  //Transmit!
    ESP_ERROR_CHECK(ret);
}

void ICACHE_RAM_ATTR HSPIClass::transferDwords_(const uint32_t * out, uint32_t * in, uint8_t size) {

}

#else
#include "HSPI.h"
#include <cmath>

typedef union {
        uint32_t regValue;
        struct {
                unsigned regL :6;
                unsigned regH :6;
                unsigned regN :6;
                unsigned regPre :13;
                unsigned regEQU :1;
        };
} spiClk_t;

HSPIClass::HSPIClass() {
}

void HSPIClass::InitMaster(uint8_t mode, uint32_t clockReg, bool msbFirst)
{
	pinMode(SCK, SPECIAL);  ///< GPIO14
	pinMode(MISO, SPECIAL); ///< GPIO12
	pinMode(MOSI, SPECIAL); ///< GPIO13

	SPI1C = (msbFirst) ? 0 : SPICWBO | SPICRBO;

	SPI1U = SPIUMOSI | SPIUDUPLEX;
	SPI1U1 = (7 << SPILMOSI) | (7 << SPILMISO);
	SPI1C1 = 0;
	SPI1S = 0;

	const bool CPOL = (mode & 0x02); ///< CPOL (Clock Polarity)
	const bool CPHA = (mode & 0x01); ///< CPHA (Clock Phase)

	/*
	 SPI_MODE0 0x00 - CPOL: 0  CPHA: 0
	 SPI_MODE1 0x01 - CPOL: 0  CPHA: 1
	 SPI_MODE2 0x10 - CPOL: 1  CPHA: 0
	 SPI_MODE3 0x11 - CPOL: 1  CPHA: 1
	 */

	if (CPHA)
	{
		SPI1U |= (SPIUSME | SPIUSSE);
	}
	else
	{
		SPI1U &= ~(SPIUSME | SPIUSSE);
	}

	if (CPOL)
	{
		SPI1P |= 1ul << 29;
	}
	else
	{
		SPI1P &= ~(1ul << 29);
	}

	setClockDivider(clockReg);
}

void HSPIClass::end() {
    pinMode(SCK, INPUT);
    pinMode(MISO, INPUT);
    pinMode(MOSI, INPUT);
}

// Begin a transaction without changing settings
void ICACHE_RAM_ATTR HSPIClass::beginTransaction() {
    while(SPI1CMD & SPIBUSY) {}
}

void ICACHE_RAM_ATTR HSPIClass::endTransaction() {
}

// clockDiv is NOT the required division ratio, it is the value to write to the SPI1CLK register
void HSPIClass::setClockDivider(uint32_t clockDiv)
{
	// From the datasheet:
	// bits 0-5  spi_clkcnt_L = (divider - 1)
	// bits 6-11 spi_clkcnt_H = floor(divider/2) - 1
	// bits 12-17 spi_clkcnt_N = divider - 1
	// bits 18-30 spi_clkdiv_pre = prescaler - 1
	// bit 31 = set to run at sysclock speed
	// We assume the divider is >1 but <64 so we need only worry about the low bits

    if (clockDiv == 0x80000000)
    {
        GPMUX |= (1 << 9); // Set bit 9 if sysclock required
    }
    else
    {
        GPMUX &= ~(1 << 9);
    }
    SPI1CLK = clockDiv;
}

void HSPIClass::setDataBits(uint16_t bits)
{
    const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
    bits--;
    SPI1U1 = ((SPI1U1 & mask) | ((bits << SPILMOSI) | (bits << SPILMISO)));
}

uint32_t ICACHE_RAM_ATTR HSPIClass::transfer32(uint32_t data)
{
    while(SPI1CMD & SPIBUSY) {}
    // Set to 32Bits transfer
    setDataBits(32);
	// LSBFIRST Byte first
	SPI1W0 = data;
	SPI1CMD |= SPIBUSY;
    while(SPI1CMD & SPIBUSY) {}
    return SPI1W0;
}

/**
 * @param out uint32_t *
 * @param in  uint32_t *
 * @param size uint32_t
 */
void ICACHE_RAM_ATTR HSPIClass::transferDwords(const uint32_t * out, uint32_t * in, uint32_t size) {
    while(size != 0) {
        if (size > 16) {
            transferDwords_(out, in, 16);
            size -= 16;
            if(out) out += 16;
            if(in) in += 16;
        } else {
            transferDwords_(out, in, size);
            size = 0;
        }
    }
}

void ICACHE_RAM_ATTR HSPIClass::transferDwords_(const uint32_t * out, uint32_t * in, uint8_t size) {
    while(SPI1CMD & SPIBUSY) {}

    // Set in/out Bits to transfer
    setDataBits(size * 32);

    volatile uint32_t * fifoPtr = &SPI1W0;
    uint8_t dataSize = size;

    if (out != nullptr) {
        while(dataSize != 0) {
            *fifoPtr++ = *out++;
            dataSize--;
        }
    } else {
        // no out data, so fill with dummy data
        while(dataSize != 0) {
            *fifoPtr++ = 0xFFFFFFFF;
            dataSize--;
        }
    }

    SPI1CMD |= SPIBUSY;
    while(SPI1CMD & SPIBUSY) {}

    if (in != nullptr) {
        volatile uint32_t * fifoPtrRd = &SPI1W0;
        while(size != 0) {
            *in++ = *fifoPtrRd++;
            size--;
        }
    }
}

// End
#endif