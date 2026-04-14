#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// ShowWebConfigDialog — Win32 dialog for configuring and controlling the
// embedded web server.
//
// Shows current server status (running / stopped + active session count),
// Start/Stop toggle, and all WebServerConfig fields.  On OK the caller
// receives a new config; if the server was running before OK is clicked it is
// restarted with the new config automatically.
// ──────────────────────────────────────────────────────────────────────────────

#include "web_server.h"

#include <filesystem>
#include <windows.h>

// Returns true if the user pressed OK (and config was potentially updated).
// Returns false on Cancel or window close.
//
// If server != nullptr the dialog shows live status and the Start/Stop button
// works in real time.  Passing nullptr is allowed (e.g. for unit tests /
// offline preview) — Start/Stop will be disabled.
bool ShowWebConfigDialog(HWND                        owner,
                         WebServer*                  server,   // may be nullptr
                         WebServerConfig*            config,
                         const std::filesystem::path app_root);
