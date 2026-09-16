# Create_M54C_RTOS.ps1

One PowerShell script that generates a ready-to-build FreeRTOS project for the BladeCore-M54C module (RP2354B) and, when asked, keeps the project's `CMakeLists.txt` in sync with your own source files.

Location: `src/Software/Template/Create_M54C_RTOS.ps1`

## What it does

- Writes a complete project (`main.c`, `CONFIG.h`, `FreeRTOSConfig.h`, `CMakeLists.txt`, `pico_sdk_import.cmake`, `build.py`, `.gitignore`, `.vscode/`) into a folder you choose.
- Creates an `incl/` folder for your own source and header files.
- Downloads and installs whatever is missing on your PC: Git, Python 3, CMake, Ninja, ARM GNU toolchain, Raspberry Pi Pico SDK, picotool, OpenOCD, FreeRTOS-Kernel.
- Sets `PICO_SDK_PATH` and `PICO_TOOLCHAIN_PATH` at user scope.
- Has an update mode that scans `incl/` and rewrites two marked regions of `CMakeLists.txt` so every `.c` becomes a source and every subfolder with a `.h` becomes an include directory.

## Requirements

- Windows 10 or 11
- Windows PowerShell 5.1 or newer
- winget available (only used to install Git and Python if they are missing)
- Internet access on the first run

You do NOT need admin rights. Everything installs under `%USERPROFILE%\.pico-sdk\`.

## Quick start

Open PowerShell in the repo root and run:

```powershell
.\src\Software\Template\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -ProjectName MyProject
```

That single command will:
1. Check every required tool. Install anything missing.
2. Clone the Pico SDK and FreeRTOS-Kernel.
3. Write all project files at `C:\Dev\MyProject`.
4. Print a "Next steps" block with the exact build command.

## Build and flash

From inside the generated project folder:

```powershell
python3 build.py            # configure if needed, then build
python3 build.py rebuild    # wipe build/, then build from scratch
python3 build.py clean      # delete build/
python3 build.py size       # print memory usage of last build
python3 build.py upload     # build, then flash via picotool (hold BOOTSEL)
```

Build outputs land in `build/`:

```
MyProject.elf, MyProject.hex, MyProject.uf2, MyProject.bin, MyProject.dis, MyProject.elf.map
```

The `.uf2` is what you drag onto the RPI-RP2 mass storage volume, or `python3 build.py upload` handles it automatically.

## Adding your own source files

1. Drop `.c` and `.h` files under `incl/`. Group them in subfolders however you like, a folder can hold as many files as you want:
    ```
    incl/
       task/
          task1.c
          task1.h
          task2.c
          task2.h
       utils/
          utils.c
          utils.h
    ```
2. Run:
    ```powershell
    .\src\Software\Template\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -Update
    ```
3. `CMakeLists.txt` now has your sources between `# === USER SOURCES BEGIN ===` and `# === USER SOURCES END ===`, and your header folders between `# === USER INCLUDE DIRS BEGIN ===` and `# === USER INCLUDE DIRS END ===`. Nothing else is touched.
4. Rebuild:
    ```powershell
    python3 build.py rebuild
    ```

You can `#include "task/task1.h"` from `main.c` right away because `incl/` is on the include path by default.

Rules for `-Update`:
- Every `.c` under `incl/` becomes a source.
- Every subfolder that contains at least one `.h` becomes an include directory.
- Only `.c` and `.h` extensions are picked up. `.cpp`, `.cc`, `.S`, `.hpp` are ignored; add them to `CMakeLists.txt` outside the marker regions.
- File and folder names must not contain spaces or `#` / `+` / `"`. CMake will misparse them.
- Do not put symlinks or junctions under `incl/`. The scan follows them and can loop.
- Anything you put inside the marker regions is overwritten on the next `-Update`. Custom tweaks belong outside the markers.

## Command reference

```powershell
.\Create_M54C_RTOS.ps1 -TargetPath <path> [-ProjectName <name>] [-SkipToolInstall] [-Force]
.\Create_M54C_RTOS.ps1 -TargetPath <path> -Update
```

