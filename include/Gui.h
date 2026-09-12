/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <functional>
#include <string>

#if defined(__linux__)

namespace Gui {

/// Returns true when the GUI window is active (display present and --no-gui not set).
bool IsEnabled();

/// Show the GTK4 status window and run @p work on a background thread.
/// Blocks until Shutdown() is called. Falls back to calling work() directly
/// when no display server is detected (headless / SSH).
void Run(int argc, const char** argv, std::function<void()> work);

/// Update the status text shown in the window (thread-safe).
void SetStatus(const std::string& message);

/// Set a determinate progress fraction [0.0, 1.0].
/// Pass a negative value to switch to indeterminate pulsing (thread-safe).
void SetProgress(double fraction);

/// Show a modal error dialog and block until the user dismisses it (thread-safe).
void ShowError(const std::string& title, const std::string& message);

/// After the game launches: hide the status window and show a compact
/// always-on-top floating indicator with a Stop button. Thread-safe.
void ShowRunningIndicator();

/// Signal the GTK main loop to quit (thread-safe). Call when launcher work is done.
void Shutdown();

} // namespace Gui

#else

// Stubs for non-Linux platforms — entirely inlined, zero overhead.
namespace Gui {
    inline bool IsEnabled()                                        { return false; }
    inline void Run(int, const char**, std::function<void()> work) { work(); }
    inline void SetStatus(const std::string&)                      {}
    inline void SetProgress(double)                                {}
    inline void ShowError(const std::string&, const std::string&)  {}
    inline void ShowRunningIndicator()                             {}
    inline void Shutdown()                                         {}
}

#endif // defined(__linux__)
