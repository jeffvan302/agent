#pragma once
#include <windows.h>
#include <types.h>
#include <openai_client.h>
#include <functional>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

struct QueueTestLogEntry {
    std::string timestamp;
    std::string level;     // INFO | WARN | ERROR
    std::string thread_id; // thread id string
    std::string event;     // ACQUIRE | ACTIVE | RELEASE | REJECT | BIND | SNAPSHOT | ERROR
    std::string gate_key;  // provider|model|domain
    std::string details;   // human readable extra info
};

struct QueueTestConfig {
    std::string provider_id;
    ModelConfig model;
    std::vector<ProviderConfig> providers;
    int thread_count = 4;
    int requests_per_thread = 10;
    int delay_ms = 100;
    int hold_ms = 500;
    bool use_try_acquire = false;
    GateDomain domain = GateDomain::Chat;
};

class QueueTestLogBuffer {
public:
    static constexpr size_t kMaxEntries = 2048;
    void Append(QueueTestLogEntry entry);
    std::vector<QueueTestLogEntry> ReadAll() const;
    size_t Size() const;
    void Clear();
private:
    mutable std::mutex mtx_;
    std::deque<QueueTestLogEntry> buffer_;
};

class QueueTestEngine {
public:
    QueueTestEngine(QueueTestConfig config, QueueTestLogBuffer* log);
    void Start();
    void Stop();
    bool IsRunning() const;
    int CompletedRequests() const;
    int RejectedRequests() const;
private:
    void WorkerLoop(int worker_id);
    void Log(const std::string& level, const std::string& event,
             const GateKey& key, const std::string& details);
    void LogSnapshot(const GateKey& key);
    std::string CurrentTimestamp() const;
    QueueTestConfig config_;
    QueueTestLogBuffer* log_;
    std::atomic<bool> running_{false};
    std::atomic<int> completed_{0};
    std::atomic<int> rejected_{0};
    std::vector<std::thread> workers_;
};

void ShowQueueTestDialog(HWND owner, const ModelConfig& model,
                         const std::vector<ProviderConfig>& providers);
