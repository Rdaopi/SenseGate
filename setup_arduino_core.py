"""
SenseGate — Arduino core patcher for RAKwireless nRF Boards v1.3.3

Applies the patches required to compile and flash the gateway firmware.
Run once after installing the RAKwireless board package.

Usage:
    python setup_arduino_core.py
"""

import os
import sys

def _find_core_base():
    candidates = []
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA", "")
        if local:
            candidates.append(os.path.join(local, "Arduino15"))
    elif sys.platform == "darwin":
        candidates.append(os.path.expanduser("~/Library/Arduino15"))
    else:
        candidates.append(os.path.expanduser("~/.arduino15"))
        candidates.append(os.path.expanduser("~/.Arduino15"))
    suffix = os.path.join("packages", "rakwireless", "hardware", "nrf52", "1.3.3")
    for base in candidates:
        path = os.path.join(base, suffix)
        if os.path.exists(path):
            return path
    # Return Windows-style path for the error message even on non-Windows
    return os.path.join(candidates[0] if candidates else "~/.arduino15", suffix)

CORE_BASE = _find_core_base()

PATCHES = {
    # 1. platform.txt — logger=0, genpkg path, upload flags
    os.path.join(CORE_BASE, "platform.txt"): [
        (
            "build.logger_flags=-DCFG_LOGGER=1",
            "build.logger_flags=-DCFG_LOGGER=0",
        ),
        (
            'recipe.objcopy.zip.pattern="{tools.nrfutil.cmd}" dfu genpkg --dev-type 0x0052 --sd-req {build.sd_fwid} --application "{build.path}/{build.project_name}.hex" "{build.path}/{build.project_name}.zip"',
            'recipe.objcopy.zip.pattern="{runtime.platform.path}/tools/adafruit-nrfutil/win32/adafruit-nrfutil.exe" dfu genpkg --dev-type 0x0052 --application "{build.path}/{build.project_name}.hex" "{build.path}/{build.project_name}.zip"',
        ),
        (
            'tools.nrfutil.upload.pattern="{cmd}" {upload.verbose} dfu serial -pkg "{build.path}/{build.project_name}.zip" -p {serial.port} -b 115200 --singlebank',
            'tools.nrfutil.upload.pattern="{cmd}" {upload.verbose} dfu serial --package "{build.path}/{build.project_name}.zip" --port {serial.port} --baudrate 115200',
        ),
    ],
    # 2. Uart.cpp — remove undefined Serial reference in serialEventRun
    os.path.join(CORE_BASE, r"cores\nRF5\Uart.cpp"): [
        (
            "  if (serialEvent && Serial.available() ) serialEvent();",
            "  // if (serialEvent && Serial.available() ) serialEvent();",
        ),
    ],
    # 3. main.cpp — fix Serial1 reference in CFG_LOGGER==1 branch
    os.path.join(CORE_BASE, r"cores\nRF5\main.cpp"): [
        (
            "#elif CFG_LOGGER == 1\n  if ( Serial )\n  {\n    ret = Serial1.write((const uint8_t *) buf, count);\n  }",
            "#elif CFG_LOGGER == 1\n  if ( Serial1 )\n  {\n    ret = Serial1.write((const uint8_t *) buf, count);\n  }",
        ),
    ],
}


def patch_file(path, replacements):
    if not os.path.exists(path):
        print(f"  [SKIP] Not found: {path}")
        return False

    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    changed = False
    for old, new in replacements:
        if old in content:
            content = content.replace(old, new)
            print(f"  [OK]   Patched: {os.path.basename(path)}")
            changed = True
        elif new in content:
            print(f"  [SKIP] Already patched: {os.path.basename(path)}")
        else:
            print(f"  [WARN] Pattern not found in {os.path.basename(path)}: {old[:60]}...")

    if changed:
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

    return changed


def main():
    print("SenseGate — Arduino core patcher")
    print(f"Core path: {CORE_BASE}")
    print()

    if not os.path.exists(CORE_BASE):
        print("ERROR: RAKwireless nRF Boards v1.3.3 not found.")
        print("Install it from Arduino IDE Board Manager using URL:")
        print("  https://raw.githubusercontent.com/RAKwireless/RAKwireless-Arduino-BSP-Index/main/package_rakwireless_index.json")
        sys.exit(1)

    for path, replacements in PATCHES.items():
        patch_file(path, replacements)

    print()
    print("Done. Restart Arduino IDE before compiling.")


if __name__ == "__main__":
    main()
