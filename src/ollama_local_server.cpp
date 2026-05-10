#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>

#include "ollama_local_server.h"
#include "types.h"
#include "ollama_cli_bridge.h"
#include "util.h"
#include "provider_profiles.h"

#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>

#pragma comment(lib, "winhttp.lib")

#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "iphlpapi.lib")

namespace {
    uint16_t SwapPortEndian(uint16_t raw) {
        return static_cast<uint16_t>(((raw & 0xFF00) >> 8) | ((raw & 0x00FF) << 8));
    }
    bool IsManagedProvider(const ProviderConfig& provider) {
        return NormalizeProviderType(provider.provider_type) == "ollama_local";
    }

    std::string ProviderKey(const ProviderConfig& provider) {
        return provider.id;
    }

    std::vector<DWORD> FindPidsListeningOnPort(int port) {
        std::vector<DWORD> result;
        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<BYTE> buffer(size);
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
            return result;
        }
        auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (SwapPortEndian(static_cast<uint16_t>(row.dwLocalPort)) == static_cast<uint16_t>(port) && row.dwState == MIB_TCP_STATE_LISTEN) {
                result.push_back(row.dwOwningPid);
            }
        }
        return result;
    }

    bool WaitForListener(int port, int timeout_ms, std::string* error) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            HINTERNET session = WinHttpOpen(L"AgentOllamaHealth/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
            HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
            if (!connect) { WinHttpCloseHandle(session); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
            HINTERNET req = WinHttpOpenRequest(connect, L"GET", L"/", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (!req) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
            WinHttpSetTimeouts(req, 2000, 2000, 2000, 2000);
            if (WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
                WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
                return true;
            }
            WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (error) *error = "Timed out waiting for Ollama listener on port " + std::to_string(port) + ".";
        return false;
    }

    bool SendGet(int port, const std::string& path, std::string* out_body, std::string* error) {
        HINTERNET session = WinHttpOpen(L"AgentOllamaHealth/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { if (error) *error = "WinHttpOpen failed."; return false; }
        HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
        if (!connect) { WinHttpCloseHandle(session); if (error) *error = "WinHttpConnect failed."; return false; }
        HINTERNET req = WinHttpOpenRequest(connect, L"GET", Utf8ToWide(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!req) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); if (error) *error = "WinHttpOpenRequest failed."; return false; }
        WinHttpSetTimeouts(req, 5000, 5000, 10000, 10000);
        if (!WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
            if (error) *error = "Request failed to 127.0.0.1:" + std::to_string(port) + path;
            return false;
        }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        std::string body;
        for (;;) { DWORD avail = 0; if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break; std::vector<char> buf(avail); DWORD read = 0; WinHttpReadData(req, buf.data(), avail, &read); body.append(buf.data(), read); }
        WinHttpCloseHandle(req); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        if (status < 200 || status >= 300) { if (error) *error = "HTTP " + std::to_string(status); return false; }
        if (out_body) *out_body = body;
        return true;
    }

    void KillListenerOnPort(int port) {
        const auto pids = FindPidsListeningOnPort(port);
        if (pids.empty()) return;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"ollama.exe") != 0) continue;
                if (std::find(pids.begin(), pids.end(), pe.th32ProcessID) == pids.end()) continue;
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (!h) continue;
                bool kill = false;
                HMODULE mods[1024]{}; DWORD needed = 0;
                if (EnumProcessModules(h, mods, sizeof(mods), &needed)) {
                    for (DWORD i = 0; i < (needed / sizeof(HMODULE)); ++i) {
                        wchar_t path[MAX_PATH]{};
                        if (GetModuleFileNameExW(h, mods[i], path, MAX_PATH)) {
                            if (std::wstring(path).find(L"ollama.exe") != std::wstring::npos) { kill = true; break; }
                        }
                    }
                }
                CloseHandle(h);
                if (kill) { HANDLE t = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); if (t) { TerminateProcess(t, 1); CloseHandle(t); } }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

OllamaLocalServerManager::OllamaLocalServerManager() {
    cleaner_thread_ = std::thread([this]() { CleanerLoop(); });
}

OllamaLocalServerManager::~OllamaLocalServerManager() {
    StopAll();
}

OllamaLocalServerManager& OllamaLocalServerManager::Instance() {
    static OllamaLocalServerManager inst;
    return inst;
}

std::string OllamaLocalServerManager::BaseUrlForProvider(const ProviderConfig& provider) const {
    int port = provider.ollama_local_port;
    if (port <= 0) port = 12434;
    return "http://127.0.0.1:" + std::to_string(port);
}

