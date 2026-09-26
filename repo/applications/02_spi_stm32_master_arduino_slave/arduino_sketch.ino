/*
 * arduino_sketch.ino — Arduino Uno as an SPI SLAVE, pairing with
 * ../02_spi_stm32_master_arduino_slave/main.c
 *
 * The Arduino's standard SPI library is master-only, so slave mode is
 * done with direct AVR register access here.
 *
 * WIRING: see main.c — SCK=13, MISO=12, MOSI=11, SS=10.
 *
 * BEHAVIOR: every byte the STM32 sends is captured in an interrupt and
 * printed to the Serial Monitor (open at 9600 baud).
 */

#include <SPI.h>

volatile byte receivedByte = 0;
volatile bool dataReady = false;

void setup()
{
    Serial.begin(9600);

    pinMode(MISO, OUTPUT); // required for slave mode on AVR SPI
    SPCR |= _BV(SPE);      // enable SPI in slave mode
    SPCR |= _BV(SPIE);     // enable SPI interrupt

    Serial.println("Arduino SPI slave ready.");
}

ISR(SPI_STC_vect)
{
    receivedByte = SPDR;
    dataReady = true;
}

void loop()
{
    if (dataReady)
    {
        dataReady = false;
        Serial.print("Received from STM32: ");
        Serial.println(receivedByte);
    }
}
