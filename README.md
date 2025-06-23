# CS2 External Overlay Cheat

![Windows](https://img.shields.io/badge/platform-windows-blue.svg)
![Language](https://img.shields.io/badge/language-c/c++-blue.svg)
![Direct3D9](https://img.shields.io/badge/graphics-d3d9-lightgrey.svg)
![Status](https://img.shields.io/badge/status-experimental-orange.svg)

A lightweight, external ESP overlay for Counter-Strike 2, written in C and using Direct3D 9.

The esp works about as well as you would expect a from-scratch external to, originally wrote using C, I had to switch to C++ as GDI was very laggy. Most of the code is still just basic C.

You can use this if you want as a base for making cheats, or if you just want to use it. I don't really care. 
Please feel free to contribute, if you have improvments, fixes or the offsets have updated and you want to update them, feel free to. 😆

## Features

- External overlay
- ESP boxes
- Position interpolation
- Written in C, using Direct3D 9

## Requirements

- Windows 10/11
- Visual Studio or MSVC build tools
- Counter-Strike 2 running
- Run as Administrator

## Build

```sh
cl rendering.cpp /I. /link user32.lib gdi32.lib d3d9.lib
```

## Usage

1. Make sure the offsets are up to date. Or add your own offset downloading code.
2. Start Counter-Strike 2 in Fullscreen Windowed or Windowed.
3. Run the compiled overlay as Administrator.


