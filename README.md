# Kinesis Bridge (BLE → USB HID) — nRF52840 Dongle

Custom Zephyr/NCS application for `PCA10059` (`nrf52840dongle`).

`Goal`: pair with **Kinesis Advantage 360 Pro** over BLE and proxy HID to USB.

---

## Prerequisites

### Install and configure the NCS toolchain (one-time per machine):

```bash
   ./install_ncs_toolchain.sh
```

This installs `nrfutil`, required plugins, `NCS v3.0.2 SDK` + toolchain, and generates:

- `~/ncs_v3.0.2_env.sh` — environment script
- `~/use_ncs_v3.0.2.sh` — convenience loader

### Load the environment (in every shell where you build):

```bash
source ~/ncs_v3.0.2_env.sh
```

or:

```bash
~/use_ncs_v3.0.2.sh
```

## Workflow
### Update workspace (optional)
Initialize/update the NCS west workspace in `$HOME/ncs-workspace` if/when you need Zephyr/NCS repos synced:

```bash
./update_workspace.sh
```

### Build and flash the firmware
Put the dongle into DFU mode (hold RESET while plugging into USB, then release). Then:

```bash
./build.sh
```

### After flashing, unplug/replug the dongle. A new USB serial device should appear, e.g. /dev/tty.usbmodemXXXXXXXX. Open it (don’t use the DFU port; use the CDC-ACM one)

```bash
```
screen /dev/tty.usbmodem* 115200
```
```


### Press the dongle button. You should see lines like

```bash
```
[SCAN] XX:XX:XX:XX:XX:XX (random) RSSI -52 NAME Kinesis Advantage360 Pro
```

```

## Defaults in build.sh:

- Builds the app from the current project directory (`APP=$PWD`)
- Target board: `nrf52840dongle`
- Produces `app_dfu.zip` in the project root
- Flashes to `/dev/tty.usbmodemD5606742A6991` (auto-detects another /dev/tty.usbmodem* if not present)

## Override examples:

```bash
APP=/path/to/app ./build.sh
BOARD=nrf52840dongle ./build.sh
PORT=/dev/tty.usbmodemXXXX ./build.sh
BUILD_DIR=/tmp/out ./build.sh
```

### Notes
You do not need to run west build manually; the scripts handle build and DFU packaging.
The environment script in `$HOME` is shared across all your NCS projects.

Run `./update_workspace.sh` only when you want to initialize or refresh the NCS workspace; day-to-day, `./build.sh` is enough.

## Project layout
```bash
.
├── install_ncs_toolchain.sh   # one-time toolchain installer; creates ~/ncs_v3.0.2_env.sh
├── update_workspace.sh        # optional: init/update west workspace in $HOME/ncs-workspace
├── build.sh                   # build + DFU flash (no workspace ops)
├── CMakeLists.txt             # Zephyr app CMake
├── prj.conf                   # Zephyr app config
├── Kconfig                    # App Kconfig (future options live here)
└── src/
    └── main.c                 # App entry point
```