| Parameter          | Mode   | Meaning                                                                 |
|--------------------|--------|-------------------------------------------------------------------------|
| `-TargetPath`      | both   | Path of the project folder.                                             |
| `-ProjectName`     | create | C identifier used as the CMake target name. Defaults to the folder name.|
| `-Update`          | update | Rescan `incl/` and rewrite the two marker regions of `CMakeLists.txt`.  |
| `-SkipToolInstall` | create | Skip all tool detection and installation.                               |
| `-Force`           | create | Overwrite existing files in `TargetPath`.                               |

Examples:

```powershell
# Create a new project, install any missing tools
.\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -ProjectName MyProject

# Regenerate files into a folder that already has stuff in it
.\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -Force

# Only generate files, don't touch anything else on the PC
.\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -SkipToolInstall

# After you've added or removed files under incl\
.\Create_M54C_RTOS.ps1 -TargetPath C:\Dev\MyProject -Update
```

## What gets installed and where

Everything lands under `%USERPROFILE%\.pico-sdk\` (no admin needed):

| Tool                | Version      | Location                                             |
|---------------------|--------------|------------------------------------------------------|
| Pico SDK            | 2.2.0        | `%USERPROFILE%\.pico-sdk\sdk\2.2.0\`                 |
| ARM GNU toolchain   | 14.2.rel1    | `%USERPROFILE%\.pico-sdk\toolchain\14_2_Rel1\`       |
| CMake               | 3.31.5       | `%USERPROFILE%\.pico-sdk\cmake\v3.31.5\`             |
| Ninja               | 1.12.1       | `%USERPROFILE%\.pico-sdk\ninja\v1.12.1\`             |
| picotool            | 2.2.0-a4     | `%USERPROFILE%\.pico-sdk\picotool\2.2.0-a4\`         |
| OpenOCD             | 0.12.0+dev   | `%USERPROFILE%\.pico-sdk\openocd\0.12.0+dev\`        |
| FreeRTOS-Kernel     | main branch  | `<project>\FreeRTOS-Kernel\` (git clone per project) |

Winget is used to install Git and Python 3 if they are missing on your PATH.

Two user environment variables are set:
- `PICO_SDK_PATH`
- `PICO_TOOLCHAIN_PATH`

## Generated project layout

```
MyProject/
   .vscode/                (IDE tasks, launch, IntelliSense, CMake kits)
   FreeRTOS-Kernel/        (git clone, ignored by git)
   incl/                   (your source and header files go here)
   build/                  (created by build.py, ignored by git)
   .gitignore
   build.py                (build and flash helper)
   CMakeLists.txt          (has USER SOURCES and USER INCLUDE DIRS regions)
   CONFIG.h                (BladeCore-M54C pin map)
   FreeRTOSConfig.h
   main.c
   pico_sdk_import.cmake
```

## VS Code

Open the generated folder:

```powershell
code C:\Dev\MyProject
```

The recommended extensions (Cortex-Debug, C/C++, Raspberry Pi Pico, Serial Monitor) are listed in `.vscode/extensions.json`.

- `Ctrl+Shift+B` runs the "Compile Project" task.
- The "Flash" and "Rescue Reset" tasks use OpenOCD over CMSIS-DAP.
- Two Cortex-Debug launch configs are provided: embedded OpenOCD and external OpenOCD on `localhost:3333`.

## Troubleshooting

**"Target path is not empty. Use -Force to overwrite."**
Either point `-TargetPath` at an empty or non-existent folder, or pass `-Force`.

**"USER SOURCES markers not found in CMakeLists.txt"**
The `-Update` mode requires the two marker regions. If you deleted them, restore them by running create mode with `-Force` (this overwrites CMakeLists.txt), then re-add your customizations outside the markers.

**Build cannot find CMake / Ninja / picotool**
Open a fresh terminal so the updated `PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`, and `PATH` are picked up. `build.py` also searches `%USERPROFILE%\.pico-sdk\...` as a fallback, so it works even if PATH is misconfigured.

**Board does not enter BOOTSEL**
Hold the BOOTSEL button while plugging USB, then run `python3 build.py upload`.

**Python 2 is on PATH as `python`**
The script auto-prefers `python3` and only accepts a `Python 3.x` version. If your PC only has Python 2, it will install Python 3.12 via winget.
