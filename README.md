# Native SSH

Enable and disable the built-in SSH server on **NextUI** handhelds directly from the launcher. When SSH is active, the Pak displays the device IP address, port, and default password so you can connect immediately.

## Supported Platforms

| Platform | Device | Build |
|----------|--------|-------|
| `tg5040` | TrimUI Brick, TrimUI Smart Pro | Docker (ARM64) |
| `tg5050` | TrimUI Smart Pro S | Docker (ARM64) |
| `my355` | Miyoo Flip | Docker (ARM64) |
| `h700` | Anbernic H700 devices running BaseOS | Docker (ARM64) |
| `mac` | macOS (local testing) | Native |

## Requirements

- **TG5040**: Stock OS **1.1.1** or higher
- **MY355**: Firmware **v250228** or higher
- **TG5050**: Any stock OS version
- **H700**: BaseOS (SSH is enabled by default)

The Pak will warn you at launch if your firmware is too old.

## Default Credentials

| Platform | User | Password |
|----------|------|----------|
| TG5040 | `root` | `tina` |
| TG5050 | `root` | *(empty — no password)* |
| MY355 | `root` | `rockchip` |
| H700 | `root` | `root` |

## Usage

1. Launch **Native SSH** from the NextUI Tools menu
2. Press **A** to toggle SSH on or off
3. When enabled, the screen shows connection details — connect with:
   ```
   ssh root@<ip> -p 22
   ```
4. Press **B** to quit

On device platforms (**TG5040**, **TG5050**, **MY355**, and **H700**), Native SSH persists the chosen SSH state across reboots by managing a small block inside NextUI's `auto.sh` startup script.

## Building

### Prerequisites

**macOS (development):**
```bash
brew install sdl2 sdl2_ttf sdl2_image
```

**Embedded (tg5040/tg5050/my355/h700):**
- Docker

### First-Time Setup

After cloning, initialise the Apostrophe submodule:
```bash
git submodule update --init
```

### Build Commands

```bash
# Build for macOS (development)
make mac

# Build one binary for every supported NextUI device
make universal
make all

# Optional legacy regression builds
make tg5040
make tg5050
make my355

# Copy the universal binary into all four platform trees and build the .pakz
make package

# Deploy to connected device via ADB (auto-detects platform)
make deploy

# See all targets
make help
```

### Output

| Target | Output |
|--------|--------|
| macOS | `build/mac/nativessh` |
| universal | `build/universal/nativessh` |
| tg5040 | `build/release/tg5040/NativeSSH.pak.zip` |
| tg5050 | `build/release/tg5050/NativeSSH.pak.zip` |
| my355 | `build/release/my355/NativeSSH.pak.zip` |
| package | `build/release/all/NativeSSH.pakz` |

## Installing on a Handheld

1. Build and package: `make package`
2. Place `NativeSSH.pakz` in the root of your SD card; NextUI will auto-install it upon (re)boot
3. Launch from the NextUI Tools menu

Or deploy directly via ADB: `make deploy`

## Acknowledgements

Built with [Apostrophe](https://github.com/Helaas/Apostrophe) — a UI library for NextUI Paks.

## License

MIT
