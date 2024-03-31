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
