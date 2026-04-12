#pragma once

#include <windows.h>

#include <optional>
#include <string>

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
