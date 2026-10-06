#!/usr/bin/env python3
"""Send the PC's local time to the STM32 RTC."""

import argparse
from datetime import datetime
import sys

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="CH340 serial port, e.g. /dev/ttyUSB0 or COM3")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, 115200, timeout=3, write_timeout=3) as port:
            port.reset_input_buffer()
            now = datetime.now()
            timestamp = now.strftime("%Y-%m-%d %H:%M:%S")
            port.write(f"SET {timestamp} {now.isoweekday()}\n".encode("ascii"))
            port.flush()
            reply = port.readline().decode("ascii", errors="replace").strip()
            if reply != "OK":
                print(f"Synchronization failed: {reply or 'no reply within 3 seconds'}",
                      file=sys.stderr)
                return 1
    except (serial.SerialException, OSError) as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1

    print(f"RTC set to PC local time: {timestamp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
