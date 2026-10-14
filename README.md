# PHSI245 Class Project 2025

This project contains a collection of games and libraries to support development and playing with the class project, a CH32X035-based hand-held game console, based on the schematic in the `hw` folder.

See [src_games/README.md](src_games/README.md) for details on the working games/apps and how to select them to be programmed.

See [FirstRun.md](FirstRun.md) to help you get your board running for the first time. 


## Setup for development
 - Install VS Code
 - Install PlatformIO
 - Install Git
 - Load this project
 - Run `pip install chprog` in the PlatformIO terminal.

The final step depends on your operating system.

### Setting up a Windows machine
  Use Zadig (https://zadig.akeo.ie/) to install drivers: Go to `File`->`Load Config` and select`prog_tools/X035ZadigConfig.cfg`, and click to install WinUSB. (This is required for `chprog` to work.)

  Then you should be good to go.

### On a Linux machine
  You'll need to add some rules to udev to give yourself permissions to connect to the device for programming. (See [https://pypi.org/project/chprog/](https://pypi.org/project/chprog/).)

### On a Mac
  Should be as simple as providing permissions when asked. You may have to install libusb if it is not already installed.

## Nonsense on the Lab machines
VS Code and PlatformIO currently choke on UNC paths, which is used internally to make your Document, Downloads, Desktop, etc available university wide.

Put this code your `H:/` drive (found in `This PC`) instead.
