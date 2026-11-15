/**
 * @file src/platform/windows/display_window.cpp
 * @brief Window lookup for optional single-window capture.
 *
 * Kept deliberately self-contained so that upstream updates only ever need the call site in
 * display_wgc.cpp to stay compatible. No Sunshine configuration structures are modified: the
 * target lives in its own file next to `apps.json`.
 */

// standard includes
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// platform includes
#include <Windows.h>
#include <TlHelp32.h>

// local includes
#include "display_window.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/platform/windows/utf_utils.h"

// third-party includes
#include <nlohmann/json.hpp>

namespace platf {
  using namespace std::literals;
}

namespace platf::window_capture {
  namespace {
    /**
     * @brief Window selection parameters read from `window_capture.json`.
     */
    struct target_t {
      std::string process;  ///< Executable name to match, e.g. `P5R.exe`.
      std::string title;  ///< Window title substring, used when `process` is empty.
      int wait_ms {20000};  ///< How long to wait for the window to appear.
    };

    /**
     * @brief State carried through the `EnumWindows` callback.
     */
    struct search_t {
      std::string process;  ///< Lowercase executable name.
      std::string title;  ///< Lowercase title substring.
      HWND best {nullptr};  ///< Largest matching window seen so far.
      long long best_area {0};  ///< Area of `best`, in pixels.
    };

    /**
     * @brief Lowercase an ASCII string without touching the current locale.
     *
     * @param value Input string.
     * @return Lowercased copy.
     */
    std::string lower_ascii(std::string value) {
      for (auto &c : value) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      return value;
    }

    /**
     * @brief Read a window's title as UTF-16.
     *
     * @param hwnd Window handle.
     * @return Window title, empty when unavailable.
     */
    std::wstring window_title(HWND hwnd) {
      const int length = GetWindowTextLengthW(hwnd);
      if (length <= 0) {
        return {};
      }
      std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
      const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
      if (copied <= 0) {
        return {};
      }
      title.resize(static_cast<std::size_t>(copied));
      return title;
    }

    /**
     * @brief Get the lowercase executable name that owns a window.
     *
     * @param hwnd Window handle.
     * @return File name such as `p5r.exe`, or an empty string when it cannot be resolved.
     */
    std::string process_name(HWND hwnd) {
      DWORD pid = 0;
      GetWindowThreadProcessId(hwnd, &pid);
      if (pid == 0) {
        return {};
      }

      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (process == nullptr) {
        return {};
      }

      std::vector<wchar_t> buffer(32768);
      DWORD size = static_cast<DWORD>(buffer.size());
      const bool ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &size) != 0;
      CloseHandle(process);
      if (!ok) {
        return {};
      }

      const std::filesystem::path path {std::wstring {buffer.data(), size}};
      return lower_ascii(utf_utils::to_utf8(path.filename().wstring()));
    }

    /**
     * @brief Check whether a process with the given lowercase executable name is running.
     *
     * Used to avoid waiting for a window that can never appear because the application was
     * never started (for example when a different Moonlight entry is launched).
     *
     * @param name Lowercase executable name, e.g. `p5r.exe`.
     * @return True when such a process exists, false otherwise. Fails open on snapshot errors.
     */
    bool process_running(const std::string &name) {
      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snapshot == INVALID_HANDLE_VALUE) {
        return true;
      }

      PROCESSENTRY32W entry {};
      entry.dwSize = static_cast<DWORD>(sizeof(entry));

      bool found = false;
      if (Process32FirstW(snapshot, &entry)) {
        do {
          if (lower_ascii(utf_utils::to_utf8(entry.szExeFile)) == name) {
            found = true;
            break;
          }
        } while (Process32NextW(snapshot, &entry));
      }

