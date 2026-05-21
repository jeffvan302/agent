#include "openai_client.h"

#include "ollama_api_client.h"
#include "message_sanitizer.h"
#include "provider_profiles.h"
#include "storage.h"

#include <windows.h>
#include <winhttp.h>

#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {
bool IsOllamaLocalProvider(const ProviderConfig& provider) {
    return NormalizeProviderType(provider.provider_type) == "ollama_local";
}

bool IsOpenAICodexOAuthProvider(const ProviderConfig& provider) {
    return NormalizeProviderType(provider.provider_type) == "openai_codex_oauth";
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string DumpJsonForProviderRequest(const json& value, const std::string& context) {
    try {
        return value.dump();
    } catch (const nlohmann::json::type_error& ex) {
        Logger::Warn("OpenAIClient",
            "Invalid UTF-8 while serializing " + context +
            "; replacing invalid sequences. error=" + ex.what());
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}
} // namespace

// Static provider cache for compression model calls
static std::vector<ProviderConfig> s_provider_cache;
static AppStorage* s_storage = nullptr;

// G��G�� Provider Request Gate G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��G��

static std::unordered_map<std::string, ProviderConfig> s_gate_provider_cache;

namespace {

struct GateState {
    int max_active = 0;     // 0 = use default (1)
    int max_queue = 0;      // 0 = use default (100)
    int currently_active = 0;
    std::deque<std::thread::id> waiters;
    std::mutex mtx;
    std::condition_variable cv;
};

std::unordered_map<GateKey, GateState, GateKeyHash>& GateMap() {
    static std::unordered_map<GateKey, GateState, GateKeyHash> map;
    return map;
}

std::mutex& GateMapMutex() {
    static std::mutex m;
    return m;
}

int ComputeEffectiveMaxQueue(int provider_max, int model_max) {
    if (provider_max > 0 && model_max > 0) return std::min(provider_max, model_max);
    if (provider_max > 0) return provider_max;
    if (model_max > 0) return model_max;
    return 0; // no queue limit unless a provider or model opts in
}

int ComputeEffectiveMaxActive(int provider_max, int model_max) {
    if (provider_max > 0 && model_max > 0) return std::min(provider_max, model_max);
    if (provider_max > 0) return provider_max;
    if (model_max > 0) return model_max;
    return 0; // no active-request gate unless a provider or model opts in
}


bool IsCancelRequested(const std::function<bool()>& should_cancel);
} // namespace

void ProviderRequestGate::Configure(const std::vector<ProviderConfig>& providers) {
    std::lock_guard<std::mutex> lock(GateMapMutex());
    s_gate_provider_cache.clear();
    for (const auto& p : providers) {
        s_gate_provider_cache[p.id] = p;
    }
}

// Non-blocking attempt: returns true immediately if capacity exists, false otherwise.
bool ProviderRequestGate::TryAcquire(const GateKey& key,
                                        int effective_max_active,
                                        int effective_max_queue,
                                        const std::function<void(const ProviderQueueStatus&)>& on_status) {
    if (effective_max_active <= 0) {
        if (on_status) {
            ProviderQueueStatus status;
            status.provider_id = key.provider_id;
            status.provider_name = key.provider_id;
            status.state = "active";
            status.max_active_requests = 0;
            status.max_queue_size = effective_max_queue;
            on_status(status);
        }
        return true;
    }

    std::unique_lock<std::mutex> map_lock(GateMapMutex());
    GateState& state = GateMap()[key];
    state.max_active = effective_max_active;
    state.max_queue = effective_max_queue;
    std::string provider_name = key.provider_id;
    if (auto it = s_gate_provider_cache.find(key.provider_id); it != s_gate_provider_cache.end()) {
        provider_name = it->second.name;
    }
    map_lock.unlock();

    std::unique_lock<std::mutex> lock(state.mtx);
    if (effective_max_queue > 0 && static_cast<int>(state.waiters.size()) >= effective_max_queue) {
        return false;
    }
    if (state.currently_active >= effective_max_active) {
        return false;
    }
    state.currently_active++;
    if (on_status) {
        ProviderQueueStatus status;
        status.provider_id = key.provider_id;
        status.provider_name = provider_name;
        status.state = "active";
        status.queue_position = 0;
        status.queue_depth = static_cast<int>(state.waiters.size());
        status.active_requests = state.currently_active;
        status.max_active_requests = effective_max_active;
        status.max_queue_size = effective_max_queue;
        on_status(status);
    }
    return true;
}

bool ProviderRequestGate::Acquire(const GateKey& key,
                                   int effective_max_active,
                                   int effective_max_queue,
                                   const std::function<void(const ProviderQueueStatus&)>& on_status,
                                   const std::function<bool()>& should_cancel) {
    auto cancelled = [&]() {
        return IsCancelRequested(should_cancel);
    };

    if (cancelled()) {
        return false;
    }

    if (effective_max_active <= 0) {
        if (on_status) {
            ProviderQueueStatus status;
            status.provider_id = key.provider_id;
            status.provider_name = key.provider_id;
            status.state = "active";
            status.max_active_requests = 0;
            status.max_queue_size = effective_max_queue;
            on_status(status);
        }
        return true;
    }

    std::unique_lock<std::mutex> map_lock(GateMapMutex());
    GateState& state = GateMap()[key];
    state.max_active = effective_max_active;
    state.max_queue = effective_max_queue;
    std::string provider_name = key.provider_id;
    if (auto it = s_gate_provider_cache.find(key.provider_id); it != s_gate_provider_cache.end()) {
        provider_name = it->second.name;
    }
    map_lock.unlock();

    std::unique_lock<std::mutex> lock(state.mtx);

    if (cancelled()) {
        return false;
    }

    if (effective_max_queue > 0 && static_cast<int>(state.waiters.size()) >= effective_max_queue) {
        return false;
    }

    if (state.waiters.empty() && state.currently_active < effective_max_active) {
        state.currently_active++;
        if (on_status) {
            ProviderQueueStatus status;
            status.provider_id = key.provider_id;
            status.provider_name = provider_name;
            status.state = "active";
            status.queue_position = 0;
            status.queue_depth = 0;
            status.active_requests = state.currently_active;
            status.max_active_requests = effective_max_active;
            status.max_queue_size = effective_max_queue;
            on_status(status);
        }
        return true;
    }

    const std::thread::id me = std::this_thread::get_id();
    state.waiters.push_back(me);

    auto queue_position = [&]() {
        for (size_t i = 0; i < state.waiters.size(); ++i) {
            if (state.waiters[i] == me) {
                return static_cast<int>(i) + 1;
            }
        }
        return 0;
    };

    auto emit_status = [&](const std::string& queue_state) {
        if (!on_status) return;
        ProviderQueueStatus status;
        status.provider_id = key.provider_id;
        status.provider_name = provider_name;
        status.state = queue_state;
        status.queue_position = queue_state == "queued" ? queue_position() : 0;
        status.queue_depth = static_cast<int>(state.waiters.size());
        status.active_requests = state.currently_active;
        status.max_active_requests = effective_max_active;
        status.max_queue_size = effective_max_queue;
        on_status(status);
    };

    auto remove_waiter = [&]() {
        auto it = std::find(state.waiters.begin(), state.waiters.end(), me);
        if (it != state.waiters.end()) {
            state.waiters.erase(it);
            state.cv.notify_all();
        }
    };

    emit_status("queued");

    auto ready = [&]() {
        return !state.waiters.empty() &&
               state.waiters.front() == me &&
               state.currently_active < effective_max_active;
    };

    auto last_status = std::chrono::steady_clock::now();
    while (!ready()) {
        if (cancelled()) {
            remove_waiter();
            emit_status("cancelled");
            return false;
        }

        state.cv.wait_for(lock, std::chrono::milliseconds(250));

        if (cancelled()) {
            remove_waiter();
            emit_status("cancelled");
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!ready() && now - last_status >= std::chrono::seconds(2)) {
            emit_status("queued");
            last_status = now;
        }
    }

    state.waiters.pop_front();
    state.currently_active++;

    emit_status("active");
    return true;
}
void ProviderRequestGate::Release(const GateKey& key) {
    std::unique_lock<std::mutex> map_lock(GateMapMutex());
    auto it = GateMap().find(key);
    if (it == GateMap().end()) return;

    std::lock_guard<std::mutex> lock(it->second.mtx);
    it->second.currently_active = std::max(0, it->second.currently_active - 1);
    it->second.cv.notify_one();
}

GateSlot::GateSlot(const ChatRequestOptions& request, GateDomain domain) {
    key.provider_id = request.provider.id;
    key.model_id = request.model.id;
    key.domain = domain;
}

// Self-managed gate: no-op acquire/release for remote-provider-managed queues.
class SelfManagedGate {
public:
    static bool Acquire(const GateKey& key,
                        int /*effective_max_active*/,
                        int /*effective_max_queue*/,
                        const std::function<void(const ProviderQueueStatus&)>& on_status) {
        if (on_status) {
            ProviderQueueStatus status;
            status.provider_id = key.provider_id;
            status.provider_name = key.provider_id;
            status.state = "active";
            status.queue_position = 0;
            status.queue_depth = 0;
            status.active_requests = 0;
            status.max_active_requests = 0;
            status.max_queue_size = 0;
            on_status(status);
        }
        return true;
    }
    static void Release(const GateKey& /*key*/) { /* no-op */ }
};

// -- Gate Introspection -------------------------------------------

std::vector<GateStateSnapshot> ProviderRequestGate::EnumerateGates() {
    std::vector<GateStateSnapshot> result;
    std::lock_guard<std::mutex> map_lock(GateMapMutex());
    auto& map = GateMap();
    for (auto& kv : map) {
        std::lock_guard<std::mutex> lock(kv.second.mtx);
        GateStateSnapshot snap;
        snap.key = kv.first;
        snap.currently_active = kv.second.currently_active;
        snap.waiters_depth = static_cast<int>(kv.second.waiters.size());
        snap.effective_max_active = kv.second.max_active;
        snap.effective_max_queue = kv.second.max_queue;
        // Self-managed lookup
        snap.is_self_managed = false;
        auto prov_it = s_gate_provider_cache.find(kv.first.provider_id);
        if (prov_it != s_gate_provider_cache.end()) {
            for (const auto& m : prov_it->second.models) {
                if (m.id == kv.first.model_id) {
                    snap.is_self_managed = m.self_managed_queue;
                    break;
                }
            }
        }
        result.push_back(std::move(snap));
    }
    return result;
}

std::optional<GateStateSnapshot> ProviderRequestGate::GetGateState(const GateKey& key) {
    std::lock_guard<std::mutex> map_lock(GateMapMutex());
    auto& map = GateMap();
    auto it = map.find(key);
    if (it == map.end()) return std::nullopt;
    std::lock_guard<std::mutex> lock(it->second.mtx);
    GateStateSnapshot snap;
    snap.key = key;
    snap.currently_active = it->second.currently_active;
    snap.waiters_depth = static_cast<int>(it->second.waiters.size());
    snap.effective_max_active = it->second.max_active;
    snap.effective_max_queue = it->second.max_queue;
    snap.is_self_managed = false;
    auto prov_it = s_gate_provider_cache.find(key.provider_id);
    if (prov_it != s_gate_provider_cache.end()) {
        for (const auto& m : prov_it->second.models) {
            if (m.id == key.model_id) {
                snap.is_self_managed = m.self_managed_queue;
                break;
            }
        }
    }
    return snap;
}

std::string ProviderRequestGate::GateKeyToString(const GateKey& key) {
    std::string s = key.provider_id + "|" + key.model_id + "|";
    switch (key.domain) {
        case GateDomain::Chat:      s += "chat"; break;
        case GateDomain::Embedding: s += "embedding"; break;
    }
    return s;
}

bool GateSlot::Acquire(const std::string& provider_name,
                        const std::function<void(const ProviderQueueStatus&)>& on_status,
                        const std::function<bool()>& should_cancel) {
    if (acquired) return true;
    if (IsCancelRequested(should_cancel)) return false;

    int provider_max_active = 0;
    int provider_max_queue = 0;
    int model_max_active = 0;
    int model_max_queue = 0;
    bool self_managed = false;
    {
        std::lock_guard<std::mutex> lock(GateMapMutex());
        auto it = s_gate_provider_cache.find(key.provider_id);
        if (it != s_gate_provider_cache.end()) {
            provider_max_active = it->second.max_active_requests;
            provider_max_queue = it->second.max_queue_size;
            for (const auto& m : it->second.models) {
                if (m.id == key.model_id) {
                    model_max_active = m.max_active_requests;
                    model_max_queue = m.max_queue_size;
                    self_managed = m.self_managed_queue;
                    break;
                }
            }
        }
    }

    if (self_managed) {
        if (IsCancelRequested(should_cancel)) return false;
        acquired = SelfManagedGate::Acquire(key, provider_max_active, provider_max_queue, on_status);
        return acquired;
    }

    const int effective_max_active = ComputeEffectiveMaxActive(provider_max_active, model_max_active);
    const int effective_max_queue = ComputeEffectiveMaxQueue(provider_max_queue, model_max_queue);

    if (effective_max_active <= 0) {
        if (IsCancelRequested(should_cancel)) return false;
        if (on_status) {
            ProviderQueueStatus status;
            status.provider_id = key.provider_id;
            status.provider_name = provider_name.empty() ? key.provider_id : provider_name;
            status.state = "active";
            status.queue_position = 0;
            status.queue_depth = 0;
            status.active_requests = 0;
            status.max_active_requests = 0;
            status.max_queue_size = effective_max_queue;
            on_status(status);
        }
        return true;
    }

    acquired = ProviderRequestGate::Acquire(key, effective_max_active, effective_max_queue, on_status, should_cancel);
    return acquired;
}
GateSlot::~GateSlot() {
    if (acquired) {
        bool self_managed = false;
        {
            std::lock_guard<std::mutex> lock(GateMapMutex());
            auto it = s_gate_provider_cache.find(key.provider_id);
            if (it != s_gate_provider_cache.end()) {
                for (const auto& m : it->second.models) {
                    if (m.id == key.model_id) { self_managed = m.self_managed_queue; break; }
                }
            }
        }
        if (self_managed) SelfManagedGate::Release(key);
        else ProviderRequestGate::Release(key);
    }
}

bool ResolveBindingTarget(const std::vector<ProviderConfig>& providers,
                          const ModelConfig& binding_model,
                          const ChatRequestOptions& original_request,
                          ChatRequestOptions* out_request,
                          const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {
    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return false;
    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;
    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {
        return OpenAIClient::BindingCooldown(t.provider_id, t.model_id);
    };
    std::vector<const BindingTargetConfig*> candidates;
    for (const auto& t : binding_model.binding_targets) {
        if (!is_cooldown(t)) candidates.push_back(&t);
    }
    if (candidates.empty()) {
        for (const auto& t : binding_model.binding_targets) {
            candidates.push_back(&t);
        }
    }
    const BindingTargetConfig* selected = nullptr;
    if (round_robin && !candidates.empty()) {
        static std::unordered_map<std::string, size_t> rr_index;
        static std::mutex rr_mtx;
        std::lock_guard<std::mutex> lock(rr_mtx);
        size_t& idx = rr_index[binding_model.id];
        selected = candidates[idx % candidates.size()];
        idx++;
    } else if (!candidates.empty()) {
        selected = candidates.front();
    }
    if (!selected) return false;
    for (const auto& p : providers) {
        if (p.id != selected->provider_id) continue;
        for (const auto& m : p.models) {
            if (m.id == selected->model_id) {
                out_request->provider = p;
                out_request->model = m;
                out_request->model.self_managed_queue = false;
                out_request->system_prompt = original_request.system_prompt;
                out_request->temperature = original_request.temperature;
                out_request->max_tokens = original_request.max_tokens;
                out_request->messages = original_request.messages;
                out_request->binding_model_id = binding_model.id;
                out_request->binding_depth = original_request.binding_depth + 1;
                return true;
            }
        }
    }
    return false;
}

// Compute total parallel capacity for a binding model (sum of target max_active_requests).
// Self-managed targets contribute 0 to the local count because their queue is remote.
int ComputeBindingModelCapacity(const ModelConfig& binding_model,
                                const std::vector<ProviderConfig>& providers) {
    if (!binding_model.is_binding_model || binding_model.binding_targets.empty()) return 0;
    int total = 0;
    for (const auto& t : binding_model.binding_targets) {
        int target_active = 0;
        bool target_self_managed = false;
        for (const auto& p : providers) {
            if (p.id != t.provider_id) continue;
            for (const auto& m : p.models) {
                if (m.id == t.model_id) {
                    target_active = std::max(0, m.max_active_requests);
                    target_self_managed = m.self_managed_queue;
                    break;
                }
            }
            break;
        }
        // All targets count toward local binding capacity (remote queue is bypassed when used via binding)
        total += target_active > 0 ? target_active : 1;
    }
    return total;
}

namespace {
struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using UniqueInternetHandle = std::unique_ptr<void, InternetHandleCloser>;

bool IsCancelRequested(const std::function<bool()>& should_cancel) {
    return should_cancel && should_cancel();
}

struct CancellableRequestWatch {
    CancellableRequestWatch(UniqueInternetHandle& request_handle_in,
                             std::function<bool()> should_cancel_in)
        : request_handle(request_handle_in),
          raw_handle(static_cast<HINTERNET>(request_handle_in.get())),
          should_cancel(std::move(should_cancel_in)) {
        if (raw_handle && should_cancel) {
            watcher = std::thread([this]() {
                while (!done.load(std::memory_order_acquire)) {
                    if (should_cancel && should_cancel()) {
                        if (WinHttpCloseHandle(raw_handle)) {
                            closed_by_cancel.store(true, std::memory_order_release);
                        }
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
    }

    CancellableRequestWatch(const CancellableRequestWatch&) = delete;
    CancellableRequestWatch& operator=(const CancellableRequestWatch&) = delete;

    ~CancellableRequestWatch() {
        done.store(true, std::memory_order_release);
        if (watcher.joinable()) {
            watcher.join();
        }
        if (closed_by_cancel.load(std::memory_order_acquire)) {
            request_handle.release();
        }
    }

    UniqueInternetHandle& request_handle;
    HINTERNET raw_handle = nullptr;
    std::function<bool()> should_cancel;
    std::atomic<bool> done{false};
    std::atomic<bool> closed_by_cancel{false};
    std::thread watcher;
};

struct ParsedUrl {
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring host;
    std::wstring path;
};

ParsedUrl CrackUrl(const std::string& url_utf8) {
    std::wstring url = Utf8ToWide(url_utf8);

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);

    wchar_t host[2048] = {};
    wchar_t path[4096] = {};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        throw std::runtime_error("Invalid URL: " + url_utf8);
    }

    ParsedUrl parsed;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.port = components.nPort;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    return parsed;
}

void ApplyCertificateFingerprintBypass(HINTERNET request, const std::string& fingerprint) {
    if (Trim(fingerprint).empty()) {
        return;
    }
    DWORD security_flags =
        SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));
}

std::string JoinChatCompletionsUrl(const std::string& base_url) {
    std::string trimmed = base_url;
    while (!trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed + "/chat/completions";
}

bool IsRetryableStatusCode(DWORD status_code) {
    switch (status_code) {
    case 408:
    case 409:
    case 425:
    case 429:
    case 500:
    case 502:
    case 503:
    case 504:
    case 529:
        return true;
    default:
        return false;
    }
}

int ComputeRetryDelayMs(int attempt_index, std::optional<int> retry_after_seconds) {
    if (retry_after_seconds && *retry_after_seconds > 0) {
        const int bounded = std::min(*retry_after_seconds, 30);
        return bounded * 1000;
    }

    const int capped_attempt = std::min(attempt_index, 3);
    return 1000 * (1 << capped_attempt);
}

std::optional<int> QueryRetryAfterSeconds(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return std::nullopt;
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Retry-After", buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return std::nullopt;
    }

    std::wstring value = TrimWide(buffer.c_str());
    if (value.empty()) {
        return std::nullopt;
    }

    try {
        size_t consumed = 0;
        const int seconds = std::stoi(value, &consumed);
        if (consumed == value.size()) {
            return seconds;
        }
    } catch (...) {
    }
    return std::nullopt;
}

const char* WinHttpErrorDescription(DWORD error_code) {
    switch (error_code) {
    case ERROR_WINHTTP_TIMEOUT:
        return "timeout";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "name not resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return "could not connect to provider";
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return "connection error: the provider closed or reset the connection, or TLS negotiation failed";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "TLS/secure channel failure";
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return "operation cancelled";
    case ERROR_WINHTTP_RESEND_REQUEST:
        return "request must be resent";
    default:
        return nullptr;
    }
}

std::string FormatWinHttpError(const std::string& prefix, DWORD error_code) {
    std::ostringstream stream;
    stream << prefix;
    if (error_code != ERROR_SUCCESS) {
        const char* description = WinHttpErrorDescription(error_code);
        if (description) {
            stream << " (" << description << "; WinHTTP error " << error_code << ")";
        } else {
            stream << " (WinHTTP error " << error_code << ")";
        }
    }
    return stream.str();
}

std::string SafeReportFilePart(std::string value) {
    if (value.empty()) return "unknown";
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    if (value.size() > 80) value.resize(80);
    return value;
}

std::string TailForReport(const std::string& value, size_t max_bytes = 4096) {
    if (value.size() <= max_bytes) return value;
    return value.substr(value.size() - max_bytes);
}

std::filesystem::path ProviderTransportReportRoot() {
    try {
        if (s_storage) {
            return s_storage->log_root() / "providers" / "transport_errors";
        }
    } catch (...) {}
    return std::filesystem::current_path() / ".log" / "providers" / "transport_errors";
}

std::string WriteWinHttpTransportReport(const ChatRequestOptions& request,
                                        const ParsedUrl& parsed,
                                        const std::string& operation,
                                        DWORD error_code,
                                        const std::string& formatted_error,
                                        int attempt,
                                        DWORD http_status,
                                        size_t bytes_received,
                                        size_t chunks_received,
                                        const std::string& response_buffer_tail) {
    try {
        const std::string timestamp = CurrentTimestampUtc();
        std::string file_stamp = timestamp;
        for (char& ch : file_stamp) {
            if (ch == ':' || ch == '-' || ch == 'T' || ch == 'Z') ch = '_';
        }

        const std::filesystem::path root = ProviderTransportReportRoot();
        std::filesystem::create_directories(root);
        const std::string file_name = file_stamp + "_" +
            SafeReportFilePart(request.provider.id.empty() ? request.provider.name : request.provider.id) + "_" +
            SafeReportFilePart(request.model.id) + "_" +
            SafeReportFilePart(operation) + "_winhttp_" + std::to_string(error_code) + ".json";
        const std::filesystem::path report_path = root / file_name;

        json report;
        report["timestamp_utc"] = timestamp;
        report["operation"] = operation;
        report["error"] = {
            {"winhttp_code", error_code},
            {"message", formatted_error},
            {"description", WinHttpErrorDescription(error_code) ? WinHttpErrorDescription(error_code) : ""},
        };
        report["request"] = {
            {"provider_id", request.provider.id},
            {"provider_name", request.provider.name},
            {"provider_type", request.provider.provider_type},
            {"model_id", request.model.id},
            {"model_display_name", request.model.display_name},
            {"binding_model_id", request.binding_model_id},
            {"binding_depth", request.binding_depth},
            {"message_count", request.messages.size()},
            {"system_prompt_bytes", request.system_prompt.size()},
            {"timeout_seconds", request.model_timeout_seconds},
        };
        report["http"] = {
            {"scheme", parsed.secure ? "https" : "http"},
            {"host", WideToUtf8(parsed.host)},
            {"port", parsed.port},
            {"path", WideToUtf8(parsed.path)},
            {"status_code", http_status},
            {"attempt", attempt},
        };
        report["stream"] = {
            {"bytes_received_before_error", bytes_received},
            {"chunks_received_before_error", chunks_received},
            {"buffer_tail", TailForReport(response_buffer_tail)},
        };
        report["interpretation"] =
            "WinHttpQueryDataAvailable/WinHttpReadData failed after the HTTP response was accepted. "
            "For WinHTTP 12030 this normally means the upstream provider, proxy, TLS layer, or network closed/reset "
            "the streaming connection before the provider sent the normal stream terminator.";

        {
            std::ofstream out(report_path, std::ios::binary | std::ios::trunc);
            out << report.dump(2);
        }

        const std::filesystem::path index_path = root.parent_path() / "transport_errors.log";
        {
            std::ofstream index(index_path, std::ios::binary | std::ios::app);
            index << timestamp
                  << " winhttp=" << error_code
                  << " operation=" << operation
                  << " provider=" << request.provider.name
                  << " model=" << request.model.id
                  << " status=" << http_status
                  << " bytes=" << bytes_received
                  << " chunks=" << chunks_received
                  << " report=" << WideToUtf8(report_path.wstring())
                  << "\n";
        }

        return WideToUtf8(report_path.wstring());
    } catch (...) {
        return {};
    }
}

std::string AppendReportPathToError(std::string error, const std::string& report_path) {
    if (!report_path.empty()) {
        error += " Transport debug report: " + report_path;
    }
    return error;
}

std::string FormatHttpErrorMessage(DWORD status_code, const std::string& details, int attempts) {
    std::ostringstream message;
    message << "HTTP " << status_code;
    if (status_code == 529) {
        message << " (provider overloaded)";
    }
    if (attempts > 1) {
        message << " after " << attempts << " attempts";
    }
    if (!details.empty()) {
        message << ": " << details;
    }
    return message.str();
}

std::string ExtractContentString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }

    if (value.is_array()) {
        std::string combined;
        for (const auto& item : value) {
            if (item.is_string()) {
                combined += item.get<std::string>();
            } else if (item.is_object()) {
                const std::string type = item.value("type", "");
                if (type == "text") {
                    combined += item.value("text", "");
                }
            }
        }
        return combined;
    }

    if (value.is_object()) {
        return value.value("text", "");
    }

    return {};
}

std::string ExtractTextLikeString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }

    if (value.is_array()) {
        std::string combined;
        for (const auto& item : value) {
            combined += ExtractTextLikeString(item);
        }
        return combined;
    }

    if (value.is_object()) {
        static const char* kTextKeys[] = {
            "text",
            "content",
            "summary",
            "reasoning_content",
            "thinking",
        };
        for (const char* key : kTextKeys) {
            if (value.contains(key)) {
                const std::string extracted = ExtractTextLikeString(value[key]);
                if (!extracted.empty()) {
                    return extracted;
                }
            }
        }
    }

    return {};
}

