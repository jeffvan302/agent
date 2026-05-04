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

# Add GateStateSnapshot and introspection methods
old_class = (
    "class ProviderRequestGate {" + eol +
    "public:" + eol +
    "    static void Configure(const std::vector<ProviderConfig>& providers);" + eol +
    "    static bool TryAcquire(const GateKey& key," + eol +
    "                         int effective_max_active," + eol +
    "                         int effective_max_queue," + eol +
    "                         const std::function<void(const ProviderQueueStatus&)>& on_status);" + eol +
    "    static bool Acquire(const GateKey& key," + eol +
    "                        int effective_max_active," + eol +
    "                        int effective_max_queue," + eol +
    "                        const std::function<void(const ProviderQueueStatus&)>& on_status);" + eol +
    "    static void Release(const GateKey& key);" + eol +
    "};"
)
new_class = (
    "struct GateStateSnapshot {" + eol +
    "    GateKey key;" + eol +
    "    int currently_active = 0;" + eol +
    "    int waiters_depth = 0;" + eol +
    "    int effective_max_active = 0;" + eol +
    "    int effective_max_queue = 0;" + eol +
    "    bool is_self_managed = false;" + eol +
    "};" + eol + eol +
    "class ProviderRequestGate {" + eol +
    "public:" + eol +
    "    static void Configure(const std::vector<ProviderConfig>& providers);" + eol +
    "    static bool TryAcquire(const GateKey& key," + eol +
    "                         int effective_max_active," + eol +
    "                         int effective_max_queue," + eol +
    "                         const std::function<void(const ProviderQueueStatus&)>& on_status);" + eol +
    "    static bool Acquire(const GateKey& key," + eol +
    "                        int effective_max_active," + eol +
    "                        int effective_max_queue," + eol +
    "                        const std::function<void(const ProviderQueueStatus&)>& on_status);" + eol +
    "    static void Release(const GateKey& key);" + eol +
    "    static std::vector<GateStateSnapshot> EnumerateGates();" + eol +
    "    static std::optional<GateStateSnapshot> GetGateState(const GateKey& key);" + eol +
    "    static std::string GateKeyToString(const GateKey& key);" + eol +
    "};"
)
if old_class in text:
    text = text.replace(old_class, new_class)
    print('Updated ProviderRequestGate declaration')
else:
    print('WARN: class declaration old not found')
    # Debug
    idx = text.find('class ProviderRequestGate')
    print(text[idx:idx+600])

write_file('src/openai_client.h', text, eol, bom)

# ===================== openai_client.cpp =====================
text, eol, bom = read_file('src/openai_client.cpp')

# Add functions before GateSlot::Acquire or after ProviderRequestGate::Release
old_insert = "bool GateSlot::Acquire(const std::string& provider_name,"
new_insert = (
    "// ── Gate Introspection ───────────────────────────────────────────" + eol + eol +
    "std::vector<GateStateSnapshot> ProviderRequestGate::EnumerateGates() {" + eol +
    "    std::vector<GateStateSnapshot> result;" + eol +
    "    std::lock_guard<std::mutex> map_lock(GateMapMutex());" + eol +
    "    const auto& map = GateMap();" + eol +
    "    for (const auto& kv : map) {" + eol +
    "        std::lock_guard<std::mutex> lock(kv.second.mtx);" + eol +
    "        GateStateSnapshot snap;" + eol +
    "        snap.key = kv.first;" + eol +
    "        snap.currently_active = kv.second.currently_active;" + eol +
    "        snap.waiters_depth = static_cast<int>(kv.second.waiters.size());" + eol +
    "        snap.effective_max_active = kv.second.max_active;" + eol +
    "        snap.effective_max_queue = kv.second.max_queue;" + eol +
    "        // Self-managed lookup" + eol +
    "        snap.is_self_managed = false;" + eol +
    "        auto prov_it = s_gate_provider_cache.find(kv.first.provider_id);" + eol +
    "        if (prov_it != s_gate_provider_cache.end()) {" + eol +
    "            for (const auto& m : prov_it->second.models) {" + eol +
    "                if (m.id == kv.first.model_id) {" + eol +
    "                    snap.is_self_managed = m.self_managed_queue;" + eol +
    "                    break;" + eol +
    "                }" + eol +
    "            }" + eol +
    "        }" + eol +
    "        result.push_back(std::move(snap));" + eol +
    "    }" + eol +
    "    return result;" + eol +
    "}" + eol + eol +
    "std::optional<GateStateSnapshot> ProviderRequestGate::GetGateState(const GateKey& key) {" + eol +
    "    std::lock_guard<std::mutex> map_lock(GateMapMutex());" + eol +
    "    const auto& map = GateMap();" + eol +
    "    auto it = map.find(key);" + eol +
    "    if (it == map.end()) return std::nullopt;" + eol +
    "    std::lock_guard<std::mutex> lock(it->second.mtx);" + eol +
    "    GateStateSnapshot snap;" + eol +
    "    snap.key = key;" + eol +
    "    snap.currently_active = it->second.currently_active;" + eol +
    "    snap.waiters_depth = static_cast<int>(it->second.waiters.size());" + eol +
    "    snap.effective_max_active = it->second.max_active;" + eol +
    "    snap.effective_max_queue = it->second.max_queue;" + eol +
    "    snap.is_self_managed = false;" + eol +
    "    auto prov_it = s_gate_provider_cache.find(key.provider_id);" + eol +
    "    if (prov_it != s_gate_provider_cache.end()) {" + eol +
    "        for (const auto& m : prov_it->second.models) {" + eol +
    "            if (m.id == key.model_id) {" + eol +
    "                snap.is_self_managed = m.self_managed_queue;" + eol +
    "                break;" + eol +
    "            }" + eol +
    "        }" + eol +
    "    }" + eol +
    "    return snap;" + eol +
    "}" + eol + eol +
    "std::string ProviderRequestGate::GateKeyToString(const GateKey& key) {" + eol +
    "    std::string s = key.provider_id + \"|\" + key.model_id + \"|\";" + eol +
    "    switch (key.domain) {" + eol +
    "        case GateDomain::Chat:      s += \"chat\"; break;" + eol +
    "        case GateDomain::Embedding: s += \"embedding\"; break;" + eol +
    "    }" + eol +
    "    return s;" + eol +
    "}" + eol + eol +
    "bool GateSlot::Acquire(const std::string& provider_name,"
)
if old_insert in text:
    text = text.replace(old_insert, new_insert)
    print('Inserted gate introspection methods')
else:
    print('WARN: GateSlot::Acquire insertion point not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done openai_client.cpp')
