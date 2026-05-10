#pragma once

#include "storage.h"
#include "types.h"

#include <windows.h>

#include <functional>
#include <vector>

HWND CreateProviderManagerWindow(HWND owner, AppStorage* storage, std::vector<ProviderConfig>* providers, std::function<void()> on_changed);
