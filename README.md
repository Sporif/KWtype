# KWtype

A cli tool that provides virtual keyboard input on KDE Plasma Wayland.

KWtype uses a privileged wayland protocol implemented only by KWin ([Fake Input](https://wayland.app/protocols/kde-fake-input)).

## Dependencies

- C++ compiler (GCC/Clang)
- Qt6
- KWayland
- xkbcommon

## Build and Install

```
meson setup --buildtype=release build
meson install -C build
```

## Usage

Run the following command:

```
kwtype <text>
```

Use `-s` or `--stdin` to read UTF-8 text from standard input instead:

```sh
printf 'Hello, world!\n' | kwtype --stdin
kwtype -s < text.txt
```

Input is read until EOF before typing begins. Whitespace and trailing newlines are
preserved. The stdin option cannot be combined with positional text, but can be
used with other options such as `--key-delay` and `--key-hold`.
