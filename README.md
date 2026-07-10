# E16

E16 is a small toolchain for the Ember-16 virtual machine and assembly language. The repository contains a C++ assembler, emulator, disassembler, image and tilemap converters, sample programs, and a Neovim plugin for editing `.e16` files.

## What is included

- `assembler/`: builds `e16asm`, which compiles `.e16` assembly into binary programs.
- `emulator/`: builds `e16emu`, an SDL3-based emulator for running compiled programs.
- `disassembler/`: builds `e16dis`, a terminal disassembler for E16 binaries.
- `converter/`: builds `e16img`, `e16spritepack`, and `e16tilemap` PNG asset converters.
- `musicMaker/`: builds `e16musicmaker`, a six-channel APU tracker with live SDL3 preview, wavetable design, PCM recording, and callable `.e16` music export.
- `tests/`: example `.e16` programs and assets.
- `nvim/e16.nvim/`: Neovim filetype, syntax highlighting, and LSP support.

## Requirements

- CMake 3.10 or newer
- A C++20 compiler
- SDL3 for the emulator and music maker
- zlib for the image converter
- Python 3 for the Neovim LSP server

On macOS with Homebrew:

```sh
brew install cmake sdl3 zlib
```

On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libsdl3-dev zlib1g-dev python3
```

## Build

Each tool is currently its own CMake project.

```sh
cmake -S assembler -B assembler/build
cmake --build assembler/build

cmake -S emulator -B emulator/build
cmake --build emulator/build

cmake -S musicMaker -B musicMaker/build
cmake --build musicMaker/build

cmake -S disassembler -B disassembler/build
cmake --build disassembler/build

cmake -S converter -B converter/build
cmake --build converter/build
```

The binaries are written to each build directory:

- `assembler/build/e16asm`
- `emulator/build/e16emu`
- `musicMaker/build/e16musicmaker`
- `disassembler/build/e16dis`
- `converter/build/e16img`
- `converter/build/e16spritepack`
- `converter/build/e16tilemap`

## Usage

Assemble a program:

```sh
assembler/build/e16asm tests/solid/main.e16 -o tests/solid/main.bin
```

Run it in the emulator:

```sh
emulator/build/e16emu tests/solid/main.bin
```

Run with debugger mode enabled:

```sh
emulator/build/e16emu -s tests/solid/main.bin
```

Compose and export E16 music:

```sh
musicMaker/build/e16musicmaker
```

The exported assembly module provides `music_play`, `music_update`, and `music_stop`. Call `music_update` once per game frame after `music_play` so the song advances without blocking the game loop.

Disassemble a binary:

```sh
disassembler/build/e16dis --plain tests/solid/main.bin
```

Convert a small PNG sprite or tile image:

```sh
converter/build/e16img sprite.png -o sprite.e16img
```

The image converter accepts 8x8, 16x16, and 32x32 PNG files with up to 16 RGB555 colors.

Combine palette-compatible sprite variants:

```sh
converter/build/e16spritepack red.png blue.png -o actors.e16spr --inc actors.e16
```

Create a background asset from a PNG tilemap:

```sh
converter/build/e16tilemap background.png -o level1.e16bg --inc level1.e16 --wrap
```

`e16spritepack` writes one padded 16-color palette page followed by every packed sprite. It checks that all sprites are the same size and share the same opaque shape unless `--force` is passed.

`e16tilemap` splits a PNG into 8x8 tiles, builds one padded 16-color palette page, deduplicates tiles with flip-aware tilemap entries, and writes constants for palette, tile, and map offsets.

## Tool options

### `e16asm`

```text
Usage: e16asm [--base address] [-o output.bin] [--print-ast] file.e16
```

- `-o`, `--output`: choose the output binary path.
- `-b`, `--base`: set the base address. Defaults to `0x200000`.
- `--print-ast`: print parsed expressions before compiling.

### `e16emu`

```text
usage: e16emu [-s] [--load-address addr] [--scale n] program.bin
```

- `-s`: enable debugger mode.
- `--load-address`: choose where the binary is loaded.
- `--scale`: set the emulator display scale.

### `e16dis`

```text
Usage: e16dis [--base address] [--plain] [--no-color] [--no-pager] [--no-labels] [--no-bytes] file.bin
```

- `-b`, `--base`: set the base address. Defaults to `0x200000`.
- `--plain`: disable color and pager output.
- `--no-color`: disable ANSI color output.
- `--no-pager`: print directly instead of using a pager.
- `--no-labels`: hide generated labels.
- `--no-bytes`: hide instruction bytes.

### `e16img`

```text
Usage: e16img <image.png> [-o output.e16img]
```

- `-o`, `--output`: choose the converted image output path.

### `e16spritepack`

```text
Usage: e16spritepack [--yes] [--force] [-o output.e16spr] [--inc output.e16] <sprite.png>...
```

- `-o`, `--output`: choose the combined binary output path.
- `--inc`, `--include`: choose the generated `.e16` constants path.
- `-y`, `--yes`: skip the confirmation prompt.
- `--force`: combine sprites even when their opaque shapes differ.

### `e16tilemap`

```text
Usage: e16tilemap <background.png> [-o output.e16bg] [--inc output.e16] [--symbol NAME] [--palette n] [--wrap] [--no-flips]
```

- `-o`, `--output`: choose the background binary output path.
- `--inc`, `--include`: choose the generated `.e16` constants path.
- `--symbol`: choose the constant prefix.
- `--palette`: set the tilemap entry palette number from 0 to 15.
- `--wrap`: emit a layer control value with horizontal and vertical wrapping.
- `--no-flips`: disable flipped tile deduplication.

## Neovim plugin

The Neovim plugin lives in `nvim/e16.nvim` and provides:

- `*.e16` filetype detection
- syntax highlighting
- completions, hover, go-to-definition, document symbols, semantic tokens, and diagnostics through the included Python LSP server

With `lazy.nvim`:

```lua
{
  dir = "/path/to/E16/nvim/e16.nvim",
  ft = "e16",
  config = function()
    require("e16").setup()
  end,
}
```

See `nvim/e16.nvim/README.md` for more editor setup details.

## GitHub Actions

This repository includes workflows for:

- Building all CMake tools on Ubuntu.
- Checking the Neovim plugin's Python LSP server with `py_compile`.

## Repository status

This is an experimental toolchain. Interfaces and binary formats may change while the emulator, assembler, and editor tooling evolve together.
