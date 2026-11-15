# Single-window capture for Sunshine (Windows)

This fork adds the ability to stream **one application window** instead of a whole display.
It is a small patch on top of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine)
that reuses the existing Windows.Graphics.Capture (`wgc`) backend and changes only the
capture target: a window instead of a monitor.

Everything else — encoding, input, audio, pairing, web UI — is stock Sunshine.

## What changed

| File | Change |
|---|---|
| `src/platform/windows/display_window.{h,cpp}` | **New.** Window lookup, config file, logging. |
| `src/platform/windows/display_wgc.cpp` | Capture item is created with `CreateForWindow` when a target is configured, otherwise `CreateForMonitor` exactly as upstream. |
| `cmake/compile_definitions/windows.cmake` | Registers the new files. |

That is the whole patch: two new files plus three lines in two existing files. See
`PATCH.md` for the exact diff and the update procedure.

## Requirements

- Windows 10 1903+ / Windows 11
- Sunshine built from this fork (`wgc` capture support is already part of upstream)
- The app must run in **windowed or borderless windowed** mode — exclusive fullscreen
  windows cannot be captured by Windows.Graphics.Capture
- Sunshine must run **in your user session**, not as a service (`SunshineService`).
  WGC is not compatible with service mode; this is an upstream limitation.

## Setup

1. Build/install this fork.
2. Enable the WGC capture backend in `config/sunshine.conf`:

   ```ini
   capture = wgc
   ```

3. Stop and disable the Sunshine service, then start Sunshine in your session:

   ```powershell
   Stop-Service SunshineService
   Set-Service SunshineService -StartupType Disabled
   & 'C:\Program Files\Sunshine\sunshine.exe'
   ```

   Use a logon task or shortcut if you want it to start automatically.

4. Describe the window to capture in `config/window_capture.json` (next to `apps.json`):

   ```json
   {
     "enabled": true,
     "process": "P5R.exe",
     "title": ""
   }
   ```

   - `process` — executable name of the app, case-insensitive (preferred).
   - `title` — window title substring, used when `process` is empty.
   - `enabled` — set to `false` to return to display capture without deleting the file.

   The file is read when a stream starts, so changes apply to the next session. Creating it
   in `C:\Program Files\Sunshine\config` needs administrator rights.

5. Start a stream from Moonlight. The log shows the selected window:

   ```
   Info: Window capture: selected [Persona 5 Royal] (1920x1080)
   ```

If no window matches, Sunshine logs a warning and falls back to display capture, so a
bad config never leaves you with a black stream.

## Behaviour and limits

- The largest visible top-level window owned by `process` is captured. Splash screens and
  helper windows are ignored if the main window is bigger.
- Minimized windows, UAC/secure desktop, and DRM-protected surfaces are not captured.
- Moving the window between monitors is fine; resizing may need a stream restart (v1).
- The captured size follows the window, so Moonlight's resolution is negotiated against
  the window rather than the monitor.
- Mouse mapping currently assumes the window starts at the display origin. Absolute input
  may be offset for windows that are not at `0,0`; a fix for this is planned.

## Relationship to upstream

This is not a plugin: Sunshine has no capture plugin interface, so window capture must live
inside the binary. The patch is intentionally narrow so that merging a new upstream release
is a one-file conflict at worst. `PATCH.md` documents the exact anchor line and how to
re-apply the change if upstream reformats it.
