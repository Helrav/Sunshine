/**
 * @file src/platform/windows/display_window.h
 * @brief Optional single-window capture target selection for Windows.Graphics.Capture.
 *
 * This is the only new interface added to Sunshine for window capture. The capture backend
 * asks for a target window; if one is configured and found, it is captured instead of the
 * display. Everything else stays in display_window.cpp.
 */
#pragma once

// platform includes
#include <Windows.h>

namespace platf::window_capture {
  /**
   * @brief Resolve the window that should be captured, if any.
   *
   * The target is read from `window_capture.json` in Sunshine's config directory:
   *
   * @code{.json}
   * {
   *   "enabled": true,
   *   "process": "P5R.exe",
   *   "title": "",
   *   "wait_seconds": 20
   * }
   * @endcode
   *
   * `process` is matched against the executable name of the window's owner (case-insensitive).
   * `title` is matched as a case-insensitive substring when `process` is empty. `wait_seconds`
   * (default 20, capped at 120) is how long Sunshine waits for the window to appear before
   * giving up, which covers applications launched by the same stream request. If both `process`
   * and `title` are empty, the file is missing, `enabled` is false, or nothing matches, this
   * returns nullptr and capture continues on the display as before.
   *
   * @return Window handle to capture, or nullptr to use display capture.
   */
  HWND target_window();
}  // namespace platf::window_capture
