import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
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

# 1. Add SelfManagedGate class and TryAcquire to ProviderRequestGate
# Insert after GateSlot destructor and before ResolveBindingTarget
old_insert = (
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,"
)
new_insert = (
    "// Self-managed gate: no-op acquire/release for remote-provider-managed queues." + eol +
    "class SelfManagedGate {" + eol +
    "public:" + eol +
    "    static bool Acquire(const GateKey& key," + eol +
    "                        int /*effective_max_active*/," + eol +
    "                        int /*effective_max_queue*/," + eol +
    "                        const std::function<void(const ProviderQueueStatus&)>& on_status) {" + eol +
    "        if (on_status) {" + eol +
    "            ProviderQueueStatus status;" + eol +
    "            status.provider_id = key.provider_id;" + eol +
    "            status.provider_name = key.provider_id;" + eol +
    "            status.state = \"active\";" + eol +
    "            status.queue_position = 0;" + eol +
    "            status.queue_depth = 0;" + eol +
    "            status.active_requests = 0;" + eol +
    "            status.max_active_requests = 0;" + eol +
    "            status.max_queue_size = 0;" + eol +
    "            on_status(status);" + eol +
    "        }" + eol +
    "        return true;" + eol +
    "    }" + eol +
    "    static void Release(const GateKey& /*key*/) { /* no-op */ }" + eol +
    "};" + eol + eol +
    "bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,"
)
if old_insert in text:
    text = text.replace(old_insert, new_insert)
    print('Added SelfManagedGate')
else:
    print('WARN: insertion point not found')

# 2. Add TryAcquire to ProviderRequestGate (insert before Acquire implementation)
old_acquire = (
    "bool ProviderRequestGate::Acquire(const GateKey& key," + eol +
    "                                   int effective_max_active," + eol +
    "                                   int effective_max_queue," + eol +
    "                                   const std::function<void(const ProviderQueueStatus&)>& on_status) {"
)
new_acquire = (
    "// Non-blocking attempt: returns true immediately if capacity exists, false otherwise." + eol +
    "bool ProviderRequestGate::TryAcquire(const GateKey& key," + eol +
    "                                        int effective_max_active," + eol +
    "                                        int effective_max_queue," + eol +
    "                                        const std::function<void(const ProviderQueueStatus&)>& on_status) {" + eol +
    "    std::unique_lock<std::mutex> map_lock(GateMapMutex());" + eol +
    "    GateState& state = GateMap()[key];" + eol +
    "    map_lock.unlock();" + eol + eol +
    "    std::unique_lock<std::mutex> lock(state.mtx);" + eol +
    "    if (effective_max_queue > 0 && static_cast<int>(state.waiters.size()) >= effective_max_queue) {" + eol +
    "        return false;" + eol +
    "    }" + eol +
    "    if (effective_max_active > 0 && state.currently_active >= effective_max_active) {" + eol +
    "        return false;" + eol +
    "    }" + eol +
    "    state.currently_active++;" + eol +
    "    if (on_status) {" + eol +
    "        ProviderQueueStatus status;" + eol +
    "        status.provider_id = key.provider_id;" + eol +
    "        status.provider_name = s_gate_provider_cache.count(key.provider_id) ? s_gate_provider_cache[key.provider_id].name : key.provider_id;" + eol +
    "        status.state = \"active\";" + eol +
    "        status.queue_position = 0;" + eol +
    "        status.queue_depth = static_cast<int>(state.waiters.size());" + eol +
    "        status.active_requests = state.currently_active;" + eol +
    "        status.max_active_requests = effective_max_active;" + eol +
    "        status.max_queue_size = effective_max_queue;" + eol +
    "        on_status(status);" + eol +
    "    }" + eol +
    "    return true;" + eol +
    "}" + eol + eol +
    "bool ProviderRequestGate::Acquire(const GateKey& key," + eol +
    "                                   int effective_max_active," + eol +
    "                                   int effective_max_queue," + eol +
    "                                   const std::function<void(const ProviderQueueStatus&)>& on_status) {"
)
if old_acquire in text:
    text = text.replace(old_acquire, new_acquire)
    print('Added ProviderRequestGate::TryAcquire')