      CloseHandle(snapshot);
      return found;
    }

    /**
     * @brief Check whether a window is a plausible capture target.
     *
     * @param hwnd Window handle.
     * @return True when the window is visible, not minimized, and has a usable size.
     */
    bool is_candidate(HWND hwnd) {
      if (hwnd == nullptr || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return false;
      }

      const auto extended_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
      if ((extended_style & WS_EX_TOOLWINDOW) != 0 && (extended_style & WS_EX_APPWINDOW) == 0) {
        return false;
      }

      RECT rect {};
      if (!GetWindowRect(hwnd, &rect)) {
        return false;
      }
      return rect.right > rect.left && rect.bottom > rect.top;
    }

    /**
     * @brief `EnumWindows` callback that keeps the largest matching candidate.
     *
     * @param hwnd Enumerated window.
     * @param lparam Pointer to `search_t`.
     * @return TRUE to continue enumeration.
     */
    BOOL CALLBACK enum_windows(HWND hwnd, LPARAM lparam) {
      auto &search = *reinterpret_cast<search_t *>(lparam);

      if (!is_candidate(hwnd)) {
        return TRUE;
      }

      if (!search.process.empty()) {
        if (process_name(hwnd) != search.process) {
          return TRUE;
        }
      } else {
        const auto title = window_title(hwnd);
        if (title.empty()) {
          return TRUE;
        }
        if (lower_ascii(utf_utils::to_utf8(title)).find(search.title) == std::string::npos) {
          return TRUE;
        }
      }

      RECT rect {};
      if (!GetWindowRect(hwnd, &rect)) {
        return TRUE;
      }

      const long long area = static_cast<long long>(rect.right - rect.left) * static_cast<long long>(rect.bottom - rect.top);
      if (area > search.best_area) {
        search.best_area = area;
        search.best = hwnd;
      }
      return TRUE;
    }

    /**
     * @brief Load the capture target from `window_capture.json`.
     *
     * @return Parsed target, or nullopt when window capture is not requested.
     */
    std::optional<target_t> load_target() {
      std::error_code error;
      const std::filesystem::path path = platf::appdata() / "window_capture.json";
      if (!std::filesystem::exists(path, error)) {
        return std::nullopt;
      }

      std::ifstream file {path};
      if (!file) {
        BOOST_LOG(warning) << "Window capture: could not open ["sv << path.string() << ']';
        return std::nullopt;
      }

      try {
        nlohmann::json json;
        file >> json;

        if (!json.value("enabled", true)) {
          return std::nullopt;
        }

        target_t target {
          json.value("process", std::string {}),
          json.value("title", std::string {}),
        };

        if (target.process.empty() && target.title.empty()) {
          BOOST_LOG(warning) << "Window capture: window_capture.json has no 'process' or 'title' set"sv;
          return std::nullopt;
        }

        const int wait_seconds = json.value("wait_seconds", 20);
        if (wait_seconds > 0) {
          target.wait_ms = wait_seconds > 120 ? 120000 : wait_seconds * 1000;
        } else {
          target.wait_ms = 0;
        }

        return target;
      } catch (const std::exception &e) {
        BOOST_LOG(warning) << "Window capture: invalid window_capture.json: "sv << e.what();
        return std::nullopt;
      }
    }
  }  // namespace

  HWND target_window() {
    const auto target = load_target();
    if (!target) {
      return nullptr;
    }

    search_t search;
    search.process = lower_ascii(target->process);
    search.title = lower_ascii(target->title);

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(target->wait_ms);
    // Give a just-launched application a moment to appear before assuming it will never start.
    const auto process_grace = started + std::chrono::seconds(2);
    bool logged_wait {false};

    while (true) {
      search.best = nullptr;
      search.best_area = 0;
      EnumWindows(enum_windows, reinterpret_cast<LPARAM>(&search));

      if (search.best != nullptr) {
        RECT rect {};
        GetWindowRect(search.best, &rect);
        BOOST_LOG(info) << "Window capture: selected ["sv << utf_utils::to_utf8(window_title(search.best))
                        << "] ("sv << (rect.right - rect.left) << 'x' << (rect.bottom - rect.top) << ')';
        return search.best;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        break;
      }
      if (!search.process.empty() && now > process_grace && !process_running(search.process)) {
        break;
      }

      if (!logged_wait) {
        BOOST_LOG(info) << "Window capture: waiting up to "sv << (target->wait_ms / 1000)
                        << "s for a window matching process ["sv << target->process
                        << "] title ["sv << target->title << ']';
        logged_wait = true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    BOOST_LOG(warning) << "Window capture: no window matched process ["sv << target->process
                       << "] title ["sv << target->title << "], falling back to display capture"sv;
    return nullptr;
  }
}  // namespace platf::window_capture
