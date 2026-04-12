#include "util.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace {
std::string TrimAscii(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !is_space(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

std::wstring TrimWideImpl(std::wstring value) {
    auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](wchar_t ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](wchar_t ch) { return !is_space(ch); }).base(), value.end());
    return value;
}
}  // namespace

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string Trim(const std::string& value) {
    return TrimAscii(value);
}

std::wstring TrimWide(const std::wstring& value) {
    return TrimWideImpl(value);
}

std::string MakeId(const std::string& prefix) {
    static std::atomic_uint64_t counter = 0;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "_" + std::to_string(millis) + "_" + std::to_string(++counter);
}

std::string CurrentTimestampUtc() {
    SYSTEMTIME st{};
    GetSystemTime(&st);

    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << st.wYear << "-"
           << std::setw(2) << st.wMonth << "-"
           << std::setw(2) << st.wDay << "T"
           << std::setw(2) << st.wHour << ":"
           << std::setw(2) << st.wMinute << ":"
           << std::setw(2) << st.wSecond << "Z";
    return stream.str();
}

std::wstring GetWindowTextString(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetWindowTextString(HWND hwnd, const std::wstring& value) {
    SetWindowTextW(hwnd, value.c_str());
}

void CenterWindowToOwner(HWND hwnd, HWND owner) {
    RECT target{};
    RECT owner_rect{};

    GetWindowRect(hwnd, &target);
    if (owner && GetWindowRect(owner, &owner_rect)) {
        const int width = target.right - target.left;
        const int height = target.bottom - target.top;
        const int owner_width = owner_rect.right - owner_rect.left;
        const int owner_height = owner_rect.bottom - owner_rect.top;
        const int x = owner_rect.left + (owner_width - width) / 2;
        const int y = owner_rect.top + (owner_height - height) / 2;
        SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        return;
    }

    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const int width = target.right - target.left;
    const int height = target.bottom - target.top;
    SetWindowPos(hwnd, nullptr, (screen_width - width) / 2, (screen_height - height) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

std::optional<int> ParseInt(const std::wstring& value) {
    const std::wstring trimmed = TrimWide(value);
    try {
        size_t consumed = 0;
        const int result = std::stoi(trimmed, &consumed);
        if (consumed == trimmed.size()) {
            return result;
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<double> ParseDouble(const std::wstring& value) {
    const std::wstring trimmed = TrimWide(value);
    try {
        size_t consumed = 0;
        const double result = std::stod(trimmed, &consumed);
        if (consumed == trimmed.size()) {
            return result;
        }
    } catch (...) {
    }
    return std::nullopt;
}
