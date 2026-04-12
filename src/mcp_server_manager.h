#pragma once

#include "mcp_manager.h"

#include <windows.h>

#include <functional>
#include <string>

HWND CreateMcpServerManagerWindow(HWND owner, McpManager* manager, std::function<std::string()> active_project_id_provider);
