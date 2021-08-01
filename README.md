# VLC plugin: Windows 10 SMTC integration

Plugin that integrates VLC Media Player with Windows 10 System Media Transport Controls (SMTC)

![example](https://i.imgur.com/md0ProP.png)

## Requirements

- VLC 3.0.x
- Windows 10 Version 1607+

## Installation

1. Copy `libwin10smtc_plugin.dll` to `<path-to-VLC>\plugins\misc`
    - *Note:* DLL architecture must match vlc.exe architecture (eg: x86 plugin is not compatible with x64 VLC)
2. Restart VLC and navigate to Advanced Preferences -> Interface -> Control Interfaces
3. Check `Windows 10 SMTC integration` and hit Save

## Build instructions

Official VLC build instructions recommend mingw. However, mingw doesn't support C++/WinRT, so for this plugin MSVC is used instead. In order to build this plugin you'll need: Visual Studio, Windows 10 SDK and VLC SDK:

1. Obtain VLC SDK from [here (x64)](https://get.videolan.org/vlc/3.0.16/win64/vlc-3.0.16-win64.7z) or [here (x86)](https://get.videolan.org/vlc/3.0.16/win32/vlc-3.0.16-win32.7z) and copy `sdk` directory into the root directory of the repository. You may need to rename `lib` to `lib64` for 64-bit builds.
2. Load .sln file in Visual Studio
3. Select as target architecture
4. Build solution
