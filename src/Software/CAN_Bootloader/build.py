#!/usr/bin/env python3
"""
CAN_Bootloader Firmware Build and Upload Script
=================================================
Universal script, works on any PC regardless of installation paths.
Requires Python 3. Invoke as `python build.py` or `python3 build.py`.

Usage:
    python build.py              # configure if needed, then build
    python build.py clean        # delete build directory
    python build.py rebuild      # clean, configure, build
    python build.py upload       # build, then flash via picotool (BOOTSEL)
    python build.py swd          # build, then flash via SWD (CMSIS-DAP / OpenOCD)
    python build.py size         # show memory usage of last build
"""
import os
import sys
import glob
import shutil
import subprocess
import platform

PROJECT_NAME = "CAN_Bootloader"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR  = os.path.join(SCRIPT_DIR, "build")
UF2_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".uf2")
ELF_FILE   = os.path.join(BUILD_DIR, PROJECT_NAME + ".elf")

if platform.system() == "Windows":
    _USER_HOME = os.environ.get("USERPROFILE", "")
else:
    _USER_HOME = os.environ.get("HOME", "")

_PICO_SDK_BASE = os.path.join(_USER_HOME, ".pico-sdk")


def _find_tool(name, *fallback_paths):
    found = shutil.which(name)
    if found:
        return found
    for p in fallback_paths:
        if os.path.isfile(p):
            return p
    return None


def _newest_glob(pattern):
    matches = sorted(glob.glob(pattern))
    return matches[-1] if matches else None


def find_cmake():
    tool = _find_tool(
        "cmake",
        os.path.join(_PICO_SDK_BASE, "cmake", "v3.31.5", "bin", "cmake.exe"),
        r"C:\Program Files\CMake\bin\cmake.exe",
        r"C:\Program Files (x86)\CMake\bin\cmake.exe",
    )
    if not tool:
        tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "cmake", "*", "bin", "cmake*"))
    if not tool:
        sys.exit("[ERROR] cmake not found.")
    return tool


def find_ninja():
    tool = _find_tool(
        "ninja",
        os.path.join(_PICO_SDK_BASE, "ninja", "v1.12.1", "ninja.exe"),
    )
    if not tool:
        tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "ninja", "*", "ninja*"))
    if not tool:
        sys.exit("[ERROR] ninja not found.")
    return tool


def find_picotool():
    tool = _find_tool("picotool")
    if tool:
        return tool
    ext = ".exe" if platform.system() == "Windows" else ""
    tool = _newest_glob(os.path.join(_PICO_SDK_BASE, "picotool", "*", "picotool", "picotool" + ext))
    if tool:
        return tool
    sys.exit("[ERROR] picotool not found.")


def find_openocd():
    ext = ".exe" if platform.system() == "Windows" else ""
    # Prefer the Pico SDK OpenOCD: it ships the RP2350 target scripts that stock
    # builds (e.g. Chocolatey) on PATH usually lack.
    exe = _newest_glob(os.path.join(_PICO_SDK_BASE, "openocd", "*", "openocd" + ext)) \
        or _find_tool("openocd")
    if not exe:
        sys.exit("[ERROR] openocd not found.")
    scripts = os.path.join(os.path.dirname(exe), "scripts")
    return exe, scripts


def _find_toolchain_bin():
    return _newest_glob(os.path.join(_PICO_SDK_BASE, "toolchain", "*", "bin"))


def find_size():
    tool = _find_tool("arm-none-eabi-size")
    if tool:
        return tool
    tc_bin = _find_toolchain_bin()
    if tc_bin:
        for name in ("arm-none-eabi-size.exe", "arm-none-eabi-size"):
            p = os.path.join(tc_bin, name)
            if os.path.isfile(p):
                return p
    return None


def find_pico_sdk():
    env = os.environ.get("PICO_SDK_PATH", "")
    if env and os.path.isdir(env):
        return env
    found = _newest_glob(os.path.join(_PICO_SDK_BASE, "sdk", "*"))
    if found and os.path.isdir(found):
        return found
    sys.exit("[ERROR] Pico SDK not found. Set PICO_SDK_PATH.")


