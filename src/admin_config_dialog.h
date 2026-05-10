#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// ShowAdminConfigDialog — Win32 tabbed dialog for administering the web server.
//
// Three tabs:
//   1. Users        — CRUD + enable/disable, reset password, force-logout
//   2. Groups       — CRUD, add/remove members
//   3. Bindings     — Per-project group access + optional per-user folder
//
// All mutations call through WebUserStore; the server's active sessions are
// touched via the WebServer pointer for force-logout operations.
// ──────────────────────────────────────────────────────────────────────────────

#include "storage.h"
#include "web_server.h"
#include "web_user_store.h"

#include <windows.h>

// Returns true if any changes were made (caller may want to refresh UI).
bool ShowAdminConfigDialog(HWND          owner,
                           WebServer*    server,      // may be nullptr
                           WebUserStore* user_store,
                           AppStorage*   storage);