std::string ExtractReasoningString(const json& value) {
    if (!value.is_object()) {
        return {};
    }

    static const char* kReasoningKeys[] = {
        "reasoning_content",
        "reasoning",
        "thinking",
        "thoughts",
        "thought",
    };
    for (const char* key : kReasoningKeys) {
        if (value.contains(key)) {
            const std::string extracted = ExtractTextLikeString(value[key]);
            if (!extracted.empty()) {
                return extracted;
            }
        }
    }

    return {};
}

struct ToolArgumentsNormalization {
    bool valid = true;
    std::string normalized_json = "{}";
    std::string error;
};

ToolArgumentsNormalization NormalizeToolArgumentsJson(const std::string& arguments_json) {
    ToolArgumentsNormalization result;
    const std::string trimmed = Trim(arguments_json);
    if (trimmed.empty()) {
        return result;
    }

    try {
        const auto parsed = json::parse(trimmed);
        if (!parsed.is_object()) {
            result.valid = false;
            result.error = "Tool arguments must be a JSON object.";
            return result;
        }
        result.normalized_json = parsed.dump();
        return result;
    } catch (const std::exception& ex) {
        result.valid = false;
        result.error = ex.what();
        return result;
    } catch (...) {
        result.valid = false;
        result.error = "Tool arguments are not valid JSON.";
        return result;
    }
}