else:
    print('WARN: Acquire insertion point not found')

# 3. Rewrite ResolveBindingTarget to NOT acquire gates, just pick a target
old_resolve = (
    "    const BindingTargetConfig* selected = nullptr;" + eol +
    "    if (round_robin) {" + eol +
    "        static std::unordered_map<std::string, size_t> rr_index;" + eol +
    "        static std::mutex rr_mtx;" + eol +
    "        std::lock_guard<std::mutex> lock(rr_mtx);" + eol +
    "        size_t& idx = rr_index[binding_model.id];" + eol +
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {" + eol +
    "            const auto* candidate = candidates[idx % candidates.size()];" + eol +
    "            idx++;" + eol +
    "            int eff_active = 1, eff_queue = 100;" + eol +
    "            bool self_managed = false;" + eol +
    "            {" + eol +
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());" + eol +
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);" + eol +
    "                if (it != s_gate_provider_cache.end()) {" + eol +
    "                    int p_active = it->second.max_active_requests;" + eol +
    "                    int p_queue = it->second.max_queue_size;" + eol +
    "                    int m_active = 0, m_queue = 0;" + eol +
    "                    for (const auto& m : it->second.models) {" + eol +
    "                        if (m.id == candidate->model_id) {" + eol +
    "                            m_active = m.max_active_requests;" + eol +
    "                            m_queue = m.max_queue_size;" + eol +
    "                            self_managed = m.self_managed_queue;" + eol +
    "                            break;" + eol +
    "                        }" + eol +
    "                    }" + eol +
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);" + eol +
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);" + eol +
    "                }" + eol +
    "            }" + eol +
    "            if (self_managed) { selected = candidate; break; }" + eol +
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {" + eol +
    "                selected = candidate;" + eol +
    "                break;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    } else {" + eol +
    "        for (const auto* candidate : candidates) {" + eol +
    "            int eff_active = 1, eff_queue = 100;" + eol +
    "            bool self_managed = false;" + eol +
    "            {" + eol +
    "                std::unique_lock<std::mutex> map_lock(GateMapMutex());" + eol +
    "                auto it = s_gate_provider_cache.find(candidate->provider_id);" + eol +
    "                if (it != s_gate_provider_cache.end()) {" + eol +
    "                    int p_active = it->second.max_active_requests;" + eol +
    "                    int p_queue = it->second.max_queue_size;" + eol +
    "                    int m_active = 0, m_queue = 0;" + eol +
    "                    for (const auto& m : it->second.models) {" + eol +
    "                        if (m.id == candidate->model_id) {" + eol +
    "                            m_active = m.max_active_requests;" + eol +
    "                            m_queue = m.max_queue_size;" + eol +
    "                            self_managed = m.self_managed_queue;" + eol +
    "                            break;" + eol +
    "                        }" + eol +
    "                    }" + eol +
    "                    eff_active = ComputeEffectiveMaxActive(p_active, m_active);" + eol +
    "                    eff_queue = ComputeEffectiveMaxQueue(p_queue, m_queue);" + eol +
    "                }" + eol +
    "            }" + eol +
    "            if (self_managed) { selected = candidate; break; }" + eol +
    "            if (ProviderRequestGate::Acquire({candidate->provider_id, candidate->model_id, GateDomain::Chat}, eff_active, eff_queue, on_queue_status)) {" + eol +
    "                selected = candidate;" + eol +
    "                break;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    }"
)
new_resolve = (
    "    const BindingTargetConfig* selected = nullptr;" + eol +
    "    if (round_robin) {" + eol +
    "        static std::unordered_map<std::string, size_t> rr_index;" + eol +
    "        static std::mutex rr_mtx;" + eol +
    "        std::lock_guard<std::mutex> lock(rr_mtx);" + eol +
    "        size_t& idx = rr_index[binding_model.id];" + eol +
    "        for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {" + eol +
    "            const auto* candidate = candidates[idx % candidates.size()];" + eol +
    "            idx++;" + eol +
    "            selected = candidate;" + eol +
    "            break;" + eol +
    "        }" + eol +
    "    } else {" + eol +
    "        for (const auto* candidate : candidates) {" + eol +
    "            selected = candidate;" + eol +
    "            break;" + eol +
    "        }" + eol +
    "    }"
)
if old_resolve in text:
    text = text.replace(old_resolve, new_resolve)
    print('Simplified ResolveBindingTarget (removed gate Acquire)')
