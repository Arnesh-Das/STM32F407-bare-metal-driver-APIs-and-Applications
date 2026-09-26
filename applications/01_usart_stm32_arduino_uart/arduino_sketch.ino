/*
 * arduino_sketch.ino — pairs with ../01_usart_stm32_arduino_uart/main.c
 *
 * WIRING: see main.c — you need a voltage divider (or level shifter) on
 * the Arduino TX -> STM32 RX line if using a 5V Arduino (Uno/Nano/Mega).
 *
 * Uses SoftwareSerial on pins 10/11 to talk to the STM32, keeping the
 * Arduino's main hardware Serial (USB) free for the Serial Monitor.
 *
 *   Arduino pin 10 (RX, SoftwareSerial) <-- STM32 PA2 (USART2 TX)
 *   Arduino pin 11 (TX, SoftwareSerial) --> [divider] --> STM32 PA3 (USART2 RX)
 *   Arduino GND                          <-> STM32 GND
 */

#include <SoftwareSerial.h>

SoftwareSerial stmSerial(10, 11); // RX, TX

void setup()
{
    Serial.begin(9600);
    stmSerial.begin(9600);
    Serial.println("Arduino ready. Listening for STM32...");
}

void loop()
{
    if (stmSerial.available())
    {
        char c = stmSerial.read();
        Serial.write(c);
    }

    if (Serial.available())
    {
        char c = Serial.read();
        stmSerial.write(c);
    }
}