void NormalizeToolCall(ChatToolCall& tool_call) {
    if (tool_call.id.empty()) {
        tool_call.id = MakeId("call");
    }
    tool_call.original_arguments_json = tool_call.arguments_json;
    const auto normalization = NormalizeToolArgumentsJson(tool_call.arguments_json);
    tool_call.arguments_json = normalization.normalized_json;
    tool_call.arguments_valid = normalization.valid;
    tool_call.arguments_error = normalization.error;
}

json NormalizeToolCallsForProvider(const json& tool_calls) {
    if (!tool_calls.is_array()) {
        return json::array();
    }

    json normalized = json::array();
    for (const auto& item : tool_calls) {
        if (!item.is_object()) {
            continue;
        }

        json normalized_item = item;
        if (!normalized_item.contains("type") || !normalized_item["type"].is_string()) {
            normalized_item["type"] = "function";
        }
        if (!normalized_item.contains("function") || !normalized_item["function"].is_object()) {
            continue;
        }

        auto& function = normalized_item["function"];
        std::string raw_arguments;
        if (function.contains("arguments")) {
            if (function["arguments"].is_string()) {
                raw_arguments = function["arguments"].get<std::string>();
            } else {
                raw_arguments = function["arguments"].dump();
            }
        }
        function["arguments"] = NormalizeToolArgumentsJson(raw_arguments).normalized_json;
        normalized.push_back(std::move(normalized_item));
    }

    return normalized;
}

json SerializeToolCallsForProvider(const std::vector<ChatToolCall>& tool_calls) {
    json serialized = json::array();
    for (const auto& tool_call : tool_calls) {
        serialized.push_back({
            {"id", tool_call.id},
            {"type", "function"},
            {"function", {
                {"name", tool_call.name},
                {"arguments", tool_call.arguments_json.empty() ? "{}" : tool_call.arguments_json},
            }},
        });
    }
    return serialized;
}

std::vector<std::string> ToolCallIds(const json& tool_calls) {
    std::vector<std::string> ids;
    if (!tool_calls.is_array()) return ids;
    for (const auto& item : tool_calls) {
        if (!item.is_object()) continue;
        const std::string id = item.value("id", "");
        if (!id.empty()) ids.push_back(id);
    }
    return ids;
}

bool HasImmediateToolResultsForAll(
    const std::vector<MessageRecord>& messages,
    size_t assistant_index,
    const json& tool_calls) {
    std::unordered_set<std::string> expected;
    for (const auto& id : ToolCallIds(tool_calls)) {
        expected.insert(id);
    }
    if (expected.empty()) return false;

    for (size_t i = assistant_index + 1;
         i < messages.size() && messages[i].role == "tool";
         ++i) {
        expected.erase(messages[i].tool_call_id);
    }
    return expected.empty();
}

json BuildRequestBody(const ChatRequestOptions& request, bool stream, const std::vector<ChatToolDefinition>& tools = {}) {
    json body;
    body["model"] = request.model.id;
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.max_tokens;
    body["stream"] = stream;
    body["messages"] = json::array();

    const std::string reasoning = LowerAscii(Trim(request.model.default_reasoning_effort));
    if (reasoning == "none") body["reasoning_effort"] = nullptr;
    else if (!reasoning.empty()) body["reasoning_effort"] = reasoning == "xhigh" ? "high" : reasoning;

    if (!request.system_prompt.empty()) {
        body["messages"].push_back({
            {"role", "system"},
            {"content", request.system_prompt},
        });
    }

    std::unordered_set<std::string> expected_tool_result_ids;
    for (size_t message_index = 0; message_index < request.messages.size(); ++message_index) {
        const auto& message = request.messages[message_index];
        if (message.role == "tool") {
            if (message.tool_call_id.empty() ||
                expected_tool_result_ids.find(message.tool_call_id) == expected_tool_result_ids.end()) {
                continue;
            }
        } else if (!expected_tool_result_ids.empty()) {
            expected_tool_result_ids.clear();
        }

        json payload{
            {"role", message.role},
        };
        const std::string content = message.role == "assistant"
            ? message_sanitizer::StripRawProviderToolCallBlocks(message.content)
            : message.content;
        if (!content.empty() || message.role != "assistant") {
            payload["content"] = content;
        }
        if (!message.name.empty()) {
            payload["name"] = message.name;
        }
        if (!message.tool_call_id.empty()) {
            payload["tool_call_id"] = message.tool_call_id;
        }
        if (!message.tool_calls_json.empty()) {
            try {
                const auto normalized_tool_calls = NormalizeToolCallsForProvider(json::parse(message.tool_calls_json));
                if (!normalized_tool_calls.empty() &&
                    HasImmediateToolResultsForAll(request.messages, message_index, normalized_tool_calls)) {
                    payload["tool_calls"] = normalized_tool_calls;
                    for (const auto& id : ToolCallIds(normalized_tool_calls)) {
                        expected_tool_result_ids.insert(id);
                    }
                }
            } catch (...) {
            }
        }
        if (message.role == "assistant" &&
            !payload.contains("content") &&
            !payload.contains("tool_calls")) {
            continue;
        }
        body["messages"].push_back(std::move(payload));
        if (message.role == "tool") {
            expected_tool_result_ids.erase(message.tool_call_id);
        }
    }

    if (!tools.empty()) {
        body["tools"] = json::array();
        body["tool_choice"] = "auto";
        for (const auto& tool : tools) {
            json parameters = json{
                {"type", "object"},
                {"properties", json::object()},
            };
            if (!tool.parameters_json.empty()) {
                try {
                    parameters = json::parse(tool.parameters_json);
                } catch (...) {
                }
            }

            body["tools"].push_back({
                {"type", "function"},
                {"function", {
                    {"name", tool.name},
                    {"description", tool.description},
                    {"parameters", parameters},
                }},
            });
        }
    }

    return body;
}

std::vector<ChatToolCall> ExtractToolCalls(const json& message) {
    std::vector<ChatToolCall> tool_calls;
    if (!message.contains("tool_calls") || !message["tool_calls"].is_array()) {
        return tool_calls;
    }

    for (const auto& item : message["tool_calls"]) {
        if (!item.is_object()) {
            continue;
        }
        ChatToolCall tool_call;
        tool_call.id = item.value("id", "");
        if (item.contains("function") && item["function"].is_object()) {
            const auto& function = item["function"];
            tool_call.name = function.value("name", "");
            if (function.contains("arguments")) {
                if (function["arguments"].is_string()) {
                    tool_call.arguments_json = function["arguments"].get<std::string>();
                } else {
                    tool_call.arguments_json = function["arguments"].dump();
                }
            }
        }
        if (!tool_call.name.empty()) {
            NormalizeToolCall(tool_call);
            tool_calls.push_back(std::move(tool_call));
        }
    }

    return tool_calls;
}

std::optional<ProviderAuthRecord> LoadCodexOAuthAuthRecord(const ProviderConfig& provider) {
    if (!s_storage) {
        return std::nullopt;
    }
    if (!provider.oauth_credential_id.empty()) {
        if (auto record = s_storage->LoadProviderAuthRecord(provider.oauth_credential_id)) {
            return record;
        }
    }
    if (!provider.id.empty()) {
        return s_storage->LoadProviderAuthRecordForProvider(provider.id);
    }
    return std::nullopt;
}

std::string CodexOAuthBearerToken(const ProviderConfig& provider, std::string* error) {
    const auto record = LoadCodexOAuthAuthRecord(provider);
    if (!record) {
        if (error) {
            *error = "OpenAI OAuth provider is not signed in. Use Sign In for this provider, then try again.";
        }
        return {};
    }
    if (!record->access_token.empty()) {
        return record->access_token;
    }
    if (!record->api_key.empty()) {
        return record->api_key;
    }
    if (error) {
        *error = "OpenAI OAuth provider auth record does not contain an access token. Sign in again.";
    }
    return {};
}