bool OllamaLocalServerManager::EnsureRunning(const ProviderConfig& provider, std::string* error) {
    if (!IsManagedProvider(provider)) {
        if (error) *error = "Not an Ollama local provider.";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = servers_[ProviderKey(provider)];
    if (!entry.started) {
        entry.port = provider.ollama_local_port;
        if (entry.port <= 0) entry.port = 12434;
        entry.shutdown_requested = false;
    }
    if (entry.process.hProcess && entry.process.hProcess != INVALID_HANDLE_VALUE) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(entry.process.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            if (SendGet(entry.port, "/", nullptr, nullptr)) return true;
            KillListenerOnPort(entry.port);
            CloseHandle(entry.process.hProcess);
            entry.process = {};
        } else {
            CloseHandle(entry.process.hProcess);
            entry.process = {};
        }
    }
    const auto exe_opt = FindOllamaCliPath();
    if (!exe_opt) { if (error) *error = "Could not find ollama.exe."; return false; }
    const auto exe = *exe_opt;
    KillListenerOnPort(entry.port);

    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    std::wstring args = L"\"" + exe + L"\" serve";
    std::vector<wchar_t> cmd(args.begin(), args.end()); cmd.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE stdin_r = nullptr, stdin_w = nullptr, stdout_r = nullptr, stdout_w = nullptr;
    CreatePipe(&stdin_r, &stdin_w, &sa, 0);
    SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&stdout_r, &stdout_w, &sa, 0);
    SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);

    si.hStdInput = stdin_r; si.hStdOutput = stdout_w; si.hStdError = stdout_w;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

    std::vector<wchar_t> envbuf;
    { LPWCH raw = GetEnvironmentStringsW(); if (raw) { const wchar_t* cur = raw; while (*cur) { std::wstring e = cur; envbuf.insert(envbuf.end(), e.begin(), e.end()); envbuf.push_back(L'\0'); cur += e.size() + 1; } FreeEnvironmentStringsW(raw); } }
    auto addEnv = [&](const std::wstring& name, const std::wstring& value) { envbuf.insert(envbuf.end(), name.begin(), name.end()); envbuf.push_back(L'='); envbuf.insert(envbuf.end(), value.begin(), value.end()); envbuf.push_back(L'\0'); };
    addEnv(L"OLLAMA_HOST", L"127.0.0.1:" + std::to_wstring(entry.port));
    envbuf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, envbuf.empty() ? nullptr : envbuf.data(), nullptr, &si, &pi)) {
        if (error) *error = "Could not start ollama serve: error " + std::to_string(GetLastError());
        CloseHandle(stdin_r); CloseHandle(stdin_w); CloseHandle(stdout_r); CloseHandle(stdout_w);
        return false;
    }
    CloseHandle(stdin_r); CloseHandle(stdin_w); CloseHandle(stdout_r); CloseHandle(stdout_w); CloseHandle(pi.hThread);
    entry.process = pi;
    if (!WaitForListener(entry.port, 30000, error)) {
        TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); entry.process = {};
        return false;
    }
    entry.started = true;
    return true;
}

void OllamaLocalServerManager::ReportActivity(const ProviderConfig& provider, const ModelConfig& model) {
    if (!IsManagedProvider(provider)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(ProviderKey(provider));
    if (it == servers_.end()) return;
    auto& activity = it->second.model_activities[model.id];
    activity.last_request = std::chrono::steady_clock::now();
    activity.keep_alive_seconds = model.ollama_keep_alive_seconds > 0 ? model.ollama_keep_alive_seconds : 300;
}

void OllamaLocalServerManager::StopAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        for (auto& kv : servers_) {
            kv.second.shutdown_requested = true;
            if (kv.second.process.hProcess && kv.second.process.hProcess != INVALID_HANDLE_VALUE) {
                TerminateProcess(kv.second.process.hProcess, 1);
            }
        }
    }
    if (cleaner_thread_.joinable()) cleaner_thread_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : servers_) { StopServer(kv.second); }
        servers_.clear();
    }
}

void OllamaLocalServerManager::StopServer(ServerEntry& entry) {
    if (entry.process.hProcess && entry.process.hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(entry.process.hProcess, 1);
        CloseHandle(entry.process.hProcess);
        entry.process = {};
    }
    entry.started = false;
    entry.shutdown_requested = false;
    entry.model_activities.clear();
}

void OllamaLocalServerManager::CleanerLoop() {
    while (!shutdown_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cleaner_cv_.wait_for(lock, std::chrono::seconds(10), [&]() { return shutdown_; });
        if (shutdown_) break;
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> keys_to_stop;
        for (auto& kv : servers_) {
            auto& entry = kv.second;
            if (!entry.started || entry.shutdown_requested) continue;
            int max_keep = 0;
            for (auto& ma : entry.model_activities) {
                int ka = ma.second.keep_alive_seconds > 0 ? ma.second.keep_alive_seconds : 300;
                if (ka > max_keep) max_keep = ka;
                auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - ma.second.last_request).count();
                if (idle >= ka) { keys_to_stop.push_back(kv.first); break; }
            }
            if (max_keep > 0) {
                auto oldest = std::chrono::steady_clock::time_point::max();
                for (auto& ma : entry.model_activities) { if (ma.second.last_request < oldest) oldest = ma.second.last_request; }
                auto idle = std::chrono::duration_cast<std::chrono::seconds>(now - oldest).count();
                if (idle >= max_keep) { keys_to_stop.push_back(kv.first); }
            }
        }
        lock.unlock();
        for (auto& key : keys_to_stop) {
            lock.lock();
            auto it = servers_.find(key);
            if (it != servers_.end()) { StopServer(it->second); }
            lock.unlock();
        }
    }
}
