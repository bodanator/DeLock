# DeLock
Decided to make my code public for a lockdown browser bypass window. The window is designed to circumvent lockdown browser's security measures with a hidden window containing an LLM wrapper with the ability to scan the users screen. The user is also able to ask questions directly into chat window. This was originally private and for educational purposes only, however my personal account used for testing was flagged by lockdown themselves after the window triggered a GetForegroundWindow() check. The program has now been updated to bypass this check but will likely be blacklisted in the future. 

# Note
This program requires an active OpenAI API key to function. It is currently set up to use 6.1 Luna as this model contains the best vision to performance to price ratio. You can configure the model at will within the Config file inside the main window folder.

# Feature List
For the technically inclined I will provide a simple list of features the window has, as well as some of the bypasses I was able to implement during my time working on this. I may or may not release of the other details I discovered while working through lockdownbrowser binary. 
### Zero Focus Architecture
The window never becomes the foreground or active window. Built with `WS_EX_NOACTIVATE` — no application on the system will detect a focus change when the overlay is toggled. Input is captured through a low-level keyboard hook (`WH_KEYBOARD_LL`) that intercepts keystrokes system-wide without requiring window focus.

### Display Affinity
Uses `SetWindowDisplayAffinity` with `WDA_EXCLUDEFROMCAPTURE` (0x11) to make the window completely invisible to all screen capture methods — screenshots, screen recording, screen sharing, OBS, Discord, Zoom, and any other capture-based software. Falls back to `WDA_MONITOR` (0x01) on older Windows builds. Affinity is re-applied every 3 seconds via timer to survive DWM composition resets.

### Machine Derived Window Class
The window class name is generated at runtime from the machine's volume serial number (`TI{hash}`), making it impossible to detect via `FindWindowW` or `EnumWindows` using a known class name. Every machine produces a unique, non-deterministic class identifier.

### Process Disguise
The output binary is named `TextInputHost.exe`, matching a legitimate Windows system process. This avoids detection by process enumeration scanners that compare running process names against a blocklist.

### Vision API
Captures the entire screen as a compressed JPEG, base64-encodes it, and sends it to the OpenAI vision API. The overlay hides itself before capture and restores after. Useful for analyzing on screen content.

### OpenAI API Integration
- Full conversation history with rolling 20-message context window
- System prompt optimized for concise, direct answers
- Configurable model via `config.ini`
- Async API calls on a background thread (UI never blocks)
- WinHTTP with Mozilla user-agent string

### Configurable Keybinds
All hotkeys are rebindable through an in-app keybind panel:

| Default Bind | Action |
|---|---|
| `Ctrl + \`` | Toggle overlay visibility |
| `Ctrl + Shift + S` | Screen scan |
| `End` | Exit process |
| `Ctrl + Shift + End` | Self-destruct |

Keybind changes persist to `config.ini` under the `[keybinds]` section using `WritePrivateProfileStringA` (non-destructive append — does not touch other sections).

### Self Destruct
Triggered by hotkey, the self-destruct sequence:
1. Spawns a hidden PowerShell process
2. Overwrites the binary with zeroed bytes
3. Overwrites with `0xFF` bytes
4. Overwrites with random bytes
5. Truncates to zero length
6. Renames to a random GUID
7. Deletes the renamed file
8. Repeats for `config.ini`
9. Removes the containing folder

The main process exits immediately after spawning the shredder. Memory is wiped with `SecureZeroMemory` before exit.

### Keyboard Hook
All text input is handled by a `WH_KEYBOARD_LL` hook — no Win32 edit control, no `WM_CHAR`, no focus required. The hook supports:
- Full alphanumeric input with shift-modified symbols
- Cursor navigation (Left, Right, Home, End)
- Backspace, Delete
- Clipboard paste (Ctrl+V)
- Select-all clear (Ctrl+A)
- Enter to send
- Blinking cursor rendered via GDI
- Automatic hotkey passthrough (registered system hotkeys still fire)

The hook is installed when the overlay is shown and removed when hidden, minimizing system-wide keystroke interception to only the active window period.

### Layered Window Compositing
Uses `WS_EX_LAYERED` to ensure the entire window surface routes through DWM's layered composition pipeline. Combined with `WS_POPUP` (no `WS_THICKFRAME`), this eliminates the non-client frame leak that would otherwise allow DWM to expose window edges through capture.

---

## Build

### Requirements
- Windows 10 1903+ (for `WDA_EXCLUDEFROMCAPTURE` support)
- CMake 3.10+
- MSVC (Visual Studio 2019+ or Build Tools)

### Steps
```
cd ghost
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

The binary lands at `build/Release/TextInputHost.exe`.

### Linked Libraries
- `gdiplus` — JPEG encoding for screen capture
- `winhttp` — HTTPS API calls
- `shell32` — system integration
- `dwmapi` — DWM attributes
- `uxtheme` — theme support
- `ole32` — COM/stream for JPEG encoding
- `user32` / `gdi32` — core Win32

---

## Configuration

Create a `config.ini` next to the binary:

```ini
[api]
key=sk-your-openai-api-key-here
model=gpt-4o

[keybinds]
Toggle Window=Ctrl+`
Scan Screen=Ctrl+Shift+S
Exit=End
Self-Destruct=Ctrl+Shift+End
```

The API key can also be set via the `OPENAI_API_KEY` environment variable.

---

## Architecture

```
TextInputHost.exe
├── WinMain
│   ├── GDI+ init
│   ├── Config loader (INI parser)
│   ├── Font/brush creation
│   ├── Window class registration (machine-derived)
│   ├── Window creation (NOACTIVATE | TOOLWINDOW | TOPMOST | LAYERED)
│   ├── Display affinity application
│   ├── Hotkey registration
│   └── Message loop
├── WndProc
│   ├── WM_PAINT → double-buffered Paint()
│   ├── WM_HOTKEY → toggle/scan/exit/nuke
│   ├── WM_LBUTTONDOWN → hit testing (dots, scan, send, drag, keybinds)
│   ├── WM_MOUSEWHEEL → chat scroll
│   ├── TIH_WM_SEND → enqueue user message
│   └── TIH_WM_AI_DONE → receive AI response
├── LLKeyboardProc (WH_KEYBOARD_LL)
│   ├── Hotkey passthrough
│   ├── Keybind reassignment capture
│   ├── Text input (VKToChar)
│   ├── Navigation keys
│   └── Clipboard paste
├── AIThread (background)
│   ├── BuildPayload (JSON construction)
│   ├── CallAPI (WinHTTP POST)
│   └── PostMessage → TIH_WM_AI_DONE
└── CaptureScreen
    ├── Hide overlay
    ├── BitBlt full screen
    ├── JPEG encode via GDI+
    ├── Base64 encode
    └── Restore overlay
```

---

## Source

Single-file implementation: `main.cpp` (~1400 lines, no dependencies beyond Win32 and the C++ standard library).

---

## License

Do whatever you want with it.