json BuildCodexResponsesInputItemForMessage(const MessageRecord& message) {
    const std::string role = message.role == "assistant" ? "assistant" : "user";
    const std::string content = message.role == "assistant"
        ? message_sanitizer::StripRawProviderToolCallBlocks(message.content)
        : message.content;
    return json{
        {"role", role},
        {"content", content},
    };
}

bool HasPrefix(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string StableHexHash(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << hash;
    return stream.str();
}

std::string SanitizeCodexIdSuffix(const std::string& value) {
    std::string suffix;
    suffix.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            suffix.push_back(static_cast<char>(ch));
        } else {
            suffix.push_back('_');
        }
    }
    while (!suffix.empty() && suffix.front() == '_') {
        suffix.erase(suffix.begin());
    }
    return suffix.empty() ? StableHexHash(value) : suffix;
}

std::string CodexResponsesIdSuffix(const std::string& source_id) {
    const std::string trimmed = Trim(source_id);
    if (HasPrefix(trimmed, "call_")) {
        return SanitizeCodexIdSuffix(trimmed.substr(5));
    }
    if (HasPrefix(trimmed, "fc_")) {
        return SanitizeCodexIdSuffix(trimmed.substr(3));
    }
    const std::string sanitized = SanitizeCodexIdSuffix(trimmed);
    return sanitized + "_" + StableHexHash(trimmed);
}

std::string CodexResponsesCallId(const std::string& source_id) {
    const std::string trimmed = Trim(source_id);
    if (HasPrefix(trimmed, "call_")) {
        return trimmed;
    }
    return "call_" + CodexResponsesIdSuffix(trimmed);
}

std::string CodexResponsesFunctionCallItemId(const std::string& source_id) {
    const std::string trimmed = Trim(source_id);
    if (HasPrefix(trimmed, "fc_")) {
        return trimmed;
    }
    return "fc_" + CodexResponsesIdSuffix(trimmed);
}

void AppendCodexResponsesHistoryItem(const MessageRecord& message, json& input) {
    if (message.role == "tool") {
        if (message.tool_call_id.empty()) {
            return;
        }
        input.push_back({
            {"type", "function_call_output"},
            {"call_id", CodexResponsesCallId(message.tool_call_id)},
            {"output", message.content},
        });
        return;
    }

    if (message.role == "system") {
        if (!message.content.empty()) {
            input.push_back({
                {"role", "user"},
                {"content", "System instruction: " + message.content},
            });
        }
        return;
    }

    const std::string content = message.role == "assistant"
        ? message_sanitizer::StripRawProviderToolCallBlocks(message.content)
        : message.content;
    if (!content.empty() || message.role != "assistant") {
        input.push_back(BuildCodexResponsesInputItemForMessage(message));
    }

    if (message.role != "assistant" || message.tool_calls_json.empty()) {
        return;
    }

    try {
        const auto tool_calls = NormalizeToolCallsForProvider(json::parse(message.tool_calls_json));
        if (!tool_calls.is_array()) {
            return;
        }
        for (const auto& item : tool_calls) {
            if (!item.is_object() || !item.contains("function") || !item["function"].is_object()) {
                continue;
            }
            const auto& function = item["function"];
            const std::string name = function.value("name", "");
            if (name.empty()) {
                continue;
            }
            const std::string arguments = function.value("arguments", "{}");
            const std::string source_id = item.value("id", name + ":" + arguments);
            input.push_back({
                {"type", "function_call"},
                {"id", CodexResponsesFunctionCallItemId(source_id)},
                {"call_id", CodexResponsesCallId(source_id)},
                {"name", name},
                {"arguments", arguments},
                {"status", "completed"},
            });
        }
    } catch (...) {
    }
}

json BuildCodexResponsesBody(const ChatRequestOptions& request,
                             const std::vector<ChatToolDefinition>& tools) {
    json body;
    body["model"] = request.model.id;
    body["instructions"] = request.system_prompt.empty()
        ? "You are a helpful assistant."
        : request.system_prompt;
    body["stream"] = true;
    body["store"] = request.provider.oauth_store_remote_history;
    body["input"] = json::array();

    for (const auto& message : request.messages) {
        AppendCodexResponsesHistoryItem(message, body["input"]);
    }
    if (body["input"].empty()) {
        body["input"].push_back({
            {"role", "user"},
            {"content", "Reply with a short acknowledgement."},
        });
    }

    const std::string reasoning = LowerAscii(Trim(request.model.default_reasoning_effort));
    if (!reasoning.empty()) {
        body["reasoning"] = json{{"effort", reasoning}};
        if (reasoning != "none") {
            body["reasoning"]["summary"] = "auto";
        }
    }
    const std::string verbosity = LowerAscii(Trim(request.model.default_text_verbosity));
    if (!verbosity.empty()) {
        body["text"] = json{{"verbosity", verbosity}};
    }

    if (!tools.empty()) {
        body["tools"] = json::array();
        body["tool_choice"] = "auto";
        body["parallel_tool_calls"] = true;
        for (const auto& tool : tools) {
            json parameters = json{
                {"type", "object"},
                {"properties", json::object()},
            };
            if (!tool.parameters_json.empty()) {
                try {
                    parameters = json::parse(tool.parameters_json);
                } catch (...) {
                }
            }

            body["tools"].push_back({
                {"type", "function"},
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", parameters},
            });
        }
    }

    return body;
}

struct CodexResponsesStreamState {
    ChatCompletionResult result;
    std::unordered_map<std::string, ChatToolCall> tool_calls_by_item;
    std::vector<std::string> tool_call_order;
    bool thinking_emitted = false;
    bool thinking_closed = false;
    std::string last_activity_status;
};

void EmitCodexActivity(CodexResponsesStreamState& state,
                       const std::function<void(const std::string&, const std::string&)>& on_activity_status,
                       const std::string& status,
                       const std::string& message) {
    if (!on_activity_status || status.empty() || status == state.last_activity_status) {
        return;
    }
    state.last_activity_status = status;
    on_activity_status(status, message);
}

void AppendCodexReasoningDelta(CodexResponsesStreamState& state,
                               const std::string& delta,
                               const std::function<void(const std::string&)>& on_delta) {
    if (delta.empty()) {
        return;
    }
    state.result.thinking_text += delta;
    if (!state.thinking_emitted) {
        const std::string tag = "<think>" + delta;
        state.thinking_emitted = true;
        state.result.assistant_text += tag;
        if (on_delta) {
            on_delta(tag);
        }
        return;
    }
    state.result.assistant_text += delta;
    if (on_delta) {
        on_delta(delta);
    }
}

void AppendCodexTextDelta(CodexResponsesStreamState& state,
                          const std::string& delta,
                          const std::function<void(const std::string&)>& on_delta) {
    if (delta.empty()) {
        return;
    }
    std::string outbound = delta;
    if (state.thinking_emitted && !state.thinking_closed) {
        outbound = "</think>\n\n" + delta;
        state.thinking_closed = true;
    }
    state.result.assistant_text += outbound;
    if (on_delta) {
        on_delta(outbound);
    }
}

void MergeCodexFunctionCallItem(CodexResponsesStreamState& state, const json& item) {
    if (!item.is_object() || item.value("type", "") != "function_call") {
        return;
    }

    const std::string item_id = item.value("id", item.value("call_id", ""));
    if (item_id.empty()) {
        return;
    }

    ChatToolCall& tool_call = state.tool_calls_by_item[item_id];
    if (std::find(state.tool_call_order.begin(), state.tool_call_order.end(), item_id) == state.tool_call_order.end()) {
        state.tool_call_order.push_back(item_id);
    }

    const std::string call_id = item.value("call_id", "");
    if (!call_id.empty()) {
        tool_call.id = call_id;
    } else if (tool_call.id.empty()) {
        tool_call.id = item_id;
    }
    const std::string name = item.value("name", "");
    if (!name.empty()) {
        tool_call.name = name;
    }
    if (item.contains("arguments")) {
        if (item["arguments"].is_string()) {
            tool_call.arguments_json = item["arguments"].get<std::string>();
        } else {
            tool_call.arguments_json = item["arguments"].dump();
        }
    }
}

void AppendCodexReasoningSummaryFromItem(CodexResponsesStreamState& state,
                                         const json& item,
                                         const std::function<void(const std::string&)>& on_delta) {
    if (!item.is_object() || item.value("type", "") != "reasoning" ||
        !state.result.thinking_text.empty()) {
        return;
    }
    std::string summary;
    if (item.contains("summary")) {
        summary = ExtractTextLikeString(item["summary"]);
    }
    if (summary.empty() && item.contains("content")) {
        summary = ExtractTextLikeString(item["content"]);
    }
    AppendCodexReasoningDelta(state, summary, on_delta);
}

void AppendCodexFunctionCallArgumentsDelta(CodexResponsesStreamState& state, const json& chunk) {
    const std::string item_id = chunk.value("item_id", "");
    if (item_id.empty()) {
        return;
    }
    ChatToolCall& tool_call = state.tool_calls_by_item[item_id];
    if (std::find(state.tool_call_order.begin(), state.tool_call_order.end(), item_id) == state.tool_call_order.end()) {
        state.tool_call_order.push_back(item_id);
    }
    if (tool_call.id.empty()) {
        tool_call.id = item_id;
    }
    tool_call.arguments_json += chunk.value("delta", "");
}

void FinalizeCodexResponsesResult(CodexResponsesStreamState& state) {
    if (state.thinking_emitted && !state.thinking_closed) {
        state.result.assistant_text += "</think>\n\n";
        state.thinking_closed = true;
    }

    state.result.tool_calls.clear();
    for (const auto& item_id : state.tool_call_order) {
        auto it = state.tool_calls_by_item.find(item_id);
        if (it == state.tool_calls_by_item.end() || it->second.name.empty()) {
            continue;
        }
        ChatToolCall tool_call = it->second;
        NormalizeToolCall(tool_call);
        state.result.tool_calls.push_back(std::move(tool_call));
    }

    json raw_message{
        {"role", "assistant"},
    };
    if (!state.result.assistant_text.empty()) {
        raw_message["content"] = state.result.assistant_text;
    }
    if (!state.result.thinking_text.empty()) {
        raw_message["thinking"] = state.result.thinking_text;
    }
    if (!state.result.tool_calls.empty()) {
        raw_message["tool_calls"] = SerializeToolCallsForProvider(state.result.tool_calls);
    }
    state.result.raw_message_json = raw_message.dump(2);
    state.result.message.role = "assistant";
    state.result.message.content = state.result.assistant_text;
}

std::string ExtractErrorMessage(const std::string& body);

