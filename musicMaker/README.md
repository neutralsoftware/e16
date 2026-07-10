# E16 Music Maker

E16 Music Maker is an SDL3 six-channel tracker for every Ember APU voice: two pulse channels, triangle, noise, wavetable, and PCM. It previews the same waveform types locally and exports assembly that can run alongside a game.

## Build

```sh
cmake -S musicMaker -B musicMaker/build
cmake --build musicMaker/build
musicMaker/build/e16musicmaker
```

SDL3 is the only runtime dependency.

## Workflow

- Select a tone or wavetable channel and step, then click the one-octave keyboard or use `Z` through `M` and `Q` through `U` to enter notes.
- Use `.` or the Hold button to sustain the previous note into a step.
- Use Delete, Backspace, the Rest button, or right-click to clear a step.
- Use Space to start or stop the live preview.
- Adjust BPM, step division, swing, song length, volume, pan, pulse duty, and noise mode from the editor.
- Draw a 32-sample wavetable directly or start from sine, square, saw, triangle, and random presets.
- Record up to two seconds from the default microphone for the PCM channel, preview it, place triggers, choose one-shot or looping playback, and optionally trim leading silence automatically.
- Empty PCM steps let a one-shot finish naturally; use an explicit Stop event to end a looping sample.
- Drag across tracker cells to select a rectangular region. Shift-click or Shift+Arrow extends the selection; `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, and `Ctrl+A` copy, cut, paste, and select all. On macOS, the corresponding Command shortcuts work too.
- Save editable projects as `.e16music` and export game modules as `.e16`.

The MaxPages selector supports between 1 and 50 pages. Each page contains 16 steps, for a maximum song length of 800 steps.

## Exported module

The generated `.e16` file is designed to be included in a game source file:

```asm
.include "song.e16"

start:
    call SONG_music_play

main:
    wait
    call SONG_music_update
    bra main
```

The module starts with a branch guard, so including it before the game entry point does not execute the music functions during startup.

Every generated function, constant, state symbol, data label, and internal label is prefixed with the uppercase sanitized export filename. Exporting `mainTheme.e16`, for example, produces `MAINTHEME_music_play`, `MAINTHEME_music_play_once`, `MAINTHEME_music_update`, and `MAINTHEME_music_stop`.

`SONG_music_play` starts looping playback. `SONG_music_play_once` starts playback that stops after the final step. Both initialize all six APU channels, upload sample data, apply the first step, and return. `SONG_music_update` advances the sequencer and must be called once per subsequent 60 Hz game frame, including frames spent in blocking wait loops. `SONG_music_stop` silences all six music channels and disables sequencing. The editor preview uses the same 60 Hz step quantization, swing schedule, and channel attenuation as the exported sequencer, so its note boundaries and mix levels match the game output.

The generated module preserves `r0` and `r1`; its sample loader also preserves `r2`. It reserves eight bytes of Work RAM beginning at `0x000100` for its step, delay, playback, and loop states. Change the prefixed state-base constant near the top of the exported file if the game already uses that range.

Wavetable and PCM samples are emitted as `.byte` data inside the assembly module. The prefixed play function calls its prefixed sample loader, which copies the 32-byte wavetable to `0x060000` and the PCM recording to `0x060100` before playback starts. Prefixed constants for the data lengths, sample rate, and Audio RAM locations are emitted beside the code so they are easy to inspect or change.

PCM recordings are normalized unsigned 8-bit mono at 8 kHz and receive short fades at both ends. When Auto Trim is enabled, the recorder measures its initial noise floor, searches for a sustained signal above it, and keeps a short pre-roll before the detected attack. The two-second limit keeps the recording at or below 16,000 bytes, leaving ample space in the E16 Audio RAM region.

The export intentionally does not install an interrupt handler or take ownership of the interrupt vector table, so it can coexist with the game’s input, rendering, and timing code.
