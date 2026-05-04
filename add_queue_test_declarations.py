import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = True
    else:
        text = raw.decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = False
    return text, eol, bom

def write_file(path, text, eol, bom):
    if bom:
        b = text.encode('utf-16-le')
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

# ===================== openai_client.h =====================
text, eol, bom = read_file('src/openai_client.h')

# Insert QueueTestEngine declarations before class OpenAIClient
old_class = "class OpenAIClient {"
new_class = (
    "// ── Queue Test System ──────────────────────────────────────────────" + eol + eol +
    "struct QueueTestLogEntry {" + eol +
    "    std::string timestamp;" + eol +
    "    std::string level;     // INFO | WARN | ERROR" + eol +
    "    std::string thread_id; // thread id string" + eol +
    "    std::string event;     // ACQUIRE | ACTIVE | RELEASE | REJECT | BIND | SNAPSHOT | ERROR" + eol +
    "    std::string gate_key;  // provider|model|domain" + eol +
    "    std::string details;   // human readable extra info" + eol +
    "};" + eol + eol +
    "struct QueueTestConfig {" + eol +
    "    ModelConfig model;" + eol +
    "    std::vector<ProviderConfig> providers;" + eol +
    "    int thread_count = 4;" + eol +
    "    int requests_per_thread = 10;" + eol +
    "    int delay_ms = 100;" + eol +
    "    int hold_ms = 500;" + eol +
    "    bool use_try_acquire = false;" + eol +
    "    GateDomain domain = GateDomain::Chat;" + eol +
    "};" + eol + eol +
    "class QueueTestLogBuffer {" + eol +
    "public:" + eol +
    "    static constexpr size_t kMaxEntries = 2048;" + eol +
    "    void Append(QueueTestLogEntry entry);" + eol +
    "    std::vector<QueueTestLogEntry> ReadAll() const;" + eol +
    "    size_t Size() const;" + eol +
    "    void Clear();" + eol +
    "private:" + eol +
    "    mutable std::mutex mtx_;" + eol +
    "    std::deque<QueueTestLogEntry> buffer_;" + eol +
    "};" + eol + eol +
    "class QueueTestEngine {" + eol +
    "public:" + eol +
    "    QueueTestEngine(QueueTestConfig config, QueueTestLogBuffer* log);" + eol +
    "    void Start();" + eol +
    "    void Stop();" + eol +
    "    bool IsRunning() const;" + eol +
    "    int CompletedRequests() const;" + eol +
    "    int RejectedRequests() const;" + eol +
    "private:" + eol +
    "    void WorkerLoop(int worker_id);" + eol +
    "    void Log(const std::string& level, const std::string& event," + eol +
    "             const GateKey& key, const std::string& details);" + eol +
    "    void LogSnapshot(const GateKey& key);" + eol +
    "    std::string CurrentTimestamp() const;" + eol +
    "    QueueTestConfig config_;" + eol +
    "    QueueTestLogBuffer* log_;" + eol +
    "    std::atomic<bool> running_{false};" + eol +
    "    std::atomic<int> completed_{0};" + eol +
    "    std::atomic<int> rejected_{0};" + eol +
    "    std::vector<std::thread> workers_;" + eol +
    "};" + eol + eol +
    "class OpenAIClient {"
)
if old_class in text:
    text = text.replace(old_class, new_class)
    print('Added QueueTestEngine declarations')
else:
    print('WARN: class OpenAIClient not found')

write_file('src/openai_client.h', text, eol, bom)
print('Done openai_client.h')
