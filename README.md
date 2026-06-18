# Sparkborne

A Win32 startup program manager for Windows RT-style environments.

## Features

- Lists startup entries from:
  - `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
  - `HKLM\Software\Microsoft\Windows\CurrentVersion\Run`
  - `shell:startup` (shortcut files)
- Displays startup entries in a left-side scrollable list, with detail panel on the right.
- Supports per-item actions:
  - Shortcuts: `.lnk` ⇄ `.dis`
  - Registry values: move between `Run` and `-Run`
  - Delete selected startup item
  - Launch selected startup item immediately
- Supports silent startup relay mode with `-startup` or `/startup`.
- Includes menu extension entry: **Help → More things made by me...**
  - Opens an embedded IE WebBrowser window to `https://www.bingtangxh.moe/works`
- Localized UI strings via MUI-style multilingual resources (English + Simplified Chinese)

## Build

Open `Sparkborne.sln` with Visual Studio 2022 and build `Sparkborne`.
