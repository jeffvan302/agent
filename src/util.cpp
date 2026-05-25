#include "util.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <mutex>
#include <filesystem>

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

std::string NormalizeProjectDescription(const std::string& value) {
    std::wstring description = Utf8ToWide(value);
    for (wchar_t& ch : description) {
        if (ch == L'\r' || ch == L'\n') {
            ch = L' ';
        }
    }
    description = TrimWide(description);
    if (description.size() > kMaxProjectDescriptionLength) {
        description.resize(kMaxProjectDescriptionLength);
        if (!description.empty() &&
            description.back() >= 0xD800 && description.back() <= 0xDBFF) {
            description.pop_back();
        }
    }
    return WideToUtf8(description);
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

namespace {
    std::filesystem::path g_log_path;
    std::mutex g_log_mutex;
    LogLevel g_log_level = LogLevel::Info;
    bool g_initialized = false;

    const char* LevelPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::Error: return "[ERROR]";
            case LogLevel::Warn:  return "[WARN] ";
            case LogLevel::Info:  return "[INFO] ";
            case LogLevel::Debug: return "[DEBUG]";
        }
        return "[????]";
    }
}  // namespace

void Logger::Initialize(const std::filesystem::path& log_dir, LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_initialized) return;

    g_log_level = level;
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    g_log_path = log_dir / "agent.log";

    std::ofstream ofs(g_log_path, std::ios::app);
    if (ofs.is_open()) {
        ofs << "\n=== Logger initialized at " << CurrentTimestampUtc() << " ===\n";
    }
    g_initialized = true;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_initialized) return;
    g_initialized = false;
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_initialized || g_log_path.empty()) return;
}

void Logger::LogImpl(LogLevel level, const std::string& context, const std::string& msg) {
    if (!g_initialized || level > g_log_level) return;

    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_path.empty()) {
        std::ofstream ofs(g_log_path, std::ios::app);
        if (ofs.is_open()) {
            ofs << CurrentTimestampUtc() << " "
                << LevelPrefix(level) << " ";
            if (!context.empty()) {
                ofs << "[" << context << "] ";
            }
            ofs << msg << "\n";
        }
    }
}

void Logger::Error(const std::string& msg) { LogImpl(LogLevel::Error, "", msg); }
void Logger::Warn(const std::string& msg) { LogImpl(LogLevel::Warn, "", msg); }
void Logger::Info(const std::string& msg) { LogImpl(LogLevel::Info, "", msg); }
void Logger::Debug(const std::string& msg) { LogImpl(LogLevel::Debug, "", msg); }

void Logger::Error(const std::wstring& msg) { Error(WideToUtf8(msg)); }
void Logger::Warn(const std::wstring& msg) { Warn(WideToUtf8(msg)); }
void Logger::Info(const std::wstring& msg) { Info(WideToUtf8(msg)); }
void Logger::Debug(const std::wstring& msg) { Debug(WideToUtf8(msg)); }

void Logger::Error(const std::string& context, const std::string& msg) { LogImpl(LogLevel::Error, context, msg); }
void Logger::Warn(const std::string& context, const std::string& msg) { LogImpl(LogLevel::Warn, context, msg); }
void Logger::Info(const std::string& context, const std::string& msg) { LogImpl(LogLevel::Info, context, msg); }
void Logger::Debug(const std::string& context, const std::string& msg) { LogImpl(LogLevel::Debug, context, msg); }
