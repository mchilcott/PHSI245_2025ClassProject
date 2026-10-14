# A few tools

In `platformio.ini`, set the src_dir variable to one of the application folders, and then compile. E.g.

```
[platformio]
src_dir = src_tools/touch_cal
```

This will build the touch calibration tool. This folder contains the `first_run` and `touch_cal` tools for testing your board initially. It also has a port of the classic `i2c_scanner` firmware, for detecting devices on the i2c bus. This will only list the display on the stock hardware, but there's nothing stopping you wiring up another device.

As usual, remove or comment out the `src_dir` line to go back to using the default `src/` directory for development.
