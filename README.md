# ClownMDEmu-X11

Minimum viable product for X11 systems, using [Clownacy's ClownMDEmu core](https://github.com/Clownacy/clownmdemu-core).

What you get:
- Sega Mega Drive and Mega-CD emulation
- Audio support (via PulseAudio on Linux or sndio on OpenBSD)
- Region autodetection (for cartridges only)

What you **don't** get:
- A pretty UI
- Save support, whether as save states or backup RAM
- Remappable controls

## Building

``` bash
$ git clone https://github.com/WinFutureX/clownmdemu-x11
$ cd clownmdemu-x11
$ git submodule update --init --recursive
$ make # or gmake on OpenBSD
```

## Running

``` bash
$ ./clownmdemu FILE
```

The file in question can be a ROM or a BIN/CUE disc image.
Optionally, specifying `-r` followed by `J`, `U` or `E` sets the region to Japan, US or Europe respectively.

## Controls

| Emulated console | Host system    |
| ---------------- | -------------- |
| Up               | Up             |
| Down             | Down           |
| Left             | Left           |
| Right            | Right          |
| X                | Q              |
| Y                | W              |
| Z                | E              |
| A                | A              |
| B                | S              |
| C                | D              |
| Start            | Return / Enter |
| Mode             | F              |
| Soft reset       | Tab            |
| Quit             | Esc            |

## Licence

This app itself is licensed under GPLv3 (see `LICENSE.txt`), while the emulator core and associated libraries are licensed under AGPLv3 (see `common/LICENCE.txt`).
