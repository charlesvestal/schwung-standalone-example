# Schwung Standalone Example

A reference standalone tool module for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move. Demonstrates direct SPI hardware access without running alongside Move firmware.

When launched from the Tools menu, Move is killed and this binary takes exclusive control of the hardware. When you exit (Back button), Move restarts automatically.

## What It Does

- **Sine synth**: Press pads to play polyphonic sine waves (8 voices, notes 68-99)
- **LEDs**: Pads light up with cycling colors on press, turn off on release
- **Display**: 128x64 1-bit screen shows title, active notes, and exit hint
- **Exit**: Back button or SIGTERM — Move restarts automatically

## How It Works

The binary opens `/dev/ablspi0.0` directly via `open()` + `mmap()` and runs a main loop driven by `ioctl()`. Each tick (~2.9ms at 44.1kHz/128 frames):

1. Clear output buffer
2. Parse MIDI input from pads/buttons
3. Render audio (sine waves for held pads)
4. Push one display slice (7-phase protocol: phase 0 = new frame signal, phases 1-6 = pixel data)
5. Trigger blocking SPI transfer

No LD_PRELOAD, no shared memory, no QuickJS — just a C binary talking to hardware.

## SPI Buffer Layout

| Offset | Size | Direction | Content |
|--------|------|-----------|---------|
| 0-79 | 80B | Out | MIDI OUT (20 x 4-byte USB-MIDI packets) |
| 80-83 | 4B | Out | Display slice number (0=new frame, 1-6=slices) |
| 84-255 | 172B | Out | Display slice pixel data |
| 256-767 | 512B | Out | Audio OUT (128 frames x stereo int16) |
| 2048-2303 | 256B | In | MIDI IN (31 x 8-byte events) |
| 2304-2815 | 512B | In | Audio IN (128 frames x stereo int16) |

## Display Format

128x64 pixels, 1-bit, packed as 8 vertical bands of 128 columns. Each byte encodes 8 vertical pixels (bit 0 = top). The 1024-byte packed buffer is sent in 6 slices of 172 bytes (slice 6 = 164 bytes), with a phase-0 "new frame" signal before each set of 6.

## Building

```bash
./scripts/build.sh
```

Requires Docker (cross-compiles for ARM64). Output: `dist/standalone-example/`.

## Installing

```bash
./scripts/install.sh
```

Copies to `/data/UserData/schwung/modules/tools/standalone-example/` on the Move. Requires Schwung with standalone module support (v0.8.8+).

## Using as a Template

To build your own standalone module:

1. Fork this repo
2. Replace `src/dsp/standalone.c` with your code
3. Update `src/module.json` with your module's id, name, description
4. Update `scripts/build.sh` and `release.json` with your module id
5. `#include "schwung_spi_lib.h"` for SPI constants and MIDI types

Your binary needs to:
- Open `/dev/ablspi0.0` with `open()` + `mmap()`
- Run a main loop calling `ioctl(fd, 0xa, 0x300)` for each SPI transfer
- Handle exit (Back button = CC 51 value 127, or SIGTERM)
- Use the 7-phase display protocol (phase 0 zeroes, then 6 data slices)

## Requirements

- Schwung v0.8.8+ (standalone module support + `launch-standalone.sh`)
- Ableton Move hardware

## License

MIT
