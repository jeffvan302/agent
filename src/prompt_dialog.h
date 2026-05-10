#pragma once

#include <windows.h>

#include <optional>
#include <string>

struct PromptOptions {
    std::wstring title;
    std::wstring label;
    std::wstring initial_text;
    bool multiline = false;
    int width = 460;
    int height = 180;
};

std::optional<std::wstring> ShowPromptDialog(HWND owner, const PromptOptions& options);
