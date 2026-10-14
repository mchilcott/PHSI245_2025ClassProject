# First Run: Checking fresh hardware

## Power
  - With the board unplugged and powered off, check the resistance between VBUS and GND to make sure they're not simply shorted.
  - Plug the device in, and check with a voltmeter that you have 5 V and 3.3 V immediately before and after the regulator respectively (C2 and C4 on the provided schematic).
  - Also check 3.3 V at the MCU VDD pins and at the display VCC pin. If the 3.3 V rail is missing or well below 3.3 V, stop and find the short/regulator fault before powering anything else.

## USB
  - Plug the device in while holding the **BOOT** (programming) button down, to enter the bootloader.
  - Does it enumerate (see below)? If so, we can program the board.
  - Try both orientations of the USB C connector. If it only works one way, then you might have issues at the USB connector. Make sure both of the UDP/D+ and both of the UDM/D- lines are connected.

### Checking if the USB device enumerates
  Windows:
  
    - If you have sound on, you can hear the "New Device" sound if the device enumerates on the USB bus. You can also check on Device Manager, `usbview.exe` from Microsoft, or just try programming the board and it will complain if it cant see the device.
    
  Linux:

    - The commands `lsusb` or `dmesg -W` are your friends.

## Board Outputs: LED/Speaker/Display
  
  Being able to program the board is a major milestone. At this point, we can upload code to the microcontroller to help us test the rest of the board.

  - Select the test app by setting `src_dir = src_tools/first_run` in `platformio.ini`, then build and upload.

  This code should:
  
  - Flash LED0 three times, then LED1 three times. 
  - Beep the speaker three times (440 Hz, 660 Hz, 880 Hz).
  - Check the display connection

  If there is an issue with the diplay connection, then both LEDs will slow flash a number of times:
  
    1) SCL is low, 
    2) SDA is low,
    3) NACK when trying to connect to display.
    
  It is possible that you'll get more than one of these errors, with a 2 second delay between them. (If you get 1 or 2, you'll probably also get 3.)
  
  The status of the testing loop is also available on the serial monitor.

### LED and Speaker
  - These are very simple devices, simply toggled on and off by one of the pins on the microcontroller, with a current limiting resistor in series. Make sure these are all well soldered (you can test connectivity with a multimeter too), and in the case of the LEDs, make sure they're soldered on the right way round.
  - Each LED is driven on its own first so a fault on one doesn't hide the other. The speaker idles with `PIN_BEEP` HIGH (no current drawn) and is driven by a timer; if you hear nothing, check the speaker and its series resistor.

### Display
  - The display has a few more "moving parts": Check that the power pins on the display are getting the 3.3 V. Make sure that SDA and SCL are being pulled up. With the board powered off, measure the resistance between VCC and each of the SDA/SCL pins; it should be about 2.2 kOhm. The test app also checks this in firmware by briefly pulling each line down internally and reading the level back.
  - The display is addressed at I2C address **0x3C**. If the app reports `scl=LOW` or `sda=LOW`, the pull-ups are missing or not soldered; if the following probe fails, check SDA/SCL continuity and the display's power.
  
## Board inputs: the buttons
  Upload the button test/calibration app by setting `src_dir = src_tools/touch_cal` in `platformio.ini`, then build and upload.
  
  - The buttons are touch (capacitive) keys read through the Analog-to-Digital Converter (ADC).
  - They should change considerably when pressed if they are working: a valid press drops the raw reading substantially. Ideally, it should indicate with an asterisk on the display when it is pressed. 
  - If one or two buttons are quite different from the others, check your wiring.
  - Because there the capacitance depends a bit on board geometry, you may need to adjust the `TOUCH_THRESHOLD` value in the Hardware Abstraction Layer (HAL): `lib/PHSI245_HAL/HAL.h`.