else:
    print('WARN: ResolveBindingTarget old not found')

# 4. Update GateSlot::Acquire to use SelfManagedGate for self-managed models
old_gateslot = (
    "    if (self_managed) {" + eol +
    "        acquired = true;" + eol +
    "        return true;" + eol +
    "    }" + eol + eol +
    "    const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);"
)
new_gateslot = (
    "    if (self_managed) {" + eol +
    "        acquired = SelfManagedGate::Acquire(key, provider_max_active, provider_max_queue, on_status);" + eol +
    "        return acquired;" + eol +
    "    }" + eol + eol +
    "    const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);"
)
if old_gateslot in text:
    text = text.replace(old_gateslot, new_gateslot)
    print('Updated GateSlot::Acquire to use SelfManagedGate')
else:
    print('WARN: GateSlot::Acquire old not found')

# 5. Update GateSlot destructor to use SelfManagedGate::Release for self-managed
old_destruct = (
    "GateSlot::~GateSlot() {" + eol +
    "    if (acquired) {" + eol +
    "        ProviderRequestGate::Release(key);" + eol +
    "    }" + eol +
    "}"
)
new_destruct = (
    "GateSlot::~GateSlot() {" + eol +
    "    if (acquired) {" + eol +
    "        bool self_managed = false;" + eol +
    "        {" + eol +
    "            std::lock_guard<std::mutex> lock(GateMapMutex());" + eol +
    "            auto it = s_gate_provider_cache.find(key.provider_id);" + eol +
    "            if (it != s_gate_provider_cache.end()) {" + eol +
    "                for (const auto& m : it->second.models) {" + eol +
    "                    if (m.id == key.model_id) { self_managed = m.self_managed_queue; break; }" + eol +
    "                }" + eol +
    "            }" + eol +
    "        }" + eol +
    "        if (self_managed) SelfManagedGate::Release(key);" + eol +
    "        else ProviderRequestGate::Release(key);" + eol +
    "    }" + eol +
    "}"
)
if old_destruct in text:
    text = text.replace(old_destruct, new_destruct)
    print('Updated GateSlot destructor for SelfManagedGate')
else:
    print('WARN: GateSlot destructor old not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done openai_client.cpp')

# ===================== openai_client.h =====================
text, eol, bom = read_file('src/openai_client.h')

# Add TryAcquire declaration to ProviderRequestGate
old_class = (
    "class ProviderRequestGate {" + eol +
    "public:" + eol +
    "    static void Configure(const std::vector<ProviderConfig>& providers);" + eol +
    "    static bool Acquire(const GateKey& key,"
)
new_class = (
    "class ProviderRequestGate {" + eol +
    "public:" + eol +
    "    static void Configure(const std::vector<ProviderConfig>& providers);" + eol +
    "    static bool TryAcquire(const GateKey& key," + eol +
    "                         int effective_max_active," + eol +
    "                         int effective_max_queue," + eol +
    "                         const std::function<void(const ProviderQueueStatus&)>& on_status);" + eol +
    "    static bool Acquire(const GateKey& key,"
)
if old_class in text:
    text = text.replace(old_class, new_class)
    print('Added ProviderRequestGate::TryAcquire declaration')
else:
    print('WARN: ProviderRequestGate declaration old not found')

write_file('src/openai_client.h', text, eol, bom)
print('Done openai_client.h')

print('All edits complete.')
