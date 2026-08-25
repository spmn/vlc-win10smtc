# VLC plugin: Windows 10 SMTC integration

Plugin that integrates VLC Media Player with Windows 10 System Media Transport Controls (SMTC)

![example](https://i.imgur.com/md0ProP.png)

## Requirements

- VLC 3.0.x
- Windows 10 Version 1607+

## Installation

1. Get the plugin from [GitHub Releases](https://github.com/spmn/vlc-win10smtc/releases)
2. Copy `libwin10smtc_plugin.dll` to `<path-to-VLC>\plugins\misc`
    - *Note:* DLL architecture must match vlc.exe architecture (eg: x86 plugin is not compatible with x64 VLC)
3. Restart VLC and navigate to Advanced Preferences -> Interface -> Control Interfaces
4. Check `Windows 10 SMTC integration` and hit Save

## Build instructions

Official VLC build instructions recommend mingw. However, mingw doesn't support C++/WinRT, so for this plugin MSVC is used instead. In order to build this plugin you'll need: Visual Studio, Windows 10 SDK and VLC SDK:

1. Download the VLC SDK for your target architecture ([x64](https://get.videolan.org/vlc/3.0.23/win64/vlc-3.0.23-win64.7z), [x86](https://get.videolan.org/vlc/3.0.23/win32/vlc-3.0.23-win32.7z) or [arm64](https://get.videolan.org/vlc/3.0.23/winarm64/vlc-3.0.23-winarm64.7z)) and copy its `sdk` directory into the repository root. Then rename `sdk\lib` to match the target: `lib64` for x64, `lib` for x86, `libarm64` for arm64.
2. Load .sln file in Visual Studio
3. Select target architecture
4. Build solution
