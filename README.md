# Sparkborne

A Win32 startup program manager for Windows RT-style environments.

## Features

- Lists startup entries from:
  - `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
  - `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`
  - `shell:startup` (shortcut files)
- Enables/disables startup items:
  - Shortcuts: `.lnk` ⇄ `.dis`
  - Registry values: move between `Run` and `-Run`
- Supports silent startup relay mode with `-startup` or `/startup`.
- Includes menu extension entry: **Help → More things made by me...**
  - Opens an embedded IE WebBrowser window to `https://www.bingtangxh.moe/works`
- Localized UI strings via MUI-style multilingual resources (English + Simplified Chinese)

## Build

Open `Sparkborne.sln` with Visual Studio 2022 and build `Sparkborne`.
