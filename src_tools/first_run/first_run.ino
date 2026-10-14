#include <Arduino.h>
#include <HAL.h>
#include <Arduboy2.h>
#include <CH32X035_USBSerial.h>
#include <Wire.h>

using namespace wch::usbcdc;

Arduboy2 arduboy;


void LED_pulse(unsigned char n){
    while (n > 0)
    {
    digitalWrite(PIN_LED_0, HIGH); 
    digitalWrite(PIN_LED_1, HIGH); 
    delay(500);
    digitalWrite(PIN_LED_0, LOW); 
    digitalWrite(PIN_LED_1, LOW); 
    delay(500);
    --n;
    }
    delay(2000);
}

void setup()
{
  USBSerial.begin();
}

void loop()
{
  pinMode(PIN_LED_0, OUTPUT);
  pinMode(PIN_LED_1, OUTPUT);
  digitalWrite(PIN_LED_0, LOW);
  digitalWrite(PIN_LED_1, LOW);

  pinMode(PIN_BEEP, OUTPUT);
  digitalWrite(PIN_BEEP, PIN_BEEP_OFF);

  if (USBSerial.dtr()) USBSerial.println("first_run: leds");
  if (USBSerial.dtr()) USBSerial.println("    LED0");
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_0, HIGH); delay(200);
    digitalWrite(PIN_LED_0, LOW);  delay(200);
  }
  if (USBSerial.dtr()) USBSerial.println("    LED1");
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_1, HIGH); delay(200);
    digitalWrite(PIN_LED_1, LOW);  delay(200);
  }

  if (USBSerial.dtr()) USBSerial.println("first_run: speaker test");
  BeepPin1 beep;
  beep.begin();
  beep.tone(beep.freq(440), 20);
  while (beep.duration) { beep.timer(); delay(5); }
  beep.tone(beep.freq(660), 20);
  while (beep.duration) { beep.timer(); delay(5); }
  beep.tone(beep.freq(880), 20);
  while (beep.duration) { beep.timer(); delay(5); }
  beep.noTone();

  if (USBSerial.dtr()) USBSerial.println("first_run: i2c line check");
  pinMode(PIN_I2C_SCL, INPUT_PULLDOWN);
  pinMode(PIN_I2C_SDA, INPUT_PULLDOWN);
  delay(2);
  bool sclHigh = digitalRead(PIN_I2C_SCL) ? true : false;
  bool sdaHigh = digitalRead(PIN_I2C_SDA) ? true : false;
  pinMode(PIN_I2C_SCL, INPUT);
  pinMode(PIN_I2C_SDA, INPUT);
  if (USBSerial.dtr()) USBSerial.print("scl=");
  if (USBSerial.dtr()) USBSerial.print(sclHigh ? "high" : "LOW");
  if (USBSerial.dtr()) USBSerial.print(" sda=");
  if (USBSerial.dtr()) USBSerial.println(sdaHigh ? "high" : "LOW");
  
  if (!sclHigh) LED_pulse(1);
  if (!sdaHigh) LED_pulse(2);

  if (USBSerial.dtr()) USBSerial.println("first_run: i2c init + probe"); 
  
  Wire.begin((uint32_t) PA11, (uint32_t) PA10);

  Wire.beginTransmission(0x3C);
  uint8_t err = Wire.endTransmission();
  
  if (err){
    if (USBSerial.dtr()) USBSerial.print("FAIL: ");
    if (USBSerial.dtr()) USBSerial.println(err);
    LED_pulse(3);
  } else {
    if (USBSerial.dtr()) USBSerial.println("success");
  }
  
  if (err == 0) {
    arduboy.boot();
    arduboy.clear();
    arduboy.setCursor(0, 0);
    arduboy.print("First Run!");
    arduboy.display();
  }
  delay(1000);
}
