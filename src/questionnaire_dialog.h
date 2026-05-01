#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

struct QuestionnaireOptions {
    std::wstring title = L"Question";
    std::wstring question;
    std::vector<std::wstring> labels;
    bool allow_multiple = false;
    int width = 460;
    int height = 240;
};

// Returns selected indices (0-based), or nullopt if cancelled.
std::optional<std::vector<int>> ShowQuestionnaireDialog(HWND owner, const QuestionnaireOptions& options);
