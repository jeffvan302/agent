#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

struct ProviderConfig;
struct ModelConfig;

class OllamaLocalServerManager {
public:
    static OllamaLocalServerManager& Instance();

    std::string BaseUrlForProvider(const ProviderConfig& provider) const;

    bool EnsureRunning(const ProviderConfig& provider, std::string* error);
    void ReportActivity(const ProviderConfig& provider, const ModelConfig& model);
    void StopAll();

private:
    OllamaLocalServerManager();
    ~OllamaLocalServerManager();

    struct ModelActivity {
        std::chrono::steady_clock::time_point last_request;
        int keep_alive_seconds = 300;
    };

    struct ServerEntry {
        PROCESS_INFORMATION process{};
        int port = 0;
        bool started = false;
        bool shutdown_requested = false;
        std::map<std::string, ModelActivity> model_activities;
    };

    bool TryStartServer(ServerEntry* entry, std::string* error);
    bool IsHealthy(int port);
    void StopServer(ServerEntry& entry);
    void CleanerLoop();

    mutable std::mutex mutex_;
    std::map<std::string, ServerEntry> servers_;
    std::thread cleaner_thread_;
    std::condition_variable cleaner_cv_;
    bool shutdown_ = false;
};
