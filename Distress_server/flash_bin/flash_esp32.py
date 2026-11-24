#!/usr/bin/env python3
"""
flash_esp32.py - flash one application .bin to an ESP32-S3 with esptool.

Only the app is written (at 0x10000). The bootloader and partition table already
on the board are left alone, so the board must have been flashed with a full
image at least once before.

Examples
--------
  python flash_esp32.py firmware.bin
  python flash_esp32.py firmware.bin --port COM5
  python flash_esp32.py firmware.bin --baud 115200
  python flash_esp32.py                  # a file dialog opens to pick the .bin
"""

import argparse
import importlib
import subprocess
import sys
from pathlib import Path

CHIP = "esp32"
APP_OFFSET = 0x10000

# USB vendor IDs of the USB-serial chips found on common ESP32-S3 boards
KNOWN_VIDS = {
    0x10C4: "CP210x",
    0x1A86: "CH340/CH9102",
    0x0403: "FTDI",
    0x303A: "Espressif native USB",
}


def ensure_esptool():
    """Make sure esptool (and pyserial, which it depends on) can be imported."""
    try:
        import esptool  # noqa: F401
        import serial.tools.list_ports  # noqa: F401
        return
    except ImportError:
        pass

    print("esptool is not installed.")
    try:
        answer = input("Install it now with pip? [Y/n] ").strip().lower()
    except EOFError:
        answer = "n"
    if answer not in ("", "y", "yes"):
        sys.exit("Install it yourself with:  python -m pip install esptool")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--upgrade", "esptool"])
    importlib.invalidate_caches()


def pick_port():
    """Find the board's serial port; ask the user if there is more than one candidate."""
    from serial.tools import list_ports

    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        sys.exit(
            "No serial ports found. Plug the board in, and make sure the USB cable "
            "carries data (not charge-only). You may also need the CP210x / CH340 driver."
        )

    likely = [p for p in ports if p.vid in KNOWN_VIDS]
    candidates = likely or ports

    if len(candidates) == 1:
        p = candidates[0]
        print(f"Using {p.device} ({p.description})")
        return p.device

    print("Several serial ports found:")
    for i, p in enumerate(candidates, 1):
        print(f"  {i}) {p.device} - {p.description}")
    while True:
        choice = input("Choose a port number: ").strip()
        if choice.isdigit() and 1 <= int(choice) <= len(candidates):
            return candidates[int(choice) - 1].device


def ask_for_file():
    """Let the user choose the .bin file (dialog if possible, typed path otherwise)."""
    path = ""
    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        path = filedialog.askopenfilename(
            title="Select the application .bin file",
            filetypes=[("Firmware", "*.bin"), ("All files", "*.*")],
        )
        root.destroy()
    except Exception:
        path = input("Path to the .bin file: ").strip().strip('"')
    if not path:
        sys.exit("No file selected.")
    return path


def run(cmd):
    print("\n> " + " ".join(cmd) + "\n")
    return subprocess.call(cmd)


def main():
    parser = argparse.ArgumentParser(
        description="Flash one application .bin to an ESP32-S3 (written at 0x10000).",
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("file", nargs="?", help="Application .bin file (dialog opens if omitted)")
    parser.add_argument("--port", "-p", help="Serial port, e.g. COM5 (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=460800, help="Flash baud rate (default: 460800)")
    parser.add_argument("--erase", action="store_true",
                        help="Erase the whole flash first (this also removes the bootloader!)")
    args = parser.parse_args()

    firmware = Path(args.file or ask_for_file()).expanduser()
    if not firmware.is_file():
        sys.exit(f"File not found: {firmware}")
    if "merged" in firmware.name.lower():
        sys.exit(
            f"'{firmware.name}' looks like a merged image (bootloader + partitions + app).\n"
            f"This script writes the app only, at {hex(APP_OFFSET)}. Use the plain app .bin instead."
        )

    ensure_esptool()
    port = args.port or pick_port()

    print(f"\nFlashing {firmware.name} ({firmware.stat().st_size:,} bytes) at {hex(APP_OFFSET)} on {CHIP}")

    base = [sys.executable, "-m", "esptool", "--chip", CHIP, "--port", port, "--baud", str(args.baud)]

    if args.erase and run(base + ["erase_flash"]) != 0:
        return fail()
    if run(base + ["write_flash", hex(APP_OFFSET), str(firmware)]) != 0:
        return fail()

    print("\nDone. If the board does not restart by itself, press the EN/RESET button.")
    return 0


def fail():
    print(
        "\nFlashing failed. Things to try:\n"
        "  - Hold BOOT, tap EN/RESET, release BOOT, then run the script again\n"
        "  - On boards with two USB ports, try the other one (UART vs native USB)\n"
        "  - Use a data-capable USB cable and close Serial Monitor / other programs using the port\n"
        "  - Lower the speed:  --baud 115200\n"
        "  - Check the driver (CP210x / CH340) in Device Manager"
    )
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit("\nCancelled.")
