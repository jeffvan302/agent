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

# ===================== openai_client.cpp =====================
text, eol, bom = read_file('src/openai_client.cpp')

# Insert QueueTestEngine implementation at the end of file (before any trailing whitespace)
impl = (
    "// ── Queue Test Engine Implementation ──────────────────────────────" + eol + eol +
    "// Circular log buffer" + eol +
    "void QueueTestLogBuffer::Append(QueueTestLogEntry entry) {" + eol +
    "    std::lock_guard<std::mutex> lock(mtx_);" + eol +
    "    if (buffer_.size() >= kMaxEntries) buffer_.pop_front();" + eol +
    "    buffer_.push_back(std::move(entry));" + eol +
    "}" + eol + eol +
    "std::vector<QueueTestLogEntry> QueueTestLogBuffer::ReadAll() const {" + eol +
    "    std::lock_guard<std::mutex> lock(mtx_);" + eol +
    "    return std::vector<QueueTestLogEntry>(buffer_.begin(), buffer_.end());" + eol +
    "}" + eol + eol +
    "size_t QueueTestLogBuffer::Size() const {" + eol +
    "    std::lock_guard<std::mutex> lock(mtx_);" + eol +
    "    return buffer_.size();" + eol +
    "}" + eol + eol +
    "void QueueTestLogBuffer::Clear() {" + eol +
    "    std::lock_guard<std::mutex> lock(mtx_);" + eol +
    "    buffer_.clear();" + eol +
    "}" + eol + eol +
    "// Test engine" + eol +
    "QueueTestEngine::QueueTestEngine(QueueTestConfig config, QueueTestLogBuffer* log)" + eol +
    "    : config_(std::move(config)), log_(log) {}" + eol + eol +
    "void QueueTestEngine::Start() {" + eol +
    "    if (running_.exchange(true)) return;" + eol +
    "    completed_ = 0;" + eol +
    "    rejected_ = 0;" + eol +
    "    if (log_) log_->Clear();" + eol +
    "    workers_.clear();" + eol +
    "    for (int i = 0; i < config_.thread_count; ++i) {" + eol +
    "        workers_.emplace_back(&QueueTestEngine::WorkerLoop, this, i);" + eol +
    "    }" + eol +
    "}" + eol + eol +
    "void QueueTestEngine::Stop() {" + eol +
    "    running_ = false;" + eol +
    "    for (auto& t : workers_) {" + eol +
    "        if (t.joinable()) t.join();" + eol +
    "    }" + eol +
    "    workers_.clear();" + eol +
    "}" + eol + eol +
    "bool QueueTestEngine::IsRunning() const { return running_; }" + eol +
    "int  QueueTestEngine::CompletedRequests() const { return completed_; }" + eol +
    "int  QueueTestEngine::RejectedRequests() const { return rejected_; }" + eol + eol +
    "std::string QueueTestEngine::CurrentTimestamp() const {" + eol +
    "    using namespace std::chrono;" + eol +
    "    auto now = system_clock::now();" + eol +
    "    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;" + eol +
    "    auto timer = system_clock::to_time_t(now);" + eol +
    "    std::tm bt;" + eol +
    "    gmtime_s(&bt, &timer);" + eol +
    "    char buf[32];" + eol +
    "    std::snprintf(buf, sizeof(buf), \"%02d:%02d:%02d.%03d\"," + eol +
    "                  bt.tm_hour, bt.tm_min, bt.tm_sec, static_cast<int>(ms.count()));" + eol +
    "    return buf;" + eol +
    "}" + eol + eol +
    "void QueueTestEngine::Log(const std::string& level, const std::string& event," + eol +
    "                          const GateKey& key, const std::string& details) {" + eol +
    "    if (!log_) return;" + eol +
    "    QueueTestLogEntry e;" + eol +
    "    e.timestamp = CurrentTimestamp();" + eol +
    "    e.level = level;" + eol +
    "    std::ostringstream tid; tid << std::this_thread::get_id();" + eol +
    "    e.thread_id = tid.str();" + eol +
    "    e.event = event;" + eol +
    "    e.gate_key = ProviderRequestGate::GateKeyToString(key);" + eol +
    "    e.details = details;" + eol +
    "    log_->Append(std::move(e));" + eol +
    "}" + eol + eol +
    "void QueueTestEngine::LogSnapshot(const GateKey& key) {" + eol +
    "    auto snap = ProviderRequestGate::GetGateState(key);" + eol +
    "    if (!snap) return;" + eol +
    "    std::string d = \"active=\" + std::to_string(snap->currently_active) +" + eol +
    "                \" waiters=\" + std::to_string(snap->waiters_depth) +" + eol +
    "                \" max=\" + std::to_string(snap->effective_max_active) +" + eol +
    "                \" queue=\" + std::to_string(snap->effective_max_queue) +" + eol +
    "                \" self-managed=\" + std::string(snap->is_self_managed ? \"yes\" : \"no\");" + eol +
    "    Log(\"INFO\", \"SNAPSHOT\", key, d);" + eol +
    "}" + eol + eol +
    "void QueueTestEngine::WorkerLoop(int worker_id) {" + eol +
    "    ChatRequestOptions gate_request;" + eol +
    "    gate_request.provider = config_.model.is_binding_model ? ProviderConfig{} : config_.providers.empty() ? ProviderConfig{} : config_.providers.front();" + eol +
    "    gate_request.model = config_.model;" + eol +
    "    GateKey key{gate_request.provider.id, gate_request.model.id, config_.domain};" + eol +
    "    // Binding resolution for initial key" + eol +
    "    if (config_.model.is_binding_model) {" + eol +
    "        ChatRequestOptions resolved = gate_request;" + eol +
    "        if (ResolveBindingTarget(config_.providers, config_.model, gate_request, &resolved, {})) {" + eol +
    "            key.provider_id = resolved.provider.id;" + eol +
    "            key.model_id = resolved.model.id;" + eol +
    "            Log(\"INFO\", \"BIND\", key,\" + eol +
    "                \"binding=\" + config_.model.id +" + eol +
    "                \" resolved=\" + resolved.provider.id + \"/\" + resolved.model.id);" + eol +
    "        } else {" + eol +
    "            Log(\"ERROR\", \"BIND\", key,\" + eol +
    "                \"binding=\" + config_.model.id + \" no target available\");" + eol +
    "            return;" + eol +
    "        }" + eol +
    "    }" + eol +
    "    // Resolve effective limits for the final key" + eol +
    "    int p_active = 0, p_queue = 0, m_active = 0, m_queue = 0;" + eol +
    "    bool self_managed = false;" + eol +
    "    {" + eol +
    "        std::lock_guard<std::mutex> lock(GateMapMutex());" + eol +
    "        auto it = s_gate_provider_cache.find(key.provider_id);" + eol +
    "        if (it != s_gate_provider_cache.end()) {" + eol +
    "            p_active = it->second.max_active_requests;" + eol +
    "            p_queue = it->second.max_queue_size;" + eol +
    "            for (const auto& m : it->second.models) {" + eol +
    "                if (m.id == key.model_id) {" + eol +
    "                    m_active = m.max_active_requests;" + eol +
    "                    m_queue = m.max_queue_size;" + eol +
    "                    self_managed = m.self_managed_queue;" + eol +
    "                    break;" + eol +
    "                }" + eol +
    "            }" + eol +
    "        }" + eol +
    "    }" + eol +
    "    int eff_active = ComputeEffectiveMaxActive(p_active, m_active);" + eol +
    "    int eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);" + eol +
    "    for (int i = 0; i < config_.requests_per_thread && running_; ++i) {" + eol +
    "        if (config_.delay_ms > 0) {" + eol +
    "            std::this_thread::sleep_for(std::chrono::milliseconds(config_.delay_ms));" + eol +
    "        }" + eol +
    "        if (!running_) break;" + eol +
    "        bool acquired = false;" + eol +
    "        if (self_managed) {" + eol +
    "            acquired = SelfManagedGate::Acquire(key, eff_active, eff_queue, {});" + eol +
    "            Log(\"INFO\", \"ACTIVE\", key,\" + eol +
    "                \"worker=\" + std::to_string(worker_id) + \" self-managed skip\");" + eol +
    "            LogSnapshot(key);" + eol +
    "        } else if (config_.use_try_acquire) {" + eol +
    "            acquired = ProviderRequestGate::TryAcquire(key, eff_active, eff_queue, {});" + eol +
    "            if (acquired) {" + eol +
    "                Log(\"INFO\", \"ACTIVE\", key,\" + eol +
    "                    \"worker=\" + std::to_string(worker_id) + \" try-acquired\");" + eol +
    "                LogSnapshot(key);" + eol +
    "            } else {" + eol +
    "                Log(\"WARN\", \"REJECT\", key,\" + eol +
    "                    \"worker=\" + std::to_string(worker_id) + \" try-acquire rejected\");" + eol +
    "                LogSnapshot(key);" + eol +
    "                ++rejected_;" + eol +
    "                continue;" + eol +
    "            }" + eol +
    "        } else {" + eol +
    "            Log(\"INFO\", \"ACQUIRE\", key,\" + eol +
    "                \"worker=\" + std::to_string(worker_id) + \" entering queue\");" + eol +
    "            LogSnapshot(key);" + eol +
    "            acquired = ProviderRequestGate::Acquire(key, eff_active, eff_queue, {});" + eol +
    "            if (acquired) {" + eol +
    "                Log(\"INFO\", \"ACTIVE\", key,\" + eol +
    "                    \"worker=\" + std::to_string(worker_id) + \" acquired from queue\");" + eol +
    "                LogSnapshot(key);" + eol +
    "            } else {" + eol +
    "                Log(\"WARN\", \"REJECT\", key,\" + eol +
    "                    \"worker=\" + std::to_string(worker_id) + \" queue full\");" + eol +
    "                LogSnapshot(key);" + eol +
    "                ++rejected_;" + eol +
    "                continue;" + eol +
    "            }" + eol +
    "        }" + eol +
    "        // Hold the slot" + eol +
    "        if (acquired && running_) {" + eol +
    "            if (config_.hold_ms > 0) {" + eol +
    "                std::this_thread::sleep_for(std::chrono::milliseconds(config_.hold_ms));" + eol +
    "            }" + eol +
    "            if (self_managed) SelfManagedGate::Release(key);" + eol +
    "            else ProviderRequestGate::Release(key);" + eol +
    "            Log(\"INFO\", \"RELEASE\", key,\" + eol +
    "                \"worker=\" + std::to_string(worker_id) + \" released\");" + eol +
    "            LogSnapshot(key);" + eol +
    "            ++completed_;" + eol +
    "        }" + eol +
    "    }" + eol +
    "}" + eol
)

# Find a good insertion point: after the last function in the file
# We'll look for "void OpenAIClient::SetStorage" or just append before any trailing newlines
if 'QueueTestEngine::QueueTestEngine' not in text:
    text = text.rstrip() + eol + eol + impl
    print('Added QueueTestEngine implementation')
else:
    print('WARN: QueueTestEngine already present')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done openai_client.cpp')
