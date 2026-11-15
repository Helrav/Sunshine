# Patch notes and update procedure

## Surface area

Two new files, two existing files touched:

```
src/platform/windows/display_window.h      (new)
src/platform/windows/display_window.cpp    (new)
src/platform/windows/display_wgc.cpp       (+6 / -1)
cmake/compile_definitions/windows.cmake    (+2)
```

No Sunshine config structures, web UI, or IPC are modified. The target is read from its own
file (`config/window_capture.json`), so upstream's config parser is untouched.

## The anchor

The only behavioural change is at the capture item creation in
`src/platform/windows/display_wgc.cpp`:

```cpp
auto monitor_factory = winrt::get_activation_factory<winrt::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
if (monitor_factory == nullptr || FAILED(status = monitor_factory->CreateForMonitor(...))) {
```

becomes

```cpp
auto monitor_factory = winrt::get_activation_factory<winrt::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
auto window_hwnd = platf::window_capture::target_window();
auto window_item = window_hwnd != nullptr && monitor_factory != nullptr &&
                   SUCCEEDED(status = monitor_factory->CreateForWindow(window_hwnd, ...));
if (!window_item && (monitor_factory == nullptr || FAILED(status = monitor_factory->CreateForMonitor(...)))) {
```

History of that line (checked 2026-11-15):

| Date | Commit | Change |
|---|---|---|
| 2024-05-28 | `287ac4c` | introduced with the WGC backend |
| 2025-01-20 | `c242042` | clang-format only |
| 2026-07-28 | `965c91a` | line wrapping only |

No semantic change in 2.5 years. Both later edits were formatting, which is why this fork
keeps the change as one short block rather than a context patch file.

## Updating to a new Sunshine release

```powershell
cd C:\Users\Henry\Documents\sunshine-window-capture
git fetch https://github.com/LizardByte/Sunshine.git <new-tag>
git merge FETCH_HEAD
```

Expected outcomes:

- **Clean merge** — nothing to do.
- **Conflict only in `display_wgc.cpp`** — keep the upstream lines and re-add the three
  lines above around the new `CreateForMonitor` call.
- **Upstream removed/renamed `CreateForMonitor`** — window capture needs a new hook point;
  the build will still compile and behave as stock Sunshine if `target_window()` is unused.

After merging, push and let CI produce a new binary.

## Verifying the anchor still exists

```powershell
git grep -n "CreateForMonitor" -- src/platform/windows/display_wgc.cpp
```

If that returns nothing, the patch needs revisiting before updating.
