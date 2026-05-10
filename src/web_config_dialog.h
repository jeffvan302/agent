#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// ShowWebConfigDialog — Win32 dialog for configuring and controlling the
// embedded web server.
//
// Shows current server status (running / stopped + active session count),
// Start/Stop toggle, and all WebServerConfig fields.  Save applies and
// persists settings immediately; OK applies settings and closes the dialog.
// ──────────────────────────────────────────────────────────────────────────────

#include "web_server.h"

#include <filesystem>
#include <windows.h>

// Returns true if settings were saved/applied in the dialog.
// Returns false on Cancel or window close without saving.
//
// If server != nullptr the dialog shows live status and the Start/Stop button
// works in real time.  Passing nullptr is allowed (e.g. for unit tests /
// offline preview) — Start/Stop will be disabled.
bool ShowWebConfigDialog(HWND                        owner,
                         WebServer*                  server,   // may be nullptr
                         WebServerConfig*            config,
                         const RuntimePaths&         runtime_paths);

// Backward-compatible overload for callers passing a single app-root path.
inline bool ShowWebConfigDialog(HWND                        owner,
                         WebServer*                  server,
                         WebServerConfig*            config,
                         std::filesystem::path       app_root)
{
    const RuntimePaths rt{app_root, app_root / ".config", app_root / ".data", app_root / ".log"};
    return ShowWebConfigDialog(owner, server, config, rt);
}