void ProcessCodexResponsesSsePayload(CodexResponsesStreamState& state,
                                     const std::string& payload_text,
                                     const std::function<void(const std::string&)>& on_delta,
                                     const std::function<void(const std::string&, const std::string&)>& on_activity_status) {
    const auto payload = json::parse(payload_text);
    const std::string type = payload.value("type", "");

    if (type == "response.created" || type == "response.in_progress") {
        EmitCodexActivity(state, on_activity_status,
            "chatgpt_codex_working",
            "ChatGPT Codex accepted the request and is working...");
        return;
    }
    if (type == "response.output_text.delta") {
        EmitCodexActivity(state, on_activity_status,
            "receiving_response",
            "Receiving model response...");
        AppendCodexTextDelta(state, payload.value("delta", ""), on_delta);
        return;
    }
    if (type == "response.reasoning_summary_text.delta" ||
        type == "response.reasoning_text.delta") {
        EmitCodexActivity(state, on_activity_status,
            "chatgpt_codex_reasoning",
            "Receiving ChatGPT Codex reasoning summary...");
        AppendCodexReasoningDelta(state, payload.value("delta", ""), on_delta);
        return;
    }
    if (type == "response.function_call_arguments.delta") {
        EmitCodexActivity(state, on_activity_status,
            "preparing_tool_call",
            "ChatGPT Codex is preparing a tool call...");
        AppendCodexFunctionCallArgumentsDelta(state, payload);
        return;
    }
    if ((type == "response.output_item.done" || type == "response.output_item.added") &&
        payload.contains("item")) {
        const auto& item = payload["item"];
        const std::string item_type = item.is_object() ? item.value("type", "") : "";
        if (item_type == "reasoning") {
            EmitCodexActivity(state, on_activity_status,
                "chatgpt_codex_reasoning",
                "ChatGPT Codex is reasoning...");
            if (type == "response.output_item.done") {
                AppendCodexReasoningSummaryFromItem(state, item, on_delta);
            }
            return;
        }
        if (item_type == "function_call") {
            EmitCodexActivity(state, on_activity_status,
                "preparing_tool_call",
                "ChatGPT Codex is preparing a tool call...");
        }
        MergeCodexFunctionCallItem(state, item);
        return;
    }
    if (type == "response.completed") {
        EmitCodexActivity(state, on_activity_status,
            "response_complete",
            "Response complete.");
        state.result.success = true;
        state.result.finish_reason = "stop";
        return;
    }
    if (type == "response.failed" || type == "error") {
        state.result.error = ExtractErrorMessage(payload_text);
        if (state.result.error.empty() && payload.contains("response") && payload["response"].is_object()) {
            const auto& response = payload["response"];
            if (response.contains("error")) {
                state.result.error = ExtractTextLikeString(response["error"]);
            }
        }
        if (state.result.error.empty()) {
            state.result.error = "OpenAI OAuth Responses request failed.";
        }
    }
}

std::string ExtractErrorMessage(const std::string& body) {
    try {
        const auto payload = json::parse(body);
        if (payload.contains("error")) {
            const auto& error = payload["error"];
            if (error.is_string()) {
                return error.get<std::string>();
            }
            if (error.is_object()) {
                return error.value("message", body);
            }
        }
        if (payload.contains("detail")) {
            const auto& detail = payload["detail"];
            if (detail.is_string()) {
                return detail.get<std::string>();
            }
            if (detail.is_object() || detail.is_array()) {
                return detail.dump();
            }
        }
        if (payload.contains("message") && payload["message"].is_string()) {
            return payload["message"].get<std::string>();
        }
    } catch (...) {
    }
    return body;
}

std::string ReadEntireResponse(HINTERNET request,
                               const std::function<bool()>& should_cancel = {},
                               bool* cancelled = nullptr) {
    std::string response;
    if (cancelled) {
        *cancelled = false;
    }

    auto mark_cancelled = [&]() {
        if (cancelled) {
            *cancelled = true;
        }
    };

    while (true) {
        if (IsCancelRequested(should_cancel)) {
            mark_cancelled();
            break;
        }

        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            if (IsCancelRequested(should_cancel)) {
                mark_cancelled();
            }
            break;
        }
        if (available == 0) {
            break;
        }

        if (IsCancelRequested(should_cancel)) {
            mark_cancelled();
            break;
        }

        std::vector<char> bytes(static_cast<size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data(), available, &read)) {
            if (IsCancelRequested(should_cancel)) {
                mark_cancelled();
            }
            break;
        }
        response.append(bytes.data(), bytes.data() + read);
    }

    return response;
}
DWORD QueryStatusCode(HINTERNET request) {
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);
    return status_code;
}

ChatCompletionResult RunOpenAICodexOAuthCompletion(
    const ChatRequestOptions& request,
    const std::vector<ChatToolDefinition>& tools,
    const std::function<void(const std::string&)>& on_delta,
    const std::function<void(const std::string&, const std::string&)>& on_activity_status,
    const std::function<bool()>& should_cancel = {}) {
    ChatCompletionResult final_result;

    std::string token_error;
    const std::string bearer_token = CodexOAuthBearerToken(request.provider, &token_error);
    if (bearer_token.empty()) {
        final_result.error = token_error.empty()
            ? "OpenAI OAuth provider is not signed in."
            : token_error;
        return final_result;
    }

    constexpr const char* kCodexResponsesUrl = "https://chatgpt.com/backend-api/codex/responses";
    const ParsedUrl parsed = CrackUrl(kCodexResponsesUrl);
    const std::string body = DumpJsonForProviderRequest(
        BuildCodexResponsesBody(request, tools), "OpenAI OAuth Responses request body");
    if (on_activity_status) {
        on_activity_status("chatgpt_codex_requesting", "Sending request to ChatGPT Codex...");
    }
    constexpr int kMaxAttempts = 4;
    std::string last_error;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        if (IsCancelRequested(should_cancel)) {
            final_result.error = "Cancelled.";
            return final_result;
        }

        UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            final_result.error = last_error;
            return final_result;
        }

        UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
        if (!connection) {
            last_error = FormatWinHttpError("Failed to connect to ChatGPT Codex backend.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            final_result.error = last_error;
            return final_result;
        }

        const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
        UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request_handle) {
            final_result.error = FormatWinHttpError("Failed to create ChatGPT Codex backend request.", GetLastError());
            return final_result;
        }
        CancellableRequestWatch cancel_watch(request_handle, should_cancel);

        const DWORD timeout_ms = request.model_timeout_seconds > 0
            ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
            : 0;
        WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);

        std::wstring headers =
            L"Content-Type: application/json\r\n"
            L"Accept: text/event-stream\r\n"
            L"Authorization: Bearer ";
        headers += Utf8ToWide(bearer_token);
        headers += L"\r\n";

        if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()),
                headers.c_str(),
                static_cast<DWORD>(headers.size()),
                reinterpret_cast<LPVOID>(const_cast<char*>(body.data())),
                static_cast<DWORD>(body.size()),
                static_cast<DWORD>(body.size()),
                0)) {
            if (IsCancelRequested(should_cancel)) {
                final_result.error = "Cancelled.";
                return final_result;
            }
            last_error = FormatWinHttpError("Failed to send ChatGPT Codex backend request.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            final_result.error = last_error;
            return final_result;
        }

        if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
            if (IsCancelRequested(should_cancel)) {
                final_result.error = "Cancelled.";
                return final_result;
            }
            last_error = FormatWinHttpError("Failed to receive ChatGPT Codex backend response.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            final_result.error = last_error;
            return final_result;
        }

        const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
        if (status_code < 200 || status_code >= 300) {
            const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
            if (IsCancelRequested(should_cancel)) {
                final_result.error = "Cancelled.";
                return final_result;
            }
            const std::string details = ExtractErrorMessage(error_body);
            last_error = FormatHttpErrorMessage(status_code, details, attempt);
            if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                continue;
            }
            final_result.error = last_error;
            return final_result;
        }

        CodexResponsesStreamState state;
        std::string response_buffer;
        while (true) {
            if (IsCancelRequested(should_cancel)) {
                final_result.error = "Cancelled.";
                return final_result;
            }

            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                if (IsCancelRequested(should_cancel)) {
                    final_result.error = "Cancelled.";
                    return final_result;
                }
                last_error = FormatWinHttpError("Failed while streaming ChatGPT Codex backend response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    break;
                }
                final_result.error = last_error;
                return final_result;
            }
            if (available == 0) {
                break;
            }

            std::vector<char> bytes(static_cast<size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                if (IsCancelRequested(should_cancel)) {
                    final_result.error = "Cancelled.";
                    return final_result;
                }
                last_error = FormatWinHttpError("Failed to read ChatGPT Codex backend response chunk.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    break;
                }
                final_result.error = last_error;
                return final_result;
            }

            response_buffer.append(bytes.data(), bytes.data() + read);
            size_t line_end = std::string::npos;
            while ((line_end = response_buffer.find('\n')) != std::string::npos) {
                std::string line = response_buffer.substr(0, line_end);
                response_buffer.erase(0, line_end + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty() || line.rfind("data:", 0) != 0) {
                    continue;
                }

                const std::string payload = Trim(line.substr(5));
                if (payload.empty() || payload == "[DONE]") {
                    continue;
                }
                try {
                    ProcessCodexResponsesSsePayload(state, payload, on_delta, on_activity_status);
                } catch (const std::exception& ex) {
                    final_result.error = std::string("Failed to parse ChatGPT Codex response stream: ") + ex.what();
                    return final_result;
                } catch (...) {
                    final_result.error = "Failed to parse ChatGPT Codex response stream.";
                    return final_result;
                }

                if (!state.result.error.empty()) {
                    final_result = state.result;
                    return final_result;
                }
            }
        }

        if (!last_error.empty() && !state.result.success && attempt < kMaxAttempts) {
            continue;
        }
        FinalizeCodexResponsesResult(state);
        if (!state.result.success && state.result.error.empty()) {
            state.result.success = !state.result.assistant_text.empty() || !state.result.tool_calls.empty();
        }
        if (!state.result.success && state.result.error.empty()) {
            state.result.error = "ChatGPT Codex backend ended the response without a completion event.";
        }
        return state.result;
    }

    final_result.error = last_error.empty() ? "The ChatGPT Codex backend request failed after multiple attempts." : last_error;
    return final_result;
}

