# Native SSH

Enable and disable the built-in SSH server on **NextUI** handhelds directly from the launcher. When SSH is active, the Pak displays the device IP address, port, and default password so you can connect immediately.

## Supported Platforms

| Platform | Device | Build |
|----------|--------|-------|
| `tg5040` | TrimUI Brick, TrimUI Smart Pro | Docker (ARM64) |
| `tg5050` | TrimUI Smart Pro S | Docker (ARM64) |
| `mac` | macOS (local testing) | Native |

## Requirements

- **TG5040**: Stock OS **1.1.1** or higher (the Pak will warn you if your OS is too old)
- **TG5050**: Any stock OS version

## What It Does

- Toggles the native SSH setting via `systemval enablessh`
- Starts and stops the `sshd` daemon
- Polls to confirm the service is actually running before showing connection info
- Displays status, IP address, port, and default password

### Default Credentials

| Platform | User | Password |
|----------|------|----------|
| TG5040 | `root` | `tina` |
| TG5050 | `root` | *(empty — no password)* |

## Usage

1. Launch **Native SSH** from the NextUI Tools menu
2. Press **A** to toggle SSH on or off
3. When enabled, the screen shows connection details — connect with:
   ```
   ssh root@<ip> -p 22
   ```
4. Press **B** to quit

## Building

### Prerequisites

**macOS (development):**
```bash
brew install go sdl2 sdl2_ttf sdl2_image sdl2_gfx
```

**Embedded (tg5040/tg5050):**
- Docker with ARM64 support

### First-Time Setup

After cloning, fetch and patch vendored dependencies:
```bash
make deps
```

This runs `go mod vendor` and applies the TG5050 power button patch to Gabagool automatically.

### Build Commands

```bash
# Auto-detect platform and build
make

# Build for specific platform
make mac
make tg5040
make tg5050

# Build for all embedded platforms
make embedded

# Package as .pak bundles for NextUI
make package

# Export TrimUI .pakz (Tools/tg5040 + Tools/tg5050 layout)
make export-trimui

# Update dependencies and re-apply patches
make deps

# See all targets
make help
```

### Output

| Target | Output |
|--------|--------|
| macOS | `build/nativessh` |
| tg5040 | `build/release/tg5040/NativeSSH.pak.zip` |
| tg5050 | `build/release/tg5050/NativeSSH.pak.zip` |
| export-trimui | `build/release/trimui/NativeSSH.pakz` |

The `.pak.zip` includes the binary, `launch.sh`, `pak.json`, `LICENSE`, and required shared libraries (`libSDL2_gfx`).

## Installing on a Handheld

1. Build and package: `make package` or `make export-trimui`
2. If using `make package`, extract `NativeSSH.pak.zip` to your SD card as `Tools/{platform}/NativeSSH.pak/`
3. If using `make export-trimui`, place `NativeSSH.pakz` in the root of your SD card; NextUI will auto-install it upon (re)boot
4. Launch from the NextUI Tools menu

## Acknowledgements

Built with [Gabagool](https://github.com/BrandonKowalski/gabagool) — a UI framework for embedded Linux handhelds by [@BrandonKowalski](https://github.com/BrandonKowalski).

## License

MIT
