#pragma once

#include "mcp_manager.h"
#include "rag_service.h"
#include "storage.h"
#include "types.h"

#include <windows.h>

#include <vector>

// Opens a modeless manager window for editing model tools.
// Returns the HWND of the created window (or nullptr on failure).
// The window is non-modal; the caller stores it and checks IsWindow() to avoid re-opening.
HWND OpenModelToolsManager(
    HWND owner,
    AppStorage* storage,
    const std::vector<ProviderConfig>& providers,
    McpManager* mcp_manager,
    RagService* rag_service);
