#pragma once

#include "rag_service.h"

#include <windows.h>

#include <functional>
#include <string>

HWND CreateRagServiceManagerWindow(HWND owner, RagService* rag_service, std::function<std::string()> active_project_id_provider);