ChatExecutionResult RunRequest(const ChatRequestOptions& request, bool stream, const std::function<void(const std::string&)>& on_delta, const std::function<bool()>& should_cancel = {}) {
    const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
    AppendDetail("RunRequest: POST " + url);
    AppendDetail("  stream=" + std::to_string(stream) + " provider=" + request.provider.name + " model=" + request.model.id);
    const ParsedUrl parsed = CrackUrl(url);
    const std::string body = DumpJsonForProviderRequest(
        BuildRequestBody(request, stream), "chat request body");
    AppendDetail("  body: " + body);
    AppendDetail("  parsed host=" + WideToUtf8(parsed.host) + " port=" + std::to_string(parsed.port) + " path=" + WideToUtf8(parsed.path));
    constexpr int kMaxAttempts = 4;
    std::string last_error;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        AppendDetail("  attempt " + std::to_string(attempt) + "/" + std::to_string(kMaxAttempts));
        ChatExecutionResult result;
        if (IsCancelRequested(should_cancel)) {
            result.error = "Cancelled.";
            return result;
        }

        UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
        if (!connection) {
            last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
        UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request_handle) {
            result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
            return result;
        }
        CancellableRequestWatch cancel_watch(request_handle, should_cancel);

        // Apply per-project timeout: 0 = infinite (default), otherwise send+receive in ms.
        const DWORD timeout_ms = request.model_timeout_seconds > 0
            ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
            : 0;
        WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);
        ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

        std::wstring headers = L"Content-Type: application/json\r\nAccept: ";
        headers += stream ? L"text/event-stream" : L"application/json";
        headers += L"\r\n";
        if (!request.provider.api_key.empty()) {
            headers += L"Authorization: Bearer ";
            headers += Utf8ToWide(request.provider.api_key);
            headers += L"\r\n";
        }

        if (IsCancelRequested(should_cancel)) {
            result.error = "Cancelled.";
            return result;
        }

        if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
            AppendDetail("  FAIL attempt " + std::to_string(attempt) + ": WinHttpSendRequest: " + last_error);
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        if (IsCancelRequested(should_cancel)) {
            result.error = "Cancelled.";
            return result;
        }

        if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
            AppendDetail("  FAIL attempt " + std::to_string(attempt) + ": WinHttpReceiveResponse: " + last_error);
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        if (IsCancelRequested(should_cancel)) {
            result.error = "Cancelled.";
            return result;
        }

        const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
        AppendDetail("  attempt " + std::to_string(attempt) + " HTTP status=" + std::to_string(status_code));
        if (status_code < 200 || status_code >= 300) {
            const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            const std::string details = ExtractErrorMessage(error_body);
            last_error = FormatHttpErrorMessage(status_code, details, attempt);
            AppendDetail("  FAIL attempt " + std::to_string(attempt) + ": " + last_error);

            if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                AppendDetail("  retrying...");
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                continue;
            }

            result.error = last_error;
            return result;
        }

        if (!stream) {
            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            AppendDetail("  response length=" + std::to_string(response.size()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    if (choice.contains("message")) {
                        const std::string reasoning = ExtractReasoningString(choice["message"]);
                        std::string content = ExtractContentString(choice["message"].value("content", json{}));
                        if (!reasoning.empty()) {
                            result.thinking_text = reasoning;
                            content = "\u003cthink\u003e" + reasoning + "\u003c/think\u003e\n\n" + content;
                        }
                        result.full_text = content;
                    }
                }
                if (result.full_text.empty()) {
                    result.full_text = "Connection succeeded.";
                }
                return result;
            } catch (...) {
                result.error = "Received a non-JSON response from the provider.";
                return result;
            }
        }

        std::string response_buffer;
        size_t streamed_bytes = 0;
        size_t streamed_chunks = 0;
        bool thinking_emitted = false;
        bool thinking_closed = false;
        std::string finish_reason;

        while (true) {
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                const DWORD error_code = GetLastError();
                result.error = FormatWinHttpError("Failed while streaming response.", error_code);
                const std::string report_path = WriteWinHttpTransportReport(
                    request,
                    parsed,
                    "stream_chat_query_data_available",
                    error_code,
                    result.error,
                    attempt,
                    status_code,
                    streamed_bytes,
                    streamed_chunks,
                    response_buffer);
                result.error = AppendReportPathToError(result.error, report_path);
                return result;
            }
            if (available == 0) {
                break;
            }

            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            std::vector<char> bytes(static_cast<size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                const DWORD error_code = GetLastError();
                result.error = FormatWinHttpError("Failed to read a streaming response chunk.", error_code);
                const std::string report_path = WriteWinHttpTransportReport(
                    request,
                    parsed,
                    "stream_chat_read_data",
                    error_code,
                    result.error,
                    attempt,
                    status_code,
                    streamed_bytes,
                    streamed_chunks,
                    response_buffer);
                result.error = AppendReportPathToError(result.error, report_path);
                return result;
            }

            streamed_bytes += read;
            ++streamed_chunks;
            response_buffer.append(bytes.data(), bytes.data() + read);

            size_t line_end = std::string::npos;
            while ((line_end = response_buffer.find('\n')) != std::string::npos) {
                std::string line = response_buffer.substr(0, line_end);
                response_buffer.erase(0, line_end + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty() || line.rfind("data:", 0) != 0) {
                    continue;
                }

                std::string payload = Trim(line.substr(5));
                if (payload == "[DONE]") {
                    result.success = true;
                    return result;
                }

                try {
                    const auto chunk = json::parse(payload);
                    if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) {
                        continue;
                    }

                    const auto& choice = chunk["choices"][0];
                    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                        finish_reason = choice["finish_reason"].get<std::string>();
                    }
                    if (choice.contains("delta")) {
                        const auto& delta = choice["delta"];
                        const std::string reasoning_piece = ExtractReasoningString(delta);
                        const std::string content_piece = delta.contains("content")
                            ? ExtractContentString(delta["content"]) : "";
                        if (!reasoning_piece.empty()) {
                            result.thinking_text += reasoning_piece;
                            if (!thinking_emitted) {
                                const std::string tag = "\u003cthink\u003e" + reasoning_piece;
                                thinking_emitted = true;
                                result.full_text += tag;
                                on_delta(tag);
                            } else {
                                result.full_text += reasoning_piece;
                                on_delta(reasoning_piece);
                            }
                        }
                        if (!content_piece.empty()) {
                            if (thinking_emitted && !thinking_closed) {
                                const std::string close = "\u003c/think\u003e\n\n";
                                thinking_closed = true;
                                result.full_text += close + content_piece;
                                on_delta(close + content_piece);
                            } else {
                                result.full_text += content_piece;
                                on_delta(content_piece);
                            }
                        }
                    }
                } catch (...) {
                    result.error = "Failed to parse a streaming SSE chunk.";
                    return result;
                }
            }
        }

        if (thinking_emitted && !thinking_closed) {
            const std::string close = "</think>\n\n";
            result.full_text += close;
        }

        if (finish_reason.empty()) {
            const std::string detail =
                "Streaming response ended without the provider completion marker.";
            const std::string report_path = WriteWinHttpTransportReport(
                request,
                parsed,
                "stream_chat_missing_done",
                ERROR_SUCCESS,
                detail,
                attempt,
                status_code,
                streamed_bytes,
                streamed_chunks,
                response_buffer);
            result.error = AppendReportPathToError(detail, report_path);
            return result;
        }

        result.success = true;
        return result;
    }

    ChatExecutionResult result;
    result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
    return result;
}
}  // namespace

// Minimal stub: tests an OpenAI-compatible embedding endpoint at /embeddings.
bool TestOpenAICompatibleEmbeddingConnection(const ProviderConfig& provider, const ModelConfig& model, std::string* message) {
    std::string base = provider.base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    const std::string url = base + "/embeddings";

    json body;
    body["model"] = model.id;
    body["input"] = "Diagnostic connection test.";

    try {
        const auto parsed = CrackUrl(url);
        auto session_h = WinHttpOpen(L"AgentEmbedTest/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_h) { if (message) *message = "WinHttpOpen failed"; return false; }
        auto conn_h = WinHttpConnect(session_h, parsed.host.c_str(), parsed.port, 0);
        if (!conn_h) { if (message) *message = "WinHttpConnect failed"; return false; }
        DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
        auto req_h = WinHttpOpenRequest(conn_h, L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!req_h) { if (message) *message = "WinHttpOpenRequest failed"; return false; }
        WinHttpSetTimeouts(req_h, 10000, 10000, 30000, 180000);

        const std::string body_str = DumpJsonForProviderRequest(body, "embedding request body");
        std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
        if (!provider.api_key.empty()) {
            headers += L"Authorization: Bearer ";
            headers += Utf8ToWide(provider.api_key);
            headers += L"\r\n";
        }

        if (!WinHttpSendRequest(req_h, headers.c_str(), static_cast<DWORD>(headers.size()),
                const_cast<char*>(body_str.data()), static_cast<DWORD>(body_str.size()),
                static_cast<DWORD>(body_str.size()), 0)) {
            if (message) *message = "POST request failed";
            WinHttpCloseHandle(req_h); WinHttpCloseHandle(conn_h); WinHttpCloseHandle(session_h);
            return false;
        }
        if (!WinHttpReceiveResponse(req_h, nullptr)) {
            if (message) *message = "Response receive failed";
            WinHttpCloseHandle(req_h); WinHttpCloseHandle(conn_h); WinHttpCloseHandle(session_h);
            return false;
        }

        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req_h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, WINHTTP_NO_HEADER_INDEX);
        std::string rsp;
        {
            std::vector<char> buf;
            DWORD avail;
            while (WinHttpQueryDataAvailable(req_h, &avail) && avail > 0) {
                size_t old = buf.size(); buf.resize(old + avail);
                DWORD read = 0;
                WinHttpReadData(req_h, buf.data() + old, avail, &read);
                buf.resize(old + read);
            }
            rsp.assign(buf.data(), buf.size());
        }
        WinHttpCloseHandle(req_h); WinHttpCloseHandle(conn_h); WinHttpCloseHandle(session_h);

        if (status < 200 || status >= 300) {
            if (message) *message = "HTTP " + std::to_string(status) + " from embedding endpoint";
            return false;
        }

        const auto payload = json::parse(rsp);
        if (payload.contains("data") && payload["data"].is_array() && !payload["data"].empty() &&
            payload["data"][0].contains("embedding") && payload["data"][0]["embedding"].is_array()) {
            const int dims = static_cast<int>(payload["data"][0]["embedding"].size());
            if (message) *message = "Embedding connection OK (" + std::to_string(dims) + " dimensions).";
            return true;
        }
        if (payload.contains("embedding") && payload["embedding"].is_array()) {
            const int dims = static_cast<int>(payload["embedding"].size());
            if (message) *message = "Embedding connection OK (" + std::to_string(dims) + " dimensions).";
            return true;
        }

        if (message) *message = "Response did not contain an embedding vector.";
        return false;
    } catch (const std::exception& ex) {
        if (message) *message = std::string("Embedding test error: ") + ex.what();
        return false;
    }
}

TestConnectionResult OpenAIClient::TestConnection(const ProviderConfig& provider, const ModelConfig& model) {
    TestConnectionResult result;
    std::string detail;
    SetTestDetailLog(&detail);
    detail = "Provider Test Connection diagnostic log\r\n";
    detail += "[" + CurrentTimestampUtc() + "] TestConnection called\r\n";
    detail += "  provider=" + provider.name + " type=" + provider.provider_type + "\r\n";
    detail += "  model=" + model.id + " (" + model.display_name + ") embedding=" + (model.supports_embedding ? "yes" : "no") + "\r\n";
    auto cleanup = [&]() { result.details_log = detail; SetTestDetailLog(nullptr); };
    try {
        if (model.supports_embedding) {
            detail += "  supports_embedding=true, dispatching to embedding test path\r\n";
            if (IsOllamaLocalProvider(provider)) {
                detail += "  IsOllamaLocalProvider=true, calling TestOllamaEmbeddingConnection\r\n";
                result.success = TestOllamaEmbeddingConnection(provider, model, &result.message);
            } else if (IsOllamaCloudProvider(provider)) {
                detail += "  IsOllamaCloudProvider=true, calling TestOllamaCloudEmbeddingConnection\r\n";
                result.success = TestOllamaCloudEmbeddingConnection(provider, model, &result.message);
            } else {
                detail += "  generic provider, calling TestOpenAICompatibleEmbeddingConnection\r\n";
                result.success = TestOpenAICompatibleEmbeddingConnection(provider, model, &result.message);
            }
            detail += "  result success=" + std::to_string(result.success) + " message=" + result.message + "\r\n";
            cleanup();
            return result;
        }

        if (NormalizeProviderType(provider.provider_type) == "ollama_cloud") {
            detail += "  ollama_cloud non-embedding path\r\n";
            if (!IsOllamaCloudModelAvailable(provider, model, &result.message)) {
                detail += "  model not available: " + result.message + "\r\n";
                result.success = false;
                cleanup();
                return result;
            }
            ChatRequestOptions request;
            request.provider = provider;
            request.model = model;
            request.temperature = 0.0;
            request.max_tokens = 8;
            request.messages.push_back(MessageRecord{"user", "Reply with the single word pong.", CurrentTimestampUtc()});
            detail += "  sending ping to Ollama Cloud /api/chat\r\n";
            const ChatExecutionResult response = RunOllamaCloudHttpChat(request, [](const std::string&) {}, {}, {});
            result.success = response.success;
            result.message = response.success ? response.full_text : response.error;
            detail += "  ping result success=" + std::to_string(result.success) + " message=" + result.message + "\r\n";
            cleanup();
            return result;
        }

        if (NormalizeProviderType(provider.provider_type) == "ollama_local") {
            detail += "  ollama_local non-embedding path\r\n";
            if (!IsOllamaModelAvailable(provider, model, &result.message)) {
                detail += "  model not available: " + result.message + "\r\n";
                result.success = false;
                cleanup();
                return result;
            }

            ChatRequestOptions request;
            request.provider = provider;
            request.model = model;
            request.temperature = 0.0;
            request.max_tokens = 8;
            request.messages.push_back(MessageRecord{"user", "Reply with the single word pong.", CurrentTimestampUtc()});
            detail += "  sending ping to Ollama /api/chat\r\n";
            const ChatExecutionResult response = RunOllamaLocalHttpChat(request, [](const std::string&) {}, {}, {});
            result.success = response.success;
            result.message = response.success ? response.full_text : response.error;
            detail += "  ping result success=" + std::to_string(result.success) + " message=" + result.message + "\r\n";
            cleanup();
            return result;
        }

        ChatRequestOptions request;
        request.provider = provider;
        request.model = model;
        request.temperature = 0.0;
        request.max_tokens = 8;
        request.messages.push_back(MessageRecord{"user", "Reply with the single word pong.", CurrentTimestampUtc()});
        if (IsOpenAICodexOAuthProvider(provider)) {
            detail += "  openai_codex_oauth path, sending ping to ChatGPT Codex backend\r\n";
            const ChatCompletionResult response = RunOpenAICodexOAuthCompletion(request, {}, [](const std::string&) {}, {}, {});
            result.success = response.success;
            result.message = response.success ? response.assistant_text : response.error;
        } else {
            detail += "  standard OpenAI-compatible non-embedding path\r\n";
            detail += "  sending ping\r\n";
            const ChatExecutionResult response = RunRequest(request, false, [](const std::string&) {}, {});
            result.success = response.success;
            result.message = response.success ? response.full_text : response.error;
        }
        detail += "  ping result success=" + std::to_string(result.success) + " message=" + result.message + "\r\n";
    } catch (const std::exception& ex) {
        result.success = false;
        result.message = ex.what();
        detail += "  EXCEPTION: " + std::string(ex.what()) + "\r\n";
    } catch (...) {
        result.success = false;
        result.message = "Unexpected error while testing the connection.";
        detail += "  EXCEPTION: unexpected\r\n";
    }
    cleanup();
    return result;
}

