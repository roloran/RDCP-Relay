#!/bin/sh

platformio run --target upload --upload-port $LORADEV && ../ROLORAN-Terminal/roloran-terminal.py
