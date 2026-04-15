#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <filesystem>

std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);

std::string Trim(const std::string& value);
std::wstring TrimWide(const std::wstring& value);

std::string MakeId(const std::string& prefix);
std::string CurrentTimestampUtc();

std::wstring GetWindowTextString(HWND hwnd);
void SetWindowTextString(HWND hwnd, const std::wstring& value);
void CenterWindowToOwner(HWND hwnd, HWND owner);

std::optional<int> ParseInt(const std::wstring& value);
std::optional<double> ParseDouble(const std::wstring& value);

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

class Logger {
public:
    static void Initialize(const std::filesystem::path& log_dir, LogLevel level = LogLevel::Info);
    static void Shutdown();

    static void Error(const std::string& msg);
    static void Warn(const std::string& msg);
    static void Info(const std::string& msg);
    static void Debug(const std::string& msg);

    static void Error(const std::wstring& msg);
    static void Warn(const std::wstring& msg);
    static void Info(const std::wstring& msg);
    static void Debug(const std::wstring& msg);

    static void Error(const std::string& context, const std::string& msg);
    static void Warn(const std::string& context, const std::string& msg);
    static void Info(const std::string& context, const std::string& msg);
    static void Debug(const std::string& context, const std::string& msg);

    static void Flush();

private:
    static void LogImpl(LogLevel level, const std::string& context, const std::string& msg);
};
