# VLC plugin: Windows SMTC integration

Plugin that integrates VLC Media Player with Windows System Media Transport Controls (SMTC)

![example](https://i.imgur.com/md0ProP.png)

## Requirements

- VLC 3.0.x (VLC 4 is not supported)
- Windows 10 Version 1607+ or Windows 11

## Installation

1. Download the plugin matching your VLC architecture from [GitHub Releases](https://github.com/spmn/vlc-win10smtc/releases)
    - *Note:* the plugin architecture must match vlc.exe, not Windows (eg: an x86 plugin does not work with x64 VLC)
2. Extract `libwin10smtc_plugin.dll` to `<path-to-VLC>\plugins\misc`
3. Restart VLC and navigate to Advanced Preferences -> Interface -> Control Interfaces
4. Check `Windows SMTC integration` and hit Save

## Build instructions

Official VLC build instructions recommend mingw. However, mingw doesn't support C++/WinRT, so for this plugin MSVC is used instead. In order to build this plugin you'll need: Visual Studio, Windows 10 SDK and VLC SDK:

1. Download the VLC SDK for your target architecture ([x64](https://get.videolan.org/vlc/3.0.23/win64/vlc-3.0.23-win64.7z), [x86](https://get.videolan.org/vlc/3.0.23/win32/vlc-3.0.23-win32.7z) or [arm64](https://get.videolan.org/vlc/3.0.23/winarm64/vlc-3.0.23-winarm64.7z)) and copy its `sdk` directory into the repository root. Then rename `sdk\lib` to match the target: `lib64` for x64, `lib` for x86, `libarm64` for arm64.
2. Load .sln file in Visual Studio
3. Select target architecture
4. Build solution
