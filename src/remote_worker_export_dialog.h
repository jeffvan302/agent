#pragma once

#include "storage.h"
#include "types.h"

#include <windows.h>
#include <filesystem>
#include <optional>
#include <vector>

// Show a modal dialog that lets the user pick providers to export, set
// server options, generate a shared secret + self-signed certificate,
// and save a single remote-provider-worker JSON file.
// Returns the saved path if the user completed the dialog.
std::optional<std::filesystem::path> ShowRemoteWorkerExportDialog(
    HWND owner,
    AppStorage* storage,
    const std::vector<ProviderConfig>& providers);