def find_pico_toolchain():
    env = os.environ.get("PICO_TOOLCHAIN_PATH", "")
    if env and os.path.isdir(env):
        return env
    found = _newest_glob(os.path.join(_PICO_SDK_BASE, "toolchain", "*"))
    if found and os.path.isdir(found):
        return found
    return None


def _run(cmd, env=None):
    print(f"[RUN] {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, env=env)
    if result.returncode != 0:
        sys.exit(f"[ERROR] Command failed with exit code {result.returncode}")


def do_clean():
    if os.path.isdir(BUILD_DIR):
        print(f"[*] Removing {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
    else:
        print("[*] Build directory does not exist.")


def do_configure():
    cmake = find_cmake()
    ninja = find_ninja()
    sdk   = find_pico_sdk()
    tc    = find_pico_toolchain()

    os.makedirs(BUILD_DIR, exist_ok=True)

    env = os.environ.copy()
    env["PICO_SDK_PATH"] = sdk
    if tc:
        env["PICO_TOOLCHAIN_PATH"] = tc
        tc_bin = os.path.join(tc, "bin")
        if tc_bin not in env.get("PATH", ""):
            env["PATH"] = tc_bin + os.pathsep + env.get("PATH", "")

    cmd = [
        cmake,
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DPICO_SDK_PATH={sdk}",
        "-B", BUILD_DIR,
        "-S", SCRIPT_DIR,
    ]
    print("[*] Configuring CMake...")
    _run(cmd, env=env)


def do_build():
    cmake = find_cmake()
    if not os.path.isfile(os.path.join(BUILD_DIR, "CMakeCache.txt")):
        do_configure()
    print("[*] Building firmware...")
    _run([cmake, "--build", BUILD_DIR, "--", "-j4"])
    print("\n[+] BUILD SUCCESSFUL")
    do_size()


def do_size():
    size = find_size()
    if size and os.path.isfile(ELF_FILE):
        print("\n=== Memory Usage ===")
        subprocess.run([size, ELF_FILE])
        print("\n=== Output Files ===")
        for f in (UF2_FILE, ELF_FILE):
            if os.path.isfile(f):
                print(f"   {os.path.basename(f)}  -  {os.path.getsize(f):,} bytes")
    elif not os.path.isfile(ELF_FILE):
        print("[!] No ELF found. Run build first.")


def do_upload():
    if not os.path.isfile(UF2_FILE):
        print("[*] UF2 not found, building first.")
        do_build()

    picotool = find_picotool()
    print("\n[*] Flashing via picotool (put board in BOOTSEL mode)...")
    _run([picotool, "load", UF2_FILE, "-f"])
    print("[*] Rebooting board...")
    _run([picotool, "reboot"])
    print("\n[+] FLASH SUCCESSFUL")


def do_swd():
    if not os.path.isfile(ELF_FILE):
        print("[*] ELF not found, building first.")
        do_build()
    openocd, scripts = find_openocd()
    elf = ELF_FILE.replace("\\", "/")
    print("\n[*] Flashing via SWD (CMSIS-DAP / OpenOCD)...")
    _run([
        openocd, "-s", scripts,
        "-f", "interface/cmsis-dap.cfg",
        "-f", "target/rp2350.cfg",
        "-c", "adapter speed 5000",
        "-c", f"program {elf} verify reset exit",
    ])
    print("\n[+] SWD FLASH SUCCESSFUL")


def main():
    action = sys.argv[1].lower() if len(sys.argv) > 1 else "build"
    if action == "clean":
        do_clean()
    elif action == "configure":
        do_configure()
    elif action == "rebuild":
        do_clean()
        do_build()
    elif action == "upload":
        do_upload()
    elif action == "swd":
        do_swd()
    elif action == "size":
        do_size()
    elif action == "build":
        do_build()
    else:
        print(__doc__)
        sys.exit(f"[ERROR] Unknown action: {action}")


if __name__ == "__main__":
    main()