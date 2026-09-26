/*
 * arduino_sketch.ino — Arduino Uno as an I2C SLAVE at address 0x08,
 * pairing with ../03_i2c_stm32_arduino_slave/main.c
 *
 * WIRING: see main.c — SCL=A5, SDA=A4, common GND, pull-ups to 3.3V.
 *
 * BEHAVIOR: prints every byte received from the STM32 to the Serial
 * Monitor (open at 9600 baud).
 */

#include <Wire.h>

void receiveEvent(int numBytes)
{
    while (Wire.available())
    {
        byte b = Wire.read();
        Serial.print("Received from STM32: ");
        Serial.println(b);
    }
}

void setup()
{
    Serial.begin(9600);
    Wire.begin(0x08); // slave address, must match ARDUINO_I2C_ADDR in main.c
    Wire.onReceive(receiveEvent);
    Serial.println("Arduino I2C slave ready.");
}

void loop()
{
    // nothing needed here — receiveEvent() fires on incoming data
}
