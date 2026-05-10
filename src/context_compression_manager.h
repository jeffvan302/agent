#pragma once

#include "context_compression.h"
#include "types.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

HWND CreateContextCompressionManagerWindow(
    HWND owner,
    ContextCompressionService* compression_service,
    AppStorage* storage,
    std::function<std::vector<ProviderConfig>()> get_providers,
    std::function<void()> on_changed);
