# Mouse Jiggler - C++ Win32 Version

C++ Win32 port of the original C# Mouse Jiggler application.

> **Original Project:** [arkane-systems/mousejiggler](https://github.com/arkane-systems/mousejiggler) by Alistair J R Young

## Features

- **Prevents screensaver activation** by periodically moving the mouse
- **Zen Mode**: Virtual mouse movement (system detects activity but pointer doesn't move)
- **System tray support**: Minimize to notification area
- **Configurable jiggle interval**: 1 to 10800 seconds (3 hours)
- **Settings persistence**: Saves preferences to INI file
- **Command-line options**: Start with specific settings
- **Single instance**: Prevents multiple instances from running

## Building

### Requirements

- Visual Studio 2022 (or 2019) with C++ Desktop Development workload
- Windows 10/11 SDK

### Build Steps

1. Open `MouseJiggler.vcxproj` in Visual Studio
2. Select your target configuration (Debug/Release) and platform (x86/x64)
3. Build the solution (F7 or Build > Build Solution)
4. The executable will be created in `bin\[Platform]\[Configuration]\MouseJiggler.exe`

### Command-Line Build

Using Visual Studio Developer Command Prompt:

```bash
# For x64 Release build
msbuild MouseJiggler.vcxproj /p:Configuration=Release /p:Platform=x64

# For x86 Release build
msbuild MouseJiggler.vcxproj /p:Configuration=Release /p:Platform=Win32
```

## Usage

### GUI Operation

1. Run `MouseJiggler.exe`
2. Check the **"Jiggling?"** checkbox to start/stop jiggling
3. Click **"Settings..."** to reveal configuration options:
   - **Minimize on startup**: Start minimized to system tray
   - **Zen jiggle?**: Enable virtual mouse movement (no visible pointer movement)
   - **Jiggle period**: Adjust the interval between jiggles (1-180 seconds via slider)
4. Click the **↓** button to minimize to system tray
5. Double-click the tray icon to restore the window

### Command-Line Options

```
Usage: MouseJiggler [options]

Options:
  -j, --jiggle               Start with jiggling enabled
  -m, --minimized            Start minimized
  -z, --zen                  Start with zen (invisible) jiggling enabled
  -s, --seconds <seconds>    Set number of seconds for the jiggle interval
  -?, -h, --help             Show help and usage information
```

**Examples:**

```bash
# Start jiggling immediately
MouseJiggler.exe -j

# Start minimized to tray with zen mode enabled
MouseJiggler.exe -m -z

# Start with 30 second interval and begin jiggling
MouseJiggler.exe -s 30 -j

# Combine options
MouseJiggler.exe -j -z -m -s 45
```

### System Tray

When minimized to the system tray, right-click the icon to:
- **Open**: Restore the main window
- **Start/Stop Jiggling**: Toggle jiggling on/off
- **Exit**: Close the application

## Settings Storage

Settings are saved to `MouseJiggler.ini` in the same directory as the executable.

**INI File Format:**
```ini
[Settings]
MinimizeOnStartup=0
ZenJiggle=0
JigglePeriod=60
```

## Technical Details

### Implementation

- **Pure Win32 API**: No external dependencies (except standard Windows libraries)
- **Dialog-based UI**: Uses Windows resource dialogs for the interface
- **SendInput API**: Generates mouse events via the Windows input system
- **Timer-based**: Uses WM_TIMER for periodic jiggling
- **Mutex for single instance**: Prevents multiple instances using named mutex

### File Structure

```
MouseJigglerCpp/
├── Main.cpp                    # Main application code
├── Resource.h                  # Resource ID definitions
├── MouseJiggler.rc             # Resource file (dialogs, icons)
├── MouseJiggler.vcxproj        # Visual Studio project
├── MouseJiggler.vcxproj.filters # VS project filters
├── icon.ico                    # Application icon
└── README.md                   # This file
```

### Key Differences from C# Version

1. **Settings Storage**: Uses INI files instead of .NET application settings
2. **UI Framework**: Native Win32 dialogs instead of Windows Forms
3. **No .NET Runtime**: Standalone executable with no dependencies
4. **Smaller Size**: ~50-100 KB vs ~1 MB for C# version
5. **No Command-Line Parser Library**: Custom argument parsing

## Original Project

This is a port of the C# Mouse Jiggler created by Alistair J R Young.

Original repository: https://github.com/arkane-systems/mousejiggler

## License

Same license as the original project.
