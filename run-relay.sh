#!/bin/sh

# export LORADEV=/dev/cu.usbserial-0001
# export LORADEV=/dev/cu.usbserial-7
platformio run --target upload --upload-port $LORADEV && ~/repos/github/roloran/ROLORAN-Terminal/roloran-terminal.py
