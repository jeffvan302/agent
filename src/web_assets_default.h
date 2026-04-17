#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// Default web asset content, embedded as string literals.
// Used by WebServer::EnsureDefaultWebAssets() to bootstrap a fresh web_root.
// Files are written only when they do NOT already exist — never overwritten.
// ──────────────────────────────────────────────────────────────────────────────

namespace DefaultWebAssets {
    extern const char kIndexHtml[];
    extern const char kLoginHtml[];
    extern const char kChangePasswordHtml[];
    extern const char kBaseCss[];
    extern const char kLoginJs[];
    extern const char kChangePasswordJs[];
    extern const char kAppJs[];
    extern const char kThemeDefaultCss[];
    extern const char kThemeDefaultJson[];
    extern const char kThemeDarkCss[];
    extern const char kThemeDarkJson[];
}
