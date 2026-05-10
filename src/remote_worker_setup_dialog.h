#pragma once

#include "storage.h"
#include "types.h"

#include <windows.h>
#include <filesystem>
#include <optional>
#include <vector>

// Opens a setup dialog for the generic Remote Provider Worker.
// The user can:
//   - Load / Save a remote-worker JSON (same format as --remote-worker)
//   - Set bind address, HTTPS port, shared secret, and certificate
//   - Select which providers / models to export (with checkboxes)
//   - Binding providers automatically include their target models
// The dialog reads provider configs and auth records from storage.
void ShowRemoteOllamaSetupDialog(
    HWND owner,
    AppStorage* storage,
    std::vector<ProviderConfig>* providers);
