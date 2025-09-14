# holo.c

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A `donut.c` inspired C program that renders rotating 3D text as retro-futuristic 14-segment ASCII art in your terminal.

It takes any text you provide—or defaults to showing the current date—and spins it around in 3D, complete with adjustable lighting, speed, and style.

![holo.c in action](https://raw.githubusercontent.com/lvrcz/holo.c/refs/heads/main/holo_c.gif)


## Features

*   **Rotating 3D Text:** Renders text in a smooth, rotating 3D animation.
*   **14-Segment Display:** Uses a classic 14-segment model for a cool, retro look.
*   **Date & Time Mode:** By default, it runs as a live clock display.
*   **Highly Customizable:** Tweak rotation speed, character dimensions, tilt, lighting, color palette, and more!
*   **Cross-Platform:** Compiles and runs on Linux, macOS, and Windows.
*   **Zero Dependencies:** All you need is a C compiler. No external libraries required.
*   **Responsive:** Automatically adjusts to your terminal's window size.

## Getting Started

### Compilation

`holo.c` is a single file and has no dependencies, so compiling it is simple.

**On Linux or macOS:**
You'll need `gcc` or `clang`. The `-lm` flag is important to link the math library.
```bash
gcc -o holo holo.c -lm
```

**On Windows:**
Using a compiler from a toolchain like MinGW-w64 is the easiest way.
```bash
gcc -o holo.exe holo.c -lm
```

### Usage

Run it without any arguments to see the default date/time display:
```bash
./holo
```

Or, give it some text to display:
```bash
./holo HELLO WORLD
```

You can customize the animation with several flags. For a full list, run:
```bash
./holo -?
```

<details>
<summary><b>Click to see all options</b></summary>

```
Usage: ./holo [options] [TEXT TO DISPLAY...]
If no text is provided, the current date and time are displayed by default.

Animation & Geometry:
 -a <val>   A-axis (pitch) speed. Default: 0.04
 -b <val>   B-axis (yaw) speed. Default: 0.02
 -s <val>   Set both speeds (a=val, b=val/2).
 -w <val>   Character width. Default: 8.0
 -h <val>   Character height. Default: 12.0
 -S <val>   Character spacing multiplier. Default: 1.50
 -t <val>   Italic/tilt factor. Default: 0.3
 -z <val>   Manual zoom, overrides auto-sizing.

Rendering & Appearance:
 -W <val>   Segment width (fatness). Default: 1.75
 -T <val>   Segment thickness (depth). Default: 1.75
 -p <val>   Pointy end length. Default: 0.85
 -d <val>   Drawing density (step rate). Smaller is denser. Default: 0.1
 -L <x,y>   Light vector (no spaces). Default: 0.3,0.7
 -c <val>   Shading contrast. Default: 15.0
 -P <str>   Shading character palette. Default: ".,-~:;=!*#$@"
 -f <fmt>   Set the date/time format (strftime). Default: "%H:%M"
            Examples: "%Y-%m-%d" (date), "%I:%M %p" (12h), "%Y-%m-%d %H:%M" (both)

 -?         Display this help message.
```
</details>

## Examples

#### A faster, more slanted look
```bash
./holo -s 0.08 -t -0.6 "SLANTED"
```

#### A custom 12-hour clock format without the date and with a reverse palette
```bash
./holo -f "%I:%M %p" -P "$EFLlv!;,."
```

#### A chunky, high-contrast display
```bash
./holo -W 2.5 -T 2.5 -c 30 -P ".-=#@" "CHUNKY"
```

## Inspiration & Credits

This project would not exist without the brilliant work of others. It stands on the shoulders of giants:

1.  **Donut Math:** The core 3D projection and ASCII rendering logic is heavily based on the principles explained in Andy Sloane's article ["Donut math: how donut.c works"](https://www.a1k0n.net/2011/07/20/donut-math.html).

2.  **14-Segment Font Data:** The bit-packed font data for the characters was adapted from Dave Madison's awesome [LED-Segment-ASCII library](https://github.com/dmadison/LED-Segment-ASCII/).

## License

This project is licensed under the **MIT License**. See the `LICENSE` file for details. Feel free to fork it, play with it, and make it your own!