ChatExecutionResult OpenAIClient::StreamChat(const ChatRequestOptions& request,
                                           const std::function<void(const std::string&)>& on_delta,
                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
                                           const std::function<void(const std::string&, const std::string&)>& on_activity_status,
                                           const std::function<bool()>& should_cancel) {
    // Binding bypass: resolve to concrete target and acquire its gate directly
    if (request.model.is_binding_model && request.binding_depth < 8) {
        ChatRequestOptions resolved = request;
        std::vector<ProviderConfig> all_providers;
        {
            std::lock_guard<std::mutex> lock(GateMapMutex());
            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);
        }
        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {
            return StreamChat(resolved, on_delta, on_queue_status, on_activity_status, should_cancel);
        }
        ChatExecutionResult fail;
        fail.error = "Binding resolution failed: no target available for " + request.model.id;
        return fail;
    }
    GateSlot slot(request, GateDomain::Chat);
    if (!slot.Acquire(request.provider.name, on_queue_status, should_cancel)) {
        ChatExecutionResult result;
        result.error = IsCancelRequested(should_cancel)
            ? "Cancelled."
            : "Provider queue is full for " + request.provider.name + "/" + request.model.id + ".";
        return result;
    }
    try {
        if (IsCancelRequested(should_cancel)) {
            ChatExecutionResult result;
            result.error = "Cancelled.";
            return result;
        }
        if (IsOllamaLocalProvider(request.provider)) {
            return RunOllamaLocalHttpChat(request, on_delta, on_queue_status, on_activity_status, should_cancel);
        }
        if (IsOllamaCloudProvider(request.provider)) {
            return RunOllamaCloudHttpChat(request, on_delta, on_queue_status, on_activity_status, should_cancel);
        }
        if (IsOpenAICodexOAuthProvider(request.provider)) {
            const ChatCompletionResult completion =
                RunOpenAICodexOAuthCompletion(request, {}, on_delta, on_activity_status, should_cancel);
            ChatExecutionResult result;
            result.success = completion.success;
            result.full_text = completion.assistant_text;
            result.error = completion.error;
            result.thinking_text = completion.thinking_text;
            return result;
        }
        return RunRequest(request, true, on_delta, should_cancel);
    } catch (const std::exception& ex) {
        ChatExecutionResult result;
        result.error = ex.what();
        return result;
    } catch (...) {
        ChatExecutionResult result;
        result.error = "Unexpected error while sending the chat request.";
        return result;
    }
}

ChatCompletionResult OpenAIClient::CreateToolAwareCompletion(const ChatRequestOptions& request,
                                                           const std::vector<ChatToolDefinition>& tools,
                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
                                                           const std::function<void(const std::string&, const std::string&)>& on_activity_status,
                                                           const std::function<bool()>& should_cancel) {
    // Binding bypass: resolve to concrete target and acquire its gate directly
    if (request.model.is_binding_model && request.binding_depth < 8) {
        ChatRequestOptions resolved = request;
        std::vector<ProviderConfig> all_providers;
        {
            std::lock_guard<std::mutex> lock(GateMapMutex());
            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);
        }
        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {
            return CreateToolAwareCompletion(resolved, tools, on_queue_status, on_activity_status, should_cancel);
        }
        ChatCompletionResult fail;
        fail.error = "Binding resolution failed: no target available for " + request.model.id;
        return fail;
    }
    GateSlot slot(request, GateDomain::Chat);
    if (!slot.Acquire(request.provider.name, on_queue_status, should_cancel)) {
        ChatCompletionResult result;
        result.error = IsCancelRequested(should_cancel)
            ? "Cancelled."
            : "Provider queue is full for " + request.provider.name + "/" + request.model.id + ".";
        return result;
    }
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpToolPrompt(request, tools, {}, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOllamaCloudProvider(request.provider)) {
        return RunOllamaCloudHttpToolPrompt(request, tools, {}, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOpenAICodexOAuthProvider(request.provider)) {
        return RunOpenAICodexOAuthCompletion(request, tools, {}, on_activity_status, should_cancel);
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = DumpJsonForProviderRequest(
            BuildRequestBody(request, false, tools), "tool-aware chat request body");
        constexpr int kMaxAttempts = 4;
        std::string last_error;

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) {
                last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
            if (!connection) {
                last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
            UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            if (!request_handle) {
                result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
                return result;
            }
            CancellableRequestWatch cancel_watch(request_handle, should_cancel);


            // Apply per-project timeout: 0 = infinite (default), otherwise send+receive in ms.
            const DWORD timeout_ms = request.model_timeout_seconds > 0
                ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
                : 0;
            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
            if (status_code < 200 || status_code >= 300) {
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    result.finish_reason = choice.value("finish_reason", "");
                    if (choice.contains("message")) {
                        json raw_message = choice["message"];
                        const std::string reasoning = ExtractReasoningString(raw_message);
                        std::string content = ExtractContentString(raw_message.value("content", json{}));
                        if (!reasoning.empty()) {
                            result.thinking_text = reasoning;
                            content = "\u003cthink\u003e" + reasoning + "\u003c/think\u003e\n\n" + content;
                            raw_message["content"] = content;
                            raw_message["thinking"] = reasoning;
                        }
                        result.assistant_text = content;
                        result.tool_calls = ExtractToolCalls(raw_message);
                        if (!result.tool_calls.empty()) {
                            raw_message["tool_calls"] = SerializeToolCallsForProvider(result.tool_calls);
                        }
                        result.raw_message_json = raw_message.dump(2);
                    }
                }
                return result;
            } catch (...) {
                result.error = "Received a non-JSON response from the provider.";
                return result;
            }
        }

        result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    } catch (...) {
        result.error = "Unexpected error while sending the tool-aware chat request.";
        return result;
    }
}

ChatCompletionResult OpenAIClient::CreateSimpleCompletion(const ChatRequestOptions& request,
                                                        const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
                                                        const std::function<void(const std::string&, const std::string&)>& on_activity_status,
                                                        const std::function<bool()>& should_cancel) {
    // Binding bypass: resolve to concrete target and acquire its gate directly
    if (request.model.is_binding_model && request.binding_depth < 8) {
        ChatRequestOptions resolved = request;
        std::vector<ProviderConfig> all_providers;
        {
            std::lock_guard<std::mutex> lock(GateMapMutex());
            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);
        }
        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {
            return CreateSimpleCompletion(resolved, on_queue_status, on_activity_status, should_cancel);
        }
        ChatCompletionResult fail;
        fail.error = "Binding resolution failed: no target available for " + request.model.id;
        return fail;
    }
    GateSlot slot(request, GateDomain::Chat);
    if (!slot.Acquire(request.provider.name, on_queue_status, should_cancel)) {
        ChatCompletionResult result;
        result.error = IsCancelRequested(should_cancel)
            ? "Cancelled."
            : "Provider queue is full for " + request.provider.name + "/" + request.model.id + ".";
        return result;
    }
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpCompletion(request, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOllamaCloudProvider(request.provider)) {
        return RunOllamaCloudHttpCompletion(request, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOpenAICodexOAuthProvider(request.provider)) {
        return RunOpenAICodexOAuthCompletion(request, {}, {}, on_activity_status, should_cancel);
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = DumpJsonForProviderRequest(
            BuildRequestBody(request, false, {}), "chat request body");
        constexpr int kMaxAttempts = 4;
        std::string last_error;

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) {
                last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
            if (!connection) {
                last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
            UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            if (!request_handle) {
                result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
                return result;
            }
            CancellableRequestWatch cancel_watch(request_handle, should_cancel);


            // Apply per-project timeout: 0 = infinite (default), otherwise send+receive in ms.
            const DWORD timeout_ms = request.model_timeout_seconds > 0
                ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
                : 0;
            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
            if (status_code < 200 || status_code >= 300) {
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    result.finish_reason = choice.value("finish_reason", "");
                    if (choice.contains("message")) {
                        const json& msg_json = choice["message"];
                        const std::string reasoning = ExtractReasoningString(msg_json);
                        std::string content = ExtractContentString(msg_json.value("content", json{}));
                        if (!reasoning.empty()) {
                            result.thinking_text = reasoning;
                            content = "\u003cthink\u003e" + reasoning + "\u003c/think\u003e\n\n" + content;
                        }
                        result.message.role = msg_json.value("role", "assistant");
                        result.message.content = content;
                        result.assistant_text = content;
                    }
                }
                return result;
            } catch (...) {
                result.error = "Received a non-JSON response from the provider.";
                return result;
            }
        }

        result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    } catch (...) {
        result.error = "Unexpected error while sending the simple completion request.";
        return result;
    }
}

void OpenAIClient::SetProviderCache(const std::vector<ProviderConfig>& providers) {
    s_provider_cache = providers;
    ProviderRequestGate::Configure(providers);
}

std::optional<ProviderConfig> OpenAIClient::LookupProvider(const std::string& provider_id) {
    for (const auto& p : s_provider_cache) {
        if (p.id == provider_id) {
            return p;
        }
    }
    return std::nullopt;
}

