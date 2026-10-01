// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: the native window (arpsid_standalone_x11.cpp on
// Linux, arpsid_standalone_win32.cpp on Windows).
#pragma once

namespace ArpSID::Standalone {

class App;

// Opens the main window with the app's toolbar and editor, runs the event
// loop until the window is closed, and returns the exit code. The window
// keeps the content's 1200 x 834 aspect; resizing zooms it.
int runWindow(App& app);

} // namespace ArpSID::Standalone
