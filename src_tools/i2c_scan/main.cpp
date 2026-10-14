// --------------------------------------
// A varient of the standard Arduino i2c_scanner
//
// This sketch tests the standard 7-bit addresses
// Devices with higher bit address might not be seen properly.
//
#include <Arduino.h>
#include <Wire.h>

#include <CH32X035_USBSerial.h>
using namespace wch::usbcdc;

void setup()
{
  Wire.begin();

  USBSerial.begin();
  USBSerial.waitForPC(0);
  USBSerial.println("\nI2C Scanner");
  USBSerial.flush();
}


void loop()
{
  byte error, address;
  int nDevices;

  Serial.println("Scanning...");

  nDevices = 0;
  for(address = 1; address < 127; address++ ) 
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      USBSerial.print("I2C device found at address 0x");
      if (address<16) 
        USBSerial.print("0");
      USBSerial.print(address,HEX);
      USBSerial.println("  !");

      nDevices++;
    }
  }
  if (nDevices == 0)
    USBSerial.println("No I2C devices found\n");
  else
    USBSerial.println("done\n");
  USBSerial.flush();
  delay(5000);           // wait 5 seconds for next scan
} 