ChatCompletionResult OpenAIClient::StreamToolAwareCompletion(const ChatRequestOptions& request,
                                                           const std::vector<ChatToolDefinition>& tools,
                                                           const std::function<void(const std::string&)>& on_delta,
                                                           const std::function<void(const ProviderQueueStatus&)>& on_queue_status,
                                                           const std::function<void(const std::string&, const std::string&)>& on_activity_status,
                                                           const std::function<bool()>& should_cancel) {
    // Binding bypass: resolve to concrete target and acquire its gate directly
    if (request.model.is_binding_model && request.binding_depth < 8) {
        ChatRequestOptions resolved = request;
        std::vector<ProviderConfig> all_providers;
        {
            std::lock_guard<std::mutex> lock(GateMapMutex());
            for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second);
        }
        if (ResolveBindingTarget(all_providers, request.model, request, &resolved, on_queue_status)) {
            return StreamToolAwareCompletion(resolved, tools, on_delta, on_queue_status, on_activity_status, should_cancel);
        }
        ChatCompletionResult fail;
        fail.error = "Binding resolution failed: no target available for " + request.model.id;
        return fail;
    }
    GateSlot slot(request, GateDomain::Chat);
    if (!slot.Acquire(request.provider.name, on_queue_status, should_cancel)) {
        ChatCompletionResult result;
        result.error = IsCancelRequested(should_cancel)
            ? "Cancelled."
            : "Provider queue is full for " + request.provider.name + "/" + request.model.id + ".";
        return result;
    }
    if (IsCancelRequested(should_cancel)) {
        ChatCompletionResult result;
        result.error = "Cancelled.";
        return result;
    }
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpToolPrompt(request, tools, on_delta, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOllamaCloudProvider(request.provider)) {
        return RunOllamaCloudHttpToolPrompt(request, tools, on_delta, on_queue_status, on_activity_status, should_cancel);
    }
    if (IsOpenAICodexOAuthProvider(request.provider)) {
        return RunOpenAICodexOAuthCompletion(request, tools, on_delta, on_activity_status, should_cancel);
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = DumpJsonForProviderRequest(
            BuildRequestBody(request, true, tools), "streaming tool-aware chat request body");
        constexpr int kMaxAttempts = 4;
        std::string last_error;

        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            UniqueInternetHandle session(WinHttpOpen(L"AgentDesktop/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session) {
                last_error = FormatWinHttpError("Failed to open WinHTTP session.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
            if (!connection) {
                last_error = FormatWinHttpError("Failed to connect to host.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
            UniqueInternetHandle request_handle(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            if (!request_handle) {
                result.error = FormatWinHttpError("Failed to create WinHTTP request.", GetLastError());
                return result;
            }
            CancellableRequestWatch cancel_watch(request_handle, should_cancel);

            // Apply per-project timeout: 0 = infinite (default), otherwise send+receive in ms.
            const DWORD timeout_ms = request.model_timeout_seconds > 0
                ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
                : 0;
            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (IsCancelRequested(should_cancel)) {
                result.error = "Cancelled.";
                return result;
            }

            const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
            if (status_code < 200 || status_code >= 300) {
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()), should_cancel);
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            std::string response_buffer;
            size_t streamed_bytes = 0;
            size_t streamed_chunks = 0;
            bool thinking_emitted = false;
            bool thinking_closed = false;
            std::vector<ChatToolCall> streamed_tool_calls;
            auto finalize_streamed_tool_result = [&]() {
                result.success = true;
                result.tool_calls.clear();
                for (auto tool_call : streamed_tool_calls) {
                    if (!tool_call.name.empty()) {
                        NormalizeToolCall(tool_call);
                        result.tool_calls.push_back(std::move(tool_call));
                    }
                }

                json raw_message{
                    {"role", "assistant"},
                };
                if (!result.thinking_text.empty()) {
                    raw_message["thinking"] = result.thinking_text;
                }
                if (!result.assistant_text.empty()) {
                    raw_message["content"] = result.assistant_text;
                }
                if (!result.tool_calls.empty()) {
                    raw_message["tool_calls"] = SerializeToolCallsForProvider(result.tool_calls);
                }
                result.raw_message_json = raw_message.dump(2);
            };

            while (true) {
                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                    if (IsCancelRequested(should_cancel)) {
                        result.error = "Cancelled.";
                        return result;
                    }
                    const DWORD error_code = GetLastError();
                    result.error = FormatWinHttpError("Failed while streaming tool-aware response.", error_code);
                    const std::string report_path = WriteWinHttpTransportReport(
                        request,
                        parsed,
                        "stream_tool_query_data_available",
                        error_code,
                        result.error,
                        attempt,
                        status_code,
                        streamed_bytes,
                        streamed_chunks,
                        response_buffer);
                    result.error = AppendReportPathToError(result.error, report_path);
                    return result;
                }
                if (available == 0) {
                    break;
                }

                if (IsCancelRequested(should_cancel)) {
                    result.error = "Cancelled.";
                    return result;
                }
                std::vector<char> bytes(static_cast<size_t>(available));
                DWORD read = 0;
                if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                    if (IsCancelRequested(should_cancel)) {
                        result.error = "Cancelled.";
                        return result;
                    }
                    const DWORD error_code = GetLastError();
                    result.error = FormatWinHttpError("Failed to read a streaming tool-aware response chunk.", error_code);
                    const std::string report_path = WriteWinHttpTransportReport(
                        request,
                        parsed,
                        "stream_tool_read_data",
                        error_code,
                        result.error,
                        attempt,
                        status_code,
                        streamed_bytes,
                        streamed_chunks,
                        response_buffer);
                    result.error = AppendReportPathToError(result.error, report_path);
                    return result;
                }

                streamed_bytes += read;
                ++streamed_chunks;
                response_buffer.append(bytes.data(), bytes.data() + read);

                size_t line_end = std::string::npos;
                while ((line_end = response_buffer.find('\n')) != std::string::npos) {
                    std::string line = response_buffer.substr(0, line_end);
                    response_buffer.erase(0, line_end + 1);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (line.empty() || line.rfind("data:", 0) != 0) {
                        continue;
                    }

                    std::string payload = Trim(line.substr(5));
                    if (payload == "[DONE]") {
                        finalize_streamed_tool_result();
                        return result;
                    }

                    try {
                        const auto chunk = json::parse(payload);
                        if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) {
                            continue;
                        }

                        const auto& choice = chunk["choices"][0];
                        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                            result.finish_reason = choice["finish_reason"].get<std::string>();
                        }
                        if (!choice.contains("delta") || !choice["delta"].is_object()) {
                            continue;
                        }

                        const auto& delta = choice["delta"];
                        const std::string reasoning_piece = ExtractReasoningString(delta);
                        if (!reasoning_piece.empty()) {
                            result.thinking_text += reasoning_piece;
                            if (!thinking_emitted) {
                                const std::string tag = "\u003cthink\u003e" + reasoning_piece;
                                thinking_emitted = true;
                                result.assistant_text += tag;
                                on_delta(tag);
                            } else {
                                result.assistant_text += reasoning_piece;
                                on_delta(reasoning_piece);
                            }
                        }
                        const std::string content_piece = delta.contains("content")
                            ? ExtractContentString(delta["content"]) : "";
                        if (!content_piece.empty()) {
                            if (thinking_emitted && !thinking_closed) {
                                const std::string close = "\u003c/think\u003e\n\n";
                                thinking_closed = true;
                                result.assistant_text += close + content_piece;
                                on_delta(close + content_piece);
                            } else {
                                result.assistant_text += content_piece;
                                on_delta(content_piece);
                            }
                        }

                        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                            for (const auto& item : delta["tool_calls"]) {
                                if (!item.is_object()) {
                                    continue;
                                }

                                const int index = item.value("index", static_cast<int>(streamed_tool_calls.size()));
                                if (index < 0) {
                                    continue;
                                }
                                if (static_cast<size_t>(index) >= streamed_tool_calls.size()) {
                                    streamed_tool_calls.resize(static_cast<size_t>(index) + 1);
                                }

                                ChatToolCall& tool_call = streamed_tool_calls[static_cast<size_t>(index)];
                                if (item.contains("id") && item["id"].is_string()) {
                                    tool_call.id = item["id"].get<std::string>();
                                }
                                if (item.contains("function") && item["function"].is_object()) {
                                    const auto& function = item["function"];
                                    if (function.contains("name") && function["name"].is_string()) {
                                        tool_call.name += function["name"].get<std::string>();
                                    }
                                    if (function.contains("arguments") && function["arguments"].is_string()) {
                                        tool_call.arguments_json += function["arguments"].get<std::string>();
                                    }
                                }
                            }
                        }
                    } catch (...) {
                        result.error = "Failed to parse a streaming tool-aware SSE chunk.";
                        return result;
                    }
                }
            }

            if (thinking_emitted && !thinking_closed) {
                const std::string close = "</think>\n\n";
                result.assistant_text += close;
            }

            if (result.finish_reason.empty()) {
                const std::string detail =
                    "Streaming tool-aware response ended without the provider completion marker.";
                const std::string report_path = WriteWinHttpTransportReport(
                    request,
                    parsed,
                    "stream_tool_missing_done",
                    ERROR_SUCCESS,
                    detail,
                    attempt,
                    status_code,
                    streamed_bytes,
                    streamed_chunks,
                    response_buffer);
                result.error = AppendReportPathToError(detail, report_path);
                return result;
            }
            finalize_streamed_tool_result();
            return result;
        }

        result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    } catch (...) {
        result.error = "Unexpected error while streaming the tool-aware chat request.";
        return result;
    }
}

std::vector<std::vector<float>> OpenAIClient::CreateEmbedding(const ProviderConfig& provider,
                                                              const ModelConfig& model,
                                                              const std::vector<std::string>& texts,
                                                              const std::function<void(const ProviderQueueStatus&)>& on_queue_status) {
    if (texts.empty()) return {};
    // Binding bypass for embeddings
    if (model.is_binding_model) {
        ChatRequestOptions synthetic; synthetic.provider = provider; synthetic.model = model;
        ChatRequestOptions resolved = synthetic;
        std::vector<ProviderConfig> all_providers;
        { std::lock_guard<std::mutex> lock(GateMapMutex()); for (const auto& kv : s_gate_provider_cache) all_providers.push_back(kv.second); }
        if (!ResolveBindingTarget(all_providers, model, synthetic, &resolved, on_queue_status)) {
            throw std::runtime_error("Embedding binding resolution failed for " + model.id);
        }
        return CreateEmbedding(resolved.provider, resolved.model, texts, on_queue_status);
    }
    ChatRequestOptions gate_request;
    gate_request.provider = provider;
    gate_request.model = model;
    GateSlot slot(gate_request, GateDomain::Embedding);
    if (!slot.Acquire(provider.name, on_queue_status)) {
        throw std::runtime_error("Provider queue is full for embedding: " + provider.name + "/" + model.id + ".");
    }
    const std::string ptype = NormalizeProviderType(provider.provider_type);
    if (ptype == "ollama_local") {
        return RunOllamaLocalEmbedding(provider, model, texts, on_queue_status);
    }
    if (ptype == "ollama_cloud") {
        return RunOllamaCloudEmbedding(provider, model, texts, on_queue_status);
    }
    return RunOpenAICompatibleEmbedding(provider, model, texts, on_queue_status);
}

std::vector<std::vector<float>> RunOllamaLocalEmbedding(const ProviderConfig& provider,
                                                         const ModelConfig& model,
                                                         const std::vector<std::string>& texts,
                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {
    (void)provider; (void)model; (void)texts;
    throw std::runtime_error("RunOllamaLocalEmbedding not yet implemented.");
}

std::vector<std::vector<float>> RunOllamaCloudEmbedding(const ProviderConfig& provider,
                                                         const ModelConfig& model,
                                                         const std::vector<std::string>& texts,
                                                         const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {
    (void)provider; (void)model; (void)texts;
    throw std::runtime_error("RunOllamaCloudEmbedding not yet implemented.");
}

std::vector<std::vector<float>> RunOpenAICompatibleEmbedding(const ProviderConfig& provider,
                                                             const ModelConfig& model,
                                                             const std::vector<std::string>& texts,
                                                             const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/) {
    (void)provider; (void)model; (void)texts;
    throw std::runtime_error("RunOpenAICompatibleEmbedding not yet implemented.");
}

static std::unordered_map<std::string, int64_t> g_cooldown_end_sec; // key = "provider_id|model_id"
static std::mutex g_cooldown_mtx;

void OpenAIClient::ApplyBindingCooldown(const ChatRequestOptions& request, const std::string& error, unsigned long http_status) {
    int cooldown_sec = 300; // default 5 minutes
    if (http_status >= 429) {
        cooldown_sec = 900;   // rate-limit: 15 minutes
    } else if (error.find("busy") != std::string::npos || error.find("overloaded") != std::string::npos) {
        cooldown_sec = 300; // busy: 5 minutes
    }
    const std::string key = request.provider.id + "|" + request.model.id;
    std::lock_guard<std::mutex> lock(g_cooldown_mtx);
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    g_cooldown_end_sec[key] = now + cooldown_sec;
}

bool OpenAIClient::BindingCooldown(const std::string& provider_id, const std::string& model_id) {
    const std::string key = provider_id + "|" + model_id;
    std::lock_guard<std::mutex> lock(g_cooldown_mtx);
    auto it = g_cooldown_end_sec.find(key);
    if (it == g_cooldown_end_sec.end()) return false;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (it->second > now) return true;
    g_cooldown_end_sec.erase(it);
    return false;
}

void OpenAIClient::SetStorage(AppStorage* storage) {
    s_storage = storage;
}


