#include "storage.h"

#include "provider_profiles.h"
#include "rag_service.h"
#include "util.h"

#include <windows.h>
#include <wincrypt.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

namespace {
std::string BindingRoutingModeToString(BindingRoutingMode mode) {
    switch (mode) {
    case BindingRoutingMode::RoundRobin:
        return "round_robin";
    case BindingRoutingMode::TopDownFailover:
    default:
        return "top_down_failover";
    }
}

BindingRoutingMode BindingRoutingModeFromString(std::string value) {
    value = Trim(value);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "round_robin" || value == "round-robin" || value == "roundrobin") {
        return BindingRoutingMode::RoundRobin;
    }
    return BindingRoutingMode::TopDownFailover;
}

std::shared_ptr<std::mutex> JsonFileMutexForPath(const std::filesystem::path& path) {
    static std::mutex mutexes_lock;
    static std::unordered_map<std::string, std::weak_ptr<std::mutex>> mutexes;

    std::error_code ec;
    const auto normalized = std::filesystem::absolute(path, ec).lexically_normal();
    const std::string key = ec ? path.lexically_normal().string() : normalized.string();

    std::lock_guard<std::mutex> lock(mutexes_lock);
    auto& weak = mutexes[key];
    auto existing = weak.lock();
    if (existing) {
        return existing;
    }
    auto created = std::make_shared<std::mutex>();
    weak = created;
    return created;
}

std::filesystem::path UniqueJsonTempPathFor(const std::filesystem::path& path) {
    static std::atomic<unsigned long long> counter{0};
    std::ostringstream suffix;
    suffix << ".tmp."
           << GetCurrentProcessId() << "."
           << GetCurrentThreadId() << "."
           << counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::path(path.string() + suffix.str());
}

std::filesystem::path UniqueJsonBackupPathFor(const std::filesystem::path& path) {
    static std::atomic<unsigned long long> counter{0};
    std::string stamp = CurrentTimestampUtc();
    for (char& ch : stamp) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '-';
        }
    }
    std::ostringstream suffix;
    suffix << ".recover-" << stamp << "."
           << GetCurrentProcessId() << "."
           << GetCurrentThreadId() << "."
           << counter.fetch_add(1, std::memory_order_relaxed)
           << ".bak";
    return std::filesystem::path(path.string() + suffix.str());
}

std::string LastWin32ErrorText(DWORD code) {
    LPSTR buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);
    std::string detail;
    if (length > 0 && buffer) {
        detail.assign(buffer, length);
        while (!detail.empty() &&
               (detail.back() == '\r' || detail.back() == '\n' || detail.back() == '.')) {
            detail.pop_back();
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return "Win32 error " + std::to_string(code) +
        (detail.empty() ? "" : ": " + detail);
}

class ScopedJsonInterprocessLock {
public:
    explicit ScopedJsonInterprocessLock(const std::filesystem::path& target_path)
        : lock_path_(std::filesystem::path(target_path.string() + ".lock")) {
        std::filesystem::create_directories(lock_path_.parent_path());
        for (int attempt = 1; attempt <= 600; ++attempt) {
            handle_ = CreateFileW(
                lock_path_.wstring().c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                acquired_ = true;
                return;
            }

            last_error_ = LastWin32ErrorText(GetLastError());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Logger::Warn(
            "Storage",
            "Could not acquire JSON interprocess lock for " +
                target_path.string() + " (" + last_error_ +
                "); proceeding with in-process lock only.");
    }

    ~ScopedJsonInterprocessLock() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        if (acquired_) {
            std::error_code ec;
            std::filesystem::remove(lock_path_, ec);
        }
    }

    ScopedJsonInterprocessLock(const ScopedJsonInterprocessLock&) = delete;
    ScopedJsonInterprocessLock& operator=(const ScopedJsonInterprocessLock&) = delete;

private:
    std::filesystem::path lock_path_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool acquired_ = false;
    std::string last_error_;
};

bool ReadFileTextAllowReplace(const std::filesystem::path& path, std::string* out) {
    if (!out) {
        return false;
    }
    out->clear();

    HANDLE handle = CreateFileW(
        path.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
        CloseHandle(handle);
        return false;
    }
    if (size.QuadPart > static_cast<LONGLONG>(std::numeric_limits<size_t>::max())) {
        CloseHandle(handle);
        return false;
    }

    out->resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out->size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(out->size() - offset, 1ull << 20));
        DWORD read = 0;
        if (!ReadFile(handle, out->data() + offset, chunk, &read, nullptr)) {
            CloseHandle(handle);
            return false;
        }
        if (read == 0) {
            break;
        }
        offset += read;
    }
    CloseHandle(handle);
    out->resize(offset);
    return true;
}

bool DirectOverwriteFileAllowReplaceBlockers(const std::filesystem::path& path,
                                             const std::string& content,
                                             std::string* error) {
    HANDLE handle = CreateFileW(
        path.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = LastWin32ErrorText(GetLastError());
        }
        return false;
    }

    size_t offset = 0;
    while (offset < content.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(content.size() - offset, 1ull << 20));
        DWORD written = 0;
        if (!WriteFile(handle, content.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            if (error) {
                *error = LastWin32ErrorText(GetLastError());
            }
            CloseHandle(handle);
            return false;
        }
        offset += written;
    }
    FlushFileBuffers(handle);
    CloseHandle(handle);
    return true;
}

void WriteJsonReplaceReport(const std::filesystem::path& path,
                            const std::filesystem::path& tmp_path,
                            const std::string& replace_error,
                            const std::string& fallback_error,
                            const std::filesystem::path& backup_path,
                            bool fallback_succeeded) {
    std::ostringstream message;
    message << "JSON replace " << (fallback_succeeded ? "used fallback" : "failed")
            << " path=" << path.string()
            << " temp=" << tmp_path.string()
            << " replace_error=" << replace_error;
    if (!backup_path.empty()) {
        message << " backup=" << backup_path.string();
    }
    if (!fallback_error.empty()) {
        message << " fallback_error=" << fallback_error;
    }

    if (fallback_succeeded) {
        Logger::Warn("Storage", message.str());
    } else {
        Logger::Error("Storage", message.str());
    }

    std::string stamp = CurrentTimestampUtc();
    for (char& ch : stamp) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '-';
        }
    }
    const auto report_dir = path.parent_path() / "json_write_reports";
    const auto report_path =
        report_dir / (path.filename().string() + "-" + stamp + ".log");
    std::error_code ec;
    std::filesystem::create_directories(report_dir, ec);
    std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
    if (report.is_open()) {
        report << message.str() << "\n";
        report << "target_exists="
               << std::filesystem::exists(path, ec) << "\n";
        ec.clear();
        report << "target_size="
               << (std::filesystem::exists(path, ec)
                    ? std::filesystem::file_size(path, ec)
                    : static_cast<std::uintmax_t>(0))
               << "\n";
        report << "target_attributes=";
        const DWORD attrs = GetFileAttributesW(path.wstring().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            report << LastWin32ErrorText(GetLastError());
        } else {
            report << attrs;
        }
        report << "\n";
    }
}

json BindingTargetToJson(const BindingTargetConfig& target) {
    return json{
        {"provider_id", target.provider_id},
        {"model_id", target.model_id},
        {"enabled", target.enabled},
        {"priority", target.priority},
        {"busy_retry_interval_seconds", target.busy_retry_interval_seconds},
        {"busy_retry_budget_seconds", target.busy_retry_budget_seconds},
        {"busy_cooldown_seconds", target.busy_cooldown_seconds},
        {"limit_cooldown_seconds", target.limit_cooldown_seconds},
        {"error_cooldown_seconds", target.error_cooldown_seconds},
    };
}

BindingTargetConfig BindingTargetFromJson(const json& item) {
    BindingTargetConfig target;
    target.provider_id = item.value("provider_id", "");
    target.model_id = item.value("model_id", "");
    target.enabled = item.value("enabled", true);
    target.priority = item.value("priority", 100);
    target.busy_retry_interval_seconds = std::max(1, item.value("busy_retry_interval_seconds", 15));
    target.busy_retry_budget_seconds = std::max(1, item.value("busy_retry_budget_seconds", 90));
    target.busy_cooldown_seconds = std::max(0, item.value("busy_cooldown_seconds", 300));
    target.limit_cooldown_seconds = std::max(0, item.value("limit_cooldown_seconds", 900));
    target.error_cooldown_seconds = std::max(0, item.value("error_cooldown_seconds", 300));
    return target;
}

json BindingTargetRuntimeStateToJson(const BindingTargetRuntimeState& target) {
    return json{
        {"provider_id", target.provider_id},
        {"model_id", target.model_id},
        {"last_status", target.last_status},
        {"last_used_at", target.last_used_at},
        {"last_success_at", target.last_success_at},
        {"last_busy_at", target.last_busy_at},
        {"last_limit_at", target.last_limit_at},
        {"last_error_at", target.last_error_at},
        {"cooldown_until", target.cooldown_until},
        {"consecutive_busy_count", target.consecutive_busy_count},
        {"consecutive_limit_count", target.consecutive_limit_count},
        {"consecutive_failure_count", target.consecutive_failure_count},
    };
}

BindingTargetRuntimeState BindingTargetRuntimeStateFromJson(const json& item) {
    BindingTargetRuntimeState target;
    target.provider_id = item.value("provider_id", "");
    target.model_id = item.value("model_id", "");
    target.last_status = item.value("last_status", "");
    target.last_used_at = item.value("last_used_at", "");
    target.last_success_at = item.value("last_success_at", "");
    target.last_busy_at = item.value("last_busy_at", "");
    target.last_limit_at = item.value("last_limit_at", "");
    target.last_error_at = item.value("last_error_at", "");
    target.cooldown_until = item.value("cooldown_until", "");
    target.consecutive_busy_count = std::max(0, item.value("consecutive_busy_count", 0));
    target.consecutive_limit_count = std::max(0, item.value("consecutive_limit_count", 0));
    target.consecutive_failure_count = std::max(0, item.value("consecutive_failure_count", 0));
    return target;
}

json BindingModelRuntimeStateToJson(const BindingModelRuntimeState& state) {
    json targets = json::array();
    for (const auto& target : state.targets) {
        targets.push_back(BindingTargetRuntimeStateToJson(target));
    }
    return json{
        {"provider_id", state.provider_id},
        {"model_id", state.model_id},
        {"next_round_robin_index", state.next_round_robin_index},
        {"targets", std::move(targets)},
    };
}

BindingModelRuntimeState BindingModelRuntimeStateFromJson(const json& item) {
    BindingModelRuntimeState state;
    state.provider_id = item.value("provider_id", "");
    state.model_id = item.value("model_id", "");
    state.next_round_robin_index = std::max(0, item.value("next_round_robin_index", 0));
    if (item.contains("targets") && item["targets"].is_array()) {
        for (const auto& target_item : item["targets"]) {
            state.targets.push_back(BindingTargetRuntimeStateFromJson(target_item));
        }
    }
    return state;
}

json ModelToJson(const ModelConfig& model) {
    json reasoning_efforts = json::array();
    for (const auto& effort : model.reasoning_efforts) {
        reasoning_efforts.push_back(effort);
    }
    json text_verbosity_modes = json::array();
    for (const auto& mode : model.text_verbosity_modes) {
        text_verbosity_modes.push_back(mode);
    }
    json binding_targets = json::array();
    for (const auto& target : model.binding_targets) {
        binding_targets.push_back(BindingTargetToJson(target));
    }
    return json{
        {"id", model.id},
        {"display_name", model.display_name},
        {"context_window", model.context_window},
        {"max_output_tokens", model.max_output_tokens},
        {"supports_streaming", model.supports_streaming},
        {"supports_tools", model.supports_tools},
        {"supports_vision", model.supports_vision},
        {"supports_embedding", model.supports_embedding},
        {"supports_thinking", model.supports_thinking},
        {"enable_thinking", model.enable_thinking},
        {"prefer_max_completion_tokens", model.prefer_max_completion_tokens},
        {"output_tokens_parameter", NormalizeOutputTokensParameter(model.output_tokens_parameter)},
        {"catalog_source", model.catalog_source},
        {"reasoning_efforts", std::move(reasoning_efforts)},
        {"text_verbosity_modes", std::move(text_verbosity_modes)},
        {"default_reasoning_effort", model.default_reasoning_effort},
        {"default_text_verbosity", model.default_text_verbosity},
        {"ollama_keep_alive_seconds", model.ollama_keep_alive_seconds},
        {"ollama_num_threads", model.ollama_num_threads},
        {"ollama_no_gpu", model.ollama_no_gpu},
        {"ollama_gpu_layers", model.ollama_gpu_layers},
        {"ollama_context_length", model.ollama_context_length},
        {"ollama_verbose", model.ollama_verbose},
        {"is_binding_model", model.is_binding_model},
        {"max_active_requests", model.max_active_requests},
        {"max_queue_size", model.max_queue_size},
        {"self_managed_queue", model.self_managed_queue},
        {"binding_routing_mode", BindingRoutingModeToString(model.binding_routing_mode)},
        {"binding_targets", std::move(binding_targets)},
    };
}

ModelConfig ModelFromJson(const json& item) {
    ModelConfig model;
    model.id = item.value("id", "");
    model.display_name = item.value("display_name", model.id);
    model.context_window = item.value("context_window", 0);
    model.max_output_tokens = std::max(0, item.value("max_output_tokens", kDefaultModelMaxOutputTokens));
    model.supports_streaming = item.value("supports_streaming", true);
    model.supports_tools = item.value("supports_tools", false);
    model.supports_vision = item.value("supports_vision", false);
    model.supports_embedding = item.value("supports_embedding", false);
    model.supports_thinking = item.value("supports_thinking", false);
    model.enable_thinking = item.value("enable_thinking", false);
    if (!item.contains("enable_thinking") && model.supports_thinking) {
        model.enable_thinking = true;
    }
    model.prefer_max_completion_tokens = item.value("prefer_max_completion_tokens", false);
    model.output_tokens_parameter = NormalizeOutputTokensParameter(
        item.value("output_tokens_parameter", model.prefer_max_completion_tokens ? "max_completion_tokens" : "auto"));
    model.catalog_source = item.value("catalog_source", "manual");
    if (item.contains("reasoning_efforts") && item["reasoning_efforts"].is_array()) {
        for (const auto& effort : item["reasoning_efforts"]) {
            if (effort.is_string()) {
                model.reasoning_efforts.push_back(effort.get<std::string>());
            }
        }
    }
    if (!item.contains("supports_thinking") && !model.reasoning_efforts.empty()) {
        model.supports_thinking = true;
    }
    if (item.contains("text_verbosity_modes") && item["text_verbosity_modes"].is_array()) {
        for (const auto& mode : item["text_verbosity_modes"]) {
            if (mode.is_string()) {
                model.text_verbosity_modes.push_back(mode.get<std::string>());
            }
        }
    }
    model.default_reasoning_effort = item.value("default_reasoning_effort", "");
    model.default_text_verbosity = item.value("default_text_verbosity", "");
    model.ollama_keep_alive_seconds = std::max(0, item.value("ollama_keep_alive_seconds", 0));
    model.ollama_num_threads = std::max(0, item.value("ollama_num_threads", 0));
    model.ollama_no_gpu = item.value("ollama_no_gpu", false);
    model.ollama_gpu_layers = std::max(0, item.value("ollama_gpu_layers", 0));
    model.ollama_context_length = std::max(0, item.value("ollama_context_length", 0));
    model.ollama_verbose = item.value("ollama_verbose", false);
    model.is_binding_model = item.value("is_binding_model", false);
    model.max_active_requests = std::max(0, item.value("max_active_requests", 0));
    model.max_queue_size = std::max(0, item.value("max_queue_size", 0));
    model.self_managed_queue = item.value("self_managed_queue", false);
    model.binding_routing_mode = BindingRoutingModeFromString(item.value("binding_routing_mode", "top_down_failover"));
    if (item.contains("binding_targets") && item["binding_targets"].is_array()) {
        for (const auto& target_item : item["binding_targets"]) {
            model.binding_targets.push_back(BindingTargetFromJson(target_item));
        }
    }
    return model;
}

json ProviderToJson(const ProviderConfig& provider) {
    json models = json::array();
    for (const auto& model : provider.models) {
        models.push_back(ModelToJson(model));
    }

    return json{
        {"id", provider.id},
        {"name", provider.name},
        {"provider_type", provider.provider_type},
        {"base_url", provider.base_url},
        {"api_key", NormalizeProviderType(provider.provider_type) == "openai_codex_oauth" ? "" : provider.api_key},
        {"tls_certificate_fingerprint", provider.tls_certificate_fingerprint},
        {"auth_mode", provider.auth_mode},
        {"oauth_credential_id", provider.oauth_credential_id},
        {"oauth_account_label", provider.oauth_account_label},
        {"oauth_authenticated", provider.oauth_authenticated},
        {"oauth_store_remote_history", provider.oauth_store_remote_history},
        {"model_catalog_mode", provider.model_catalog_mode},
        {"max_active_requests", provider.max_active_requests},
        {"max_queue_size", provider.max_queue_size},
        {"ollama_local_port", provider.ollama_local_port},
        {"models", std::move(models)},
    };
}

ProviderConfig ProviderFromJson(const json& item) {
    ProviderConfig provider;
    provider.id = item.value("id", MakeId("provider"));
    provider.name = item.value("name", "Unnamed Provider");
    provider.provider_type = NormalizeProviderType(item.value("provider_type", "openai_compatible"));
    provider.base_url = item.value("base_url", "");
    provider.api_key = item.value("api_key", "");
    provider.tls_certificate_fingerprint = item.value("tls_certificate_fingerprint", "");
    provider.auth_mode = item.value("auth_mode", "");
    provider.oauth_credential_id = item.value("oauth_credential_id", "");
    provider.oauth_account_label = item.value("oauth_account_label", "");
    provider.oauth_authenticated = item.value("oauth_authenticated", false);
    provider.oauth_store_remote_history = item.value("oauth_store_remote_history", false);
    provider.model_catalog_mode = item.value("model_catalog_mode", "");
    provider.max_active_requests = std::max(0, item.value("max_active_requests", 0));
    provider.max_queue_size = std::max(0, item.value("max_queue_size", 0));
    provider.ollama_local_port = std::max(0, item.value("ollama_local_port", 0));
    if (item.contains("models") && item["models"].is_array()) {
        for (const auto& model_item : item["models"]) {
            provider.models.push_back(ModelFromJson(model_item));
        }
    }
    return provider;
}

json ProviderAuthRecordToJson(const ProviderAuthRecord& record) {
    return json{
        {"credential_id", record.credential_id},
        {"provider_id", record.provider_id},
        {"auth_mode", record.auth_mode},
        {"api_key", record.api_key},
        {"id_token", record.id_token},
        {"access_token", record.access_token},
        {"refresh_token", record.refresh_token},
        {"token_type", record.token_type},
        {"account_id", record.account_id},
        {"account_email", record.account_email},
        {"account_display_name", record.account_display_name},
        {"scope", record.scope},
        {"expires_at", record.expires_at},
        {"last_refresh", record.last_refresh},
    };
}

ProviderAuthRecord ProviderAuthRecordFromJson(const json& item) {
    ProviderAuthRecord record;
    record.credential_id = item.value("credential_id", "");
    record.provider_id = item.value("provider_id", "");
    record.auth_mode = item.value("auth_mode", "");
    record.api_key = item.value("api_key", "");
    record.id_token = item.value("id_token", "");
    record.access_token = item.value("access_token", "");
    record.refresh_token = item.value("refresh_token", "");
    record.token_type = item.value("token_type", "Bearer");
    record.account_id = item.value("account_id", "");
    record.account_email = item.value("account_email", "");
    record.account_display_name = item.value("account_display_name", "");
    record.scope = item.value("scope", "");
    record.expires_at = item.value("expires_at", "");
    record.last_refresh = item.value("last_refresh", "");
    return record;
}

std::string HexEncode(const std::string& value) {
    static const char* digits = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (unsigned char ch : value) {
        encoded.push_back(digits[(ch >> 4) & 0x0F]);
        encoded.push_back(digits[ch & 0x0F]);
    }
    return encoded;
}

std::string HexDecode(const std::string& value) {
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    if (value.size() % 2 != 0) {
        throw std::runtime_error("Invalid encrypted provider auth payload.");
    }
    std::string decoded;
    decoded.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int hi = nibble(value[i]);
        const int lo = nibble(value[i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::runtime_error("Invalid encrypted provider auth payload.");
        }
        decoded.push_back(static_cast<char>((hi << 4) | lo));
    }
    return decoded;
}

std::string ProtectProviderAuthPayload(const std::string& plaintext) {
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"AgentProviderAuth", nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        throw std::runtime_error("Failed to protect provider auth payload.");
    }
    std::string encrypted(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return HexEncode(encrypted);
}

std::string UnprotectProviderAuthPayload(const std::string& ciphertext_hex) {
    const std::string ciphertext = HexDecode(ciphertext_hex);
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(ciphertext.data()));
    input.cbData = static_cast<DWORD>(ciphertext.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &output)) {
        throw std::runtime_error("Failed to read provider auth payload.");
    }
    std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return plaintext;
}

json DefaultOpenAIOAuthManifest() {
    return json{
        {"provider_type", "openai_codex_oauth"},
        {"models", json::array({
            {
                {"id", "gpt-5.4"},
                {"display_name", "GPT 5.4"},
                {"context_window", 1050000},
                {"max_output_tokens", 128000},
                {"supports_streaming", true},
                {"supports_tools", true},
                {"supports_vision", true},
                {"supports_embedding", false},
                {"supports_thinking", true},
                {"prefer_max_completion_tokens", false},
                {"output_tokens_parameter", "auto"},
                {"catalog_source", "bundled"},
                {"reasoning_efforts", json::array({"none", "low", "medium", "high", "xhigh"})},
                {"text_verbosity_modes", json::array({"low", "medium", "high"})}
            },
            {
                {"id", "gpt-5.3-codex"},
                {"display_name", "GPT 5.3 Codex"},
                {"context_window", 400000},
                {"max_output_tokens", 128000},
                {"supports_streaming", true},
                {"supports_tools", true},
                {"supports_vision", true},
                {"supports_embedding", false},
                {"supports_thinking", true},
                {"prefer_max_completion_tokens", false},
                {"output_tokens_parameter", "auto"},
                {"catalog_source", "bundled"},
                {"reasoning_efforts", json::array({"low", "medium", "high", "xhigh"})},
                {"text_verbosity_modes", json::array({"low", "medium", "high"})}
            },
            {
                {"id", "gpt-5.2-codex"},
                {"display_name", "GPT 5.2 Codex"},
                {"context_window", 400000},
                {"max_output_tokens", 128000},
                {"supports_streaming", true},
                {"supports_tools", true},
                {"supports_vision", true},
                {"supports_embedding", false},
                {"supports_thinking", true},
                {"prefer_max_completion_tokens", false},
                {"output_tokens_parameter", "auto"},
                {"catalog_source", "bundled"},
                {"reasoning_efforts", json::array({"low", "medium", "high", "xhigh"})},
                {"text_verbosity_modes", json::array({"low", "medium", "high"})}
            },
            {
                {"id", "gpt-5-codex"},
                {"display_name", "GPT 5 Codex"},
                {"context_window", 400000},
                {"max_output_tokens", 128000},
                {"supports_streaming", true},
                {"supports_tools", true},
                {"supports_vision", true},
                {"supports_embedding", false},
                {"supports_thinking", true},
                {"prefer_max_completion_tokens", false},
                {"output_tokens_parameter", "auto"},
                {"catalog_source", "bundled"},
                {"reasoning_efforts", json::array({"low", "medium", "high", "xhigh"})},
                {"text_verbosity_modes", json::array({"low", "medium", "high"})}
            }
        })}
    };
}

std::string McpVariableKindToString(McpVariableKind kind) {
    switch (kind) {
    case McpVariableKind::Folder:
        return "folder";
    case McpVariableKind::File:
        return "file";
    case McpVariableKind::None:
    default:
        return "none";
    }
}

McpVariableKind McpVariableKindFromString(const std::string& value) {
    if (value == "folder") {
        return McpVariableKind::Folder;
    }
    if (value == "file") {
        return McpVariableKind::File;
    }
    return McpVariableKind::None;
}

std::string McpServerScopeToString(McpServerScope scope) {
    switch (scope) {
    case McpServerScope::Shared:
        return "shared";
    case McpServerScope::PerProject:
    default:
        return "per_project";
    }
}

McpServerScope McpServerScopeFromString(const std::string& value) {
    if (value == "shared") {
        return McpServerScope::Shared;
    }
    return McpServerScope::PerProject;
}

json McpServerVariableToJson(const McpServerVariable& variable) {
    return json{
        {"name", variable.name},
        {"description", variable.description},
        {"kind", McpVariableKindToString(variable.kind)},
        {"inject_into_context", variable.inject_into_context},
    };
}

McpServerVariable McpServerVariableFromJson(const json& item) {
    McpServerVariable variable;
    variable.name = item.value("name", "");
    variable.description = item.value("description", "");
    variable.kind = McpVariableKindFromString(item.value("kind", "none"));
    variable.inject_into_context = item.value("inject_into_context", false);
    return variable;
}

json ProjectMcpVariableValueToJson(const ProjectMcpVariableValue& variable) {
    return json{
        {"name", variable.name},
        {"value", variable.value},
        {"description", variable.description},
        {"inject_into_context", variable.inject_into_context},
        {"allow_user_definition", variable.allow_user_definition},
    };
}

ProjectMcpVariableValue ProjectMcpVariableValueFromJson(const json& item) {
    ProjectMcpVariableValue variable;
    variable.name = item.value("name", "");
    variable.value = item.value("value", "");
    variable.description = item.value("description", "");
    variable.inject_into_context = item.value("inject_into_context", false);
    variable.allow_user_definition = item.value("allow_user_definition", false);
    return variable;
}

json ProjectMcpServerBindingToJson(const ProjectMcpServerBinding& binding) {
    json variables = json::array();
    for (const auto& variable : binding.variables) {
        variables.push_back(ProjectMcpVariableValueToJson(variable));
    }
    return json{
        {"server_id", binding.server_id},
        {"variables", std::move(variables)},
    };
}

ProjectMcpServerBinding ProjectMcpServerBindingFromJson(const json& item) {
    ProjectMcpServerBinding binding;
    binding.server_id = item.value("server_id", "");
    if (item.contains("variables") && item["variables"].is_array()) {
        for (const auto& variable_item : item["variables"]) {
            binding.variables.push_back(ProjectMcpVariableValueFromJson(variable_item));
        }
    }
    return binding;
}

static std::string RagRetrievalModeToString(RagRetrievalMode mode) {
    switch (mode) {
        case RagRetrievalMode::PassiveOnly:    return "passive_only";
        case RagRetrievalMode::ActiveToolOnly: return "active_tool_only";
        case RagRetrievalMode::Disabled:       return "disabled";
        case RagRetrievalMode::Both:           // fallthrough
        default:                               return "both";
    }
}

static RagRetrievalMode RagRetrievalModeFromString(const std::string& s) {
    if (s == "passive_only")    return RagRetrievalMode::PassiveOnly;
    if (s == "active_tool_only") return RagRetrievalMode::ActiveToolOnly;
    if (s == "disabled")        return RagRetrievalMode::Disabled;
    return RagRetrievalMode::Both;
}

json ProjectRagBindingToJson(const ProjectRagBinding& binding) {
    // NOTE: inject_on_start is only used in model tool rag_bindings, but we serialize it
    // everywhere for simplicity.
    return json{
        {"rag_id", binding.rag_id},
        {"enabled", binding.enabled},
        {"can_read", binding.can_read},
        {"can_write", binding.can_write},
        {"expose_as_tool", binding.expose_as_tool},
        {"can_delete", binding.can_delete},
        {"can_export", binding.can_export},
        {"export_path_template", binding.export_path_template},
        {"default_ingest_target", binding.default_ingest_target},
        {"retrieval_priority", binding.retrieval_priority},
        {"max_chunks", binding.max_chunks},
        {"default_min_confidence", binding.default_min_confidence},
        {"default_max_confidence", binding.default_max_confidence},
        {"retrieval_mode", RagRetrievalModeToString(binding.retrieval_mode)},
        {"inject_on_start", binding.inject_on_start},
    };
}

ProjectRagBinding ProjectRagBindingFromJson(const json& item) {
    ProjectRagBinding binding;
    binding.rag_id = item.value("rag_id", "");
    binding.enabled = item.value("enabled", true);
    binding.can_read = item.value("can_read", true);
    binding.can_write = item.value("can_write", false);
    binding.expose_as_tool = item.value("expose_as_tool", false);
    binding.can_delete = item.value("can_delete", false);
    binding.can_export = item.value("can_export", false);
    binding.export_path_template = item.value("export_path_template", "");
    binding.default_ingest_target = item.value("default_ingest_target", false);
    binding.retrieval_priority = item.value("retrieval_priority", 10);
    binding.max_chunks = std::max(1, item.value("max_chunks", 8));
    binding.default_min_confidence = std::clamp(item.value("default_min_confidence", 0.0), 0.0, 1.0);
    binding.default_max_confidence = std::clamp(item.value("default_max_confidence", 1.0), 0.0, 1.0);
    if (binding.default_min_confidence > binding.default_max_confidence) {
        binding.default_min_confidence = 0.0;
        binding.default_max_confidence = 1.0;
    }
    binding.retrieval_mode = RagRetrievalModeFromString(item.value("retrieval_mode", "both"));
    binding.inject_on_start = item.value("inject_on_start", false);
    return binding;
}

json ModelToolConfigToJson(const ModelToolConfig& tool) {
    json mcp_arr = json::array();
    for (const auto& b : tool.mcp_bindings) {
        mcp_arr.push_back(ProjectMcpServerBindingToJson(b));
    }
    json rag_arr = json::array();
    for (const auto& b : tool.rag_bindings) {
        rag_arr.push_back(ProjectRagBindingToJson(b));
    }
    json built_in_arr = json::array();
    for (const auto& name : tool.built_in_tool_names) {
        if (!name.empty()) {
            built_in_arr.push_back(name);
        }
    }
    return json{
        {"id", tool.id},
        {"name", tool.name},
        {"description", tool.description},
        {"preferred_provider_id", tool.preferred_provider_id},
        {"preferred_model_id", tool.preferred_model_id},
        {"instructions", tool.instructions},
        {"selected_compression_config_id", tool.selected_compression_config_id},
        {"selected_agentic_mode_id", tool.selected_agentic_mode_id},
        {"built_in_tool_names", std::move(built_in_arr)},
        {"mcp_bindings", std::move(mcp_arr)},
        {"rag_bindings", std::move(rag_arr)},
    };
}

ModelToolConfig ModelToolConfigFromJson(const json& item) {
    ModelToolConfig tool;
    tool.id = item.value("id", "");
    tool.name = item.value("name", "");
    tool.description = item.value("description", "");
    tool.preferred_provider_id = item.value("preferred_provider_id", "");
    tool.preferred_model_id = item.value("preferred_model_id", "");
    tool.instructions = item.value("instructions", "");
    tool.selected_compression_config_id = item.value("selected_compression_config_id", "");
    tool.selected_agentic_mode_id = item.value("selected_agentic_mode_id", "");
    if (item.contains("built_in_tool_names") && item["built_in_tool_names"].is_array()) {
        for (const auto& name : item["built_in_tool_names"]) {
            if (name.is_string()) {
                const std::string value = Trim(name.get<std::string>());
                if (!value.empty() &&
                    std::find(tool.built_in_tool_names.begin(),
                              tool.built_in_tool_names.end(),
                              value) == tool.built_in_tool_names.end()) {
                    tool.built_in_tool_names.push_back(value);
                }
            }
        }
    }
    if (item.contains("mcp_bindings") && item["mcp_bindings"].is_array()) {
        for (const auto& b : item["mcp_bindings"]) {
            tool.mcp_bindings.push_back(ProjectMcpServerBindingFromJson(b));
        }
    }
    if (item.contains("rag_bindings") && item["rag_bindings"].is_array()) {
        for (const auto& b : item["rag_bindings"]) {
            tool.rag_bindings.push_back(ProjectRagBindingFromJson(b));
        }
    }
    return tool;
}

json McpServerToJson(const McpServerConfig& server) {
    json variables = json::array();
    for (const auto& variable : server.variables) {
        variables.push_back(McpServerVariableToJson(variable));
    }

    return json{
        {"id", server.id},
        {"name", server.name},
        {"command", server.command},
        {"arguments", server.arguments},
        {"working_directory", server.working_directory},
        {"env_entries", server.env_entries},
        {"scope", McpServerScopeToString(server.scope)},
        {"variables", std::move(variables)},
        {"enabled", server.enabled},
        {"auto_connect", server.auto_connect},
    };
}

McpServerConfig McpServerFromJson(const json& item) {
    McpServerConfig server;
    server.id = item.value("id", MakeId("mcp_server"));
    server.name = item.value("name", "Unnamed MCP Server");
    server.command = item.value("command", "");
    server.working_directory = item.value("working_directory", "");
    server.scope = McpServerScopeFromString(item.value("scope", "per_project"));
    server.enabled = item.value("enabled", true);
    server.auto_connect = item.value("auto_connect", false);
    if (item.contains("arguments") && item["arguments"].is_array()) {
        for (const auto& argument : item["arguments"]) {
            if (argument.is_string()) {
                server.arguments.push_back(argument.get<std::string>());
            }
        }
    }
    if (item.contains("env_entries") && item["env_entries"].is_array()) {
        for (const auto& entry : item["env_entries"]) {
            if (entry.is_string()) {
                server.env_entries.push_back(entry.get<std::string>());
            }
        }
    }
    if (item.contains("variables") && item["variables"].is_array()) {
        for (const auto& variable_item : item["variables"]) {
            server.variables.push_back(McpServerVariableFromJson(variable_item));
        }
    }
    return server;
}

json ProjectToJson(const ProjectInfo& project) {
    return json{
        {"id", project.id},
        {"name", project.name},
    };
}

ProjectInfo ProjectFromJson(const json& item, const std::string& fallback_id) {
    ProjectInfo project;
    project.id = item.value("id", fallback_id);
    project.name = item.value("name", fallback_id);
    return project;
}

bool ProjectNameLooksMissing(const ProjectInfo& project) {
    const std::string name = Trim(project.name);
    return name.empty() || name == project.id;
}

std::string ProjectNameFromSettingsJson(const json& item) {
    if (!item.is_object()) {
        return {};
    }
    return Trim(item.value("project_name", ""));
}

json ChatToJson(const ChatInfo& chat) {
    json user_vars = json::array();
    for (const auto& variable : chat.user_variables) {
        user_vars.push_back(ProjectMcpVariableValueToJson(variable));
    }
    return json{
        {"id", chat.id},
        {"name", chat.name},
        {"provider_id", chat.provider_id},
        {"model_id", chat.model_id},
        {"system_prompt", chat.system_prompt},
        {"temperature", chat.temperature},
        {"max_tokens", chat.max_tokens},
        {"selected_agentic_mode_id", chat.selected_agentic_mode_id},
        {"user_variables", std::move(user_vars)},
    };
}

ChatInfo ChatFromJson(const json& item, const std::string& fallback_id) {
    ChatInfo chat;
    chat.id = item.value("id", fallback_id);
    chat.name = item.value("name", fallback_id);
    chat.provider_id = item.value("provider_id", "");
    chat.model_id = item.value("model_id", "");
    chat.system_prompt = item.value("system_prompt", "");
    chat.temperature = item.value("temperature", 0.2);
    chat.max_tokens = item.value("max_tokens", 1024);
    chat.selected_agentic_mode_id = item.value("selected_agentic_mode_id", "");
    if (item.contains("user_variables") && item["user_variables"].is_array()) {
        for (const auto& variable_item : item["user_variables"]) {
            if (!variable_item.is_object()) continue;
            ProjectMcpVariableValue variable = ProjectMcpVariableValueFromJson(variable_item);
            if (!variable.name.empty()) {
                chat.user_variables.push_back(std::move(variable));
            }
        }
    }
    return chat;
}

json MessageToJson(const MessageRecord& message) {
    json payload{
        {"role", message.role},
        {"content", message.content},
        {"created_at", message.created_at},
    };
    if (!message.name.empty()) {
        payload["name"] = message.name;
    }
    if (!message.tool_call_id.empty()) {
        payload["tool_call_id"] = message.tool_call_id;
    }
    if (!message.tool_calls_json.empty()) {
        try {
            payload["tool_calls"] = json::parse(message.tool_calls_json);
        } catch (...) {
            payload["tool_calls_json"] = message.tool_calls_json;
        }
    }
    return payload;
}

MessageRecord MessageFromJson(const json& item) {
    MessageRecord message;
    message.role = item.value("role", "assistant");
    message.content = item.value("content", "");
    message.created_at = item.value("created_at", "");
    message.name = item.value("name", "");
    message.tool_call_id = item.value("tool_call_id", "");
    if (item.contains("tool_calls")) {
        message.tool_calls_json = item["tool_calls"].dump();
    } else {
        message.tool_calls_json = item.value("tool_calls_json", "");
    }
    return message;
}

json ChatContextDebugEntryToJson(const ChatContextDebugEntry& entry) {
    json request_messages = json::array();
    for (const auto& message : entry.request_messages) {
        request_messages.push_back(MessageToJson(message));
    }

    return json{
        {"id", entry.id},
        {"created_at", entry.created_at},
        {"kind", entry.kind},
        {"user_message_index", entry.user_message_index},
        {"provider_id", entry.provider_id},
        {"model_id", entry.model_id},
        {"system_prompt", entry.system_prompt},
        {"request_messages", std::move(request_messages)},
        {"compressed_context", entry.compressed_context},
        {"mcp_context", entry.mcp_context},
        {"rag_context", entry.rag_context},
        {"rag_working_set_json", entry.rag_working_set_json},
    };
}

ChatContextDebugEntry ChatContextDebugEntryFromJson(const json& item) {
    ChatContextDebugEntry entry;
    entry.id = item.value("id", "");
    entry.created_at = item.value("created_at", "");
    entry.kind = item.value("kind", "request");
    entry.user_message_index = item.value("user_message_index", static_cast<size_t>(0));
    entry.provider_id = item.value("provider_id", "");
    entry.model_id = item.value("model_id", "");
    entry.system_prompt = item.value("system_prompt", "");
    entry.compressed_context = item.value("compressed_context", "");
    entry.mcp_context = item.value("mcp_context", "");
    entry.rag_context = item.value("rag_context", "");
    entry.rag_working_set_json = item.value("rag_working_set_json", "");
    if (item.contains("request_messages") && item["request_messages"].is_array()) {
        for (const auto& message_item : item["request_messages"]) {
            entry.request_messages.push_back(MessageFromJson(message_item));
        }
    }
    return entry;
}

json RagWorkingSetEntryToJson(const RagWorkingSetEntry& entry) {
    return json{
        {"chunk_id", entry.chunk_id},
        {"rag_id", entry.rag_id},
        {"rag_name", entry.rag_name},
        {"document_id", entry.document_id},
        {"document_title", entry.document_title},
        {"text", entry.text},
        {"score", entry.score},
        {"query", entry.query},
        {"retrieved_at", entry.retrieved_at},
    };
}

RagWorkingSetEntry RagWorkingSetEntryFromJson(const json& item) {
    RagWorkingSetEntry entry;
    entry.chunk_id = item.value("chunk_id", "");
    entry.rag_id = item.value("rag_id", "");
    entry.rag_name = item.value("rag_name", "");
    entry.document_id = item.value("document_id", "");
    entry.document_title = item.value("document_title", "");
    entry.text = item.value("text", "");
    entry.score = item.value("score", 0.0);
    entry.query = item.value("query", "");
    entry.retrieved_at = item.value("retrieved_at", "");
    return entry;
}

json LoadJsonFileUnlocked(const std::filesystem::path& path, const json& fallback) {
    std::string last_error;
    for (int attempt = 1; attempt <= 8; ++attempt) {
        std::ifstream input(path, std::ios::binary);
        if (input.is_open()) {
            try {
                json data;
                input >> data;
                return data;
            } catch (const std::exception& ex) {
                last_error = ex.what();
            } catch (...) {
                last_error = "unknown parse error";
            }
        } else {
            last_error = "ifstream open failed";
        }

        std::string content;
        if (ReadFileTextAllowReplace(path, &content)) {
            try {
                return json::parse(content);
            } catch (const std::exception& ex) {
                last_error += "; Win32 read parse failed: ";
                last_error += ex.what();
            } catch (...) {
                last_error += "; Win32 read parse failed";
            }
        } else {
            last_error += "; Win32 read failed";
        }

        if (attempt < 8) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::min(500, 25 * attempt)));
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        Logger::Warn(
            "Storage",
            "Using fallback while loading JSON " + path.string() +
                " (" + last_error + ").");
    }
    return fallback;
}

json LoadJsonFile(const std::filesystem::path& path, const json& fallback) {
    const auto path_mutex = JsonFileMutexForPath(path);
    std::lock_guard<std::mutex> path_lock(*path_mutex);
    return LoadJsonFileUnlocked(path, fallback);
}

void SaveJsonFileUnlocked(const std::filesystem::path& path, const json& data) {
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path tmp_path = UniqueJsonTempPathFor(path);
    std::string serialized;
    try {
        serialized = data.dump(2);
    } catch (const nlohmann::json::type_error& ex) {
        Logger::Warn("Storage",
            "Invalid UTF-8 while serializing JSON file " + path.string() +
            "; replacing invalid sequences. error=" + ex.what());
        serialized = data.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    // Write to a temporary file so readers never see a half-written file.
    {
        std::ofstream output(tmp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("Unable to open file for writing: " + tmp_path.string());
        }
        output << serialized;
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code remove_ec;
            std::filesystem::remove(tmp_path, remove_ec);
            throw std::runtime_error("Unable to flush file: " + tmp_path.string());
        }
    }
    // Atomic replace with retries in case another thread/process briefly holds
    // the target. Windows is especially strict about replacing open files, so
    // use a unique temp name and MoveFileExW instead of a shared .tmp path.
    std::string last_error;
    for (int attempt = 1; attempt <= 40; ++attempt) {
        std::error_code exists_ec;
        const bool target_exists = std::filesystem::exists(path, exists_ec);

        if (target_exists) {
            const std::wstring target_w = path.wstring();
            const DWORD attrs = GetFileAttributesW(target_w.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
                SetFileAttributesW(target_w.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }

        const std::wstring tmp_w = tmp_path.wstring();
        const std::wstring target_w = path.wstring();
        if (MoveFileExW(
                tmp_w.c_str(),
                target_w.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return;
        }

        const DWORD error_code = GetLastError();
        last_error = LastWin32ErrorText(error_code);
        if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND) {
            std::error_code rename_ec;
            std::filesystem::rename(tmp_path, path, rename_ec);
            if (!rename_ec) {
                return;
            }
            last_error = rename_ec.message();
        }

        const int delay_ms = std::min(1000, 25 * attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    std::error_code remove_ec;
    std::filesystem::path backup_path;
    std::string fallback_error;
    std::error_code backup_ec;
    if (std::filesystem::exists(path, backup_ec)) {
        backup_path = UniqueJsonBackupPathFor(path);
        if (!CopyFileW(path.wstring().c_str(), backup_path.wstring().c_str(), TRUE)) {
            fallback_error = "backup copy failed: " +
                LastWin32ErrorText(GetLastError());
            backup_path.clear();
        }
    }
    std::string direct_error;
    if (DirectOverwriteFileAllowReplaceBlockers(path, serialized, &direct_error)) {
        std::filesystem::remove(tmp_path, remove_ec);
        WriteJsonReplaceReport(
            path, tmp_path, last_error, fallback_error, backup_path, true);
        return;
    }
    if (!fallback_error.empty()) {
        fallback_error += "; ";
    }
    fallback_error += "direct overwrite failed: " + direct_error;
    WriteJsonReplaceReport(
        path, tmp_path, last_error, fallback_error, backup_path, false);
    std::filesystem::remove(tmp_path, remove_ec);
    throw std::runtime_error(
        "Unable to replace file: " + path.string() +
        (last_error.empty() ? "" : " (" + last_error + ")") +
        (fallback_error.empty() ? "" : " (" + fallback_error + ")"));
}

void SaveJsonFile(const std::filesystem::path& path, const json& data) {
    const auto path_mutex = JsonFileMutexForPath(path);
    std::lock_guard<std::mutex> path_lock(*path_mutex);
    ScopedJsonInterprocessLock file_lock(path);
    SaveJsonFileUnlocked(path, data);
}

json MessagesToJsonArray(const std::vector<MessageRecord>& messages) {
    json payload = json::array();
    for (const auto& message : messages) {
        payload.push_back(MessageToJson(message));
    }
    return payload;
}

std::vector<MessageRecord> MessagesFromJsonArray(const json& data) {
    std::vector<MessageRecord> messages;
    if (!data.is_array()) {
        return messages;
    }
    messages.reserve(data.size());
    for (const auto& item : data) {
        messages.push_back(MessageFromJson(item));
    }
    return messages;
}

bool MessageCountsAsModelVisibleForArchive(const MessageRecord& message) {
    return message.role == "system" ||
           message.role == "user" ||
           message.role == "assistant" ||
           message.role == "tool";
}

std::optional<int> MessageArchiveSequenceFromName(const std::string& filename) {
    const std::string prefix = "messages-old-";
    const std::string suffix = ".json";
    if (filename.size() <= prefix.size() + suffix.size() ||
        filename.rfind(prefix, 0) != 0 ||
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }

    const std::string digits = filename.substr(
        prefix.size(),
        filename.size() - prefix.size() - suffix.size());
    if (digits.empty()) {
        return std::nullopt;
    }
    for (unsigned char ch : digits) {
        if (!std::isdigit(ch)) {
            return std::nullopt;
        }
    }

    try {
        return std::stoi(digits);
    } catch (...) {
        return std::nullopt;
    }
}

std::filesystem::path MessageArchivePath(const std::filesystem::path& chat_dir,
                                         int sequence) {
    std::ostringstream name;
    name << "messages-old-" << std::setw(3) << std::setfill('0')
         << sequence << ".json";
    return chat_dir / name.str();
}

std::vector<std::pair<int, std::filesystem::path>> MessageArchivePaths(
    const std::filesystem::path& chat_dir) {
    std::vector<std::pair<int, std::filesystem::path>> archives;
    std::error_code ec;
    if (!std::filesystem::exists(chat_dir, ec) ||
        !std::filesystem::is_directory(chat_dir, ec)) {
        return archives;
    }

    for (const auto& entry : std::filesystem::directory_iterator(chat_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto sequence =
            MessageArchiveSequenceFromName(entry.path().filename().string());
        if (sequence) {
            archives.emplace_back(*sequence, entry.path());
        }
    }
    std::sort(archives.begin(), archives.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    return archives;
}

int NextMessageArchiveSequence(const std::filesystem::path& chat_dir) {
    int next = 1;
    for (const auto& [sequence, path] : MessageArchivePaths(chat_dir)) {
        (void)path;
        next = std::max(next, sequence + 1);
    }
    return next;
}
}  // namespace

AppStorage::AppStorage(RuntimePaths runtime_paths) : runtime_paths_(std::move(runtime_paths)) {}

void AppStorage::EnsureInitialized() const {
    std::filesystem::create_directories(config_root());
    std::filesystem::create_directories(data_root());
    std::filesystem::create_directories(log_root());
    std::filesystem::create_directories(ConfigProjectsRoot());
    std::filesystem::create_directories(DataProjectsRoot());
    std::filesystem::create_directories(ProviderManifestsRoot());
    if (!std::filesystem::exists(ProvidersPath())) {
        SaveJsonFile(ProvidersPath(), json{{"providers", json::array()}});
    }
    if (!std::filesystem::exists(BindingProviderRuntimePath())) {
        SaveJsonFile(BindingProviderRuntimePath(), json{{"states", json::array()}});
    }
    if (!std::filesystem::exists(McpServersPath())) {
        SaveJsonFile(McpServersPath(), json{{"servers", json::array()}, {"variables", json::array()}});
    }
    if (!std::filesystem::exists(ProviderAuthPath())) {
        SaveJsonFile(ProviderAuthPath(), json{{"records", json::array()}});
    }
    if (!std::filesystem::exists(ProviderManifestPath("openai_codex_oauth"))) {
        SaveJsonFile(ProviderManifestPath("openai_codex_oauth"), DefaultOpenAIOAuthManifest());
    }
}

std::vector<ProviderConfig> AppStorage::LoadProviders() const {
    EnsureInitialized();
    const json data = LoadJsonFile(ProvidersPath(), json{{"providers", json::array()}});

    std::vector<ProviderConfig> providers;
    if (data.contains("providers") && data["providers"].is_array()) {
        for (const auto& provider_item : data["providers"]) {
            ProviderConfig provider = ProviderFromJson(provider_item);
            if (NormalizeProviderType(provider.provider_type) == "openai_codex_oauth") {
                std::optional<ProviderAuthRecord> auth_record;
                if (!provider.oauth_credential_id.empty()) {
                    auth_record = LoadProviderAuthRecord(provider.oauth_credential_id);
                }
                if (!auth_record) {
                    auth_record = LoadProviderAuthRecordForProvider(provider.id);
                }
                if (auth_record) {
                    provider.oauth_credential_id = auth_record->credential_id;
                    provider.oauth_authenticated = !auth_record->api_key.empty() || !auth_record->access_token.empty();
                    provider.oauth_account_label = !auth_record->account_display_name.empty()
                        ? auth_record->account_display_name
                        : (!auth_record->account_email.empty() ? auth_record->account_email : provider.oauth_account_label);
                    provider.api_key = auth_record->api_key;
                } else {
                    provider.oauth_authenticated = false;
                    provider.api_key.clear();
                    provider.oauth_account_label.clear();
                }
            }
            providers.push_back(std::move(provider));
        }
    }
    return providers;
}

void AppStorage::SaveProviders(const std::vector<ProviderConfig>& providers) const {
    EnsureInitialized();
    json payload;
    payload["providers"] = json::array();
    for (const auto& provider : providers) {
        payload["providers"].push_back(ProviderToJson(provider));
    }
    SaveJsonFile(ProvidersPath(), payload);
}

std::vector<BindingModelRuntimeState> AppStorage::LoadBindingProviderRuntimeStates() const {
    EnsureInitialized();
    const json data = LoadJsonFile(BindingProviderRuntimePath(), json{{"states", json::array()}});
    std::vector<BindingModelRuntimeState> states;
    if (data.contains("states") && data["states"].is_array()) {
        for (const auto& item : data["states"]) {
            states.push_back(BindingModelRuntimeStateFromJson(item));
        }
    }
    return states;
}

void AppStorage::SaveBindingProviderRuntimeStates(const std::vector<BindingModelRuntimeState>& states) const {
    EnsureInitialized();
    json payload;
    payload["states"] = json::array();
    for (const auto& state : states) {
        payload["states"].push_back(BindingModelRuntimeStateToJson(state));
    }
    SaveJsonFile(BindingProviderRuntimePath(), payload);
}

std::vector<ModelConfig> AppStorage::LoadProviderManifestModels(const std::string& provider_type) const {
    EnsureInitialized();
    const json data = LoadJsonFile(ProviderManifestPath(provider_type), json::object());
    std::vector<ModelConfig> models;
    if (data.contains("models") && data["models"].is_array()) {
        for (const auto& item : data["models"]) {
            ModelConfig model = ModelFromJson(item);
            if (!model.id.empty()) {
                if (Trim(model.catalog_source).empty()) {
                    model.catalog_source = "bundled";
                }
                models.push_back(std::move(model));
            }
        }
    }
    return models;
}

std::optional<ProviderAuthRecord> AppStorage::LoadProviderAuthRecord(const std::string& credential_id) const {
    EnsureInitialized();
    if (credential_id.empty()) {
        return std::nullopt;
    }
    const json data = LoadJsonFile(ProviderAuthPath(), json{{"records", json::array()}});
    if (!data.contains("records") || !data["records"].is_array()) {
        return std::nullopt;
    }
    for (const auto& item : data["records"]) {
        if (!item.is_object() || item.value("credential_id", "") != credential_id) {
            continue;
        }
        const std::string encrypted = item.value("encrypted_payload", "");
        if (encrypted.empty()) {
            return std::nullopt;
        }
        const json payload = json::parse(UnprotectProviderAuthPayload(encrypted));
        return ProviderAuthRecordFromJson(payload);
    }
    return std::nullopt;
}

std::optional<ProviderAuthRecord> AppStorage::LoadProviderAuthRecordForProvider(const std::string& provider_id) const {
    EnsureInitialized();
    if (provider_id.empty()) {
        return std::nullopt;
    }
    const json data = LoadJsonFile(ProviderAuthPath(), json{{"records", json::array()}});
    if (!data.contains("records") || !data["records"].is_array()) {
        return std::nullopt;
    }
    for (const auto& item : data["records"]) {
        if (!item.is_object() || item.value("provider_id", "") != provider_id) {
            continue;
        }
        const std::string encrypted = item.value("encrypted_payload", "");
        if (encrypted.empty()) {
            return std::nullopt;
        }
        const json payload = json::parse(UnprotectProviderAuthPayload(encrypted));
        return ProviderAuthRecordFromJson(payload);
    }
    return std::nullopt;
}

void AppStorage::SaveProviderAuthRecord(const ProviderAuthRecord& record) const {
    EnsureInitialized();
    if (record.credential_id.empty()) {
        throw std::runtime_error("Provider auth record is missing a credential ID.");
    }
    json data = LoadJsonFile(ProviderAuthPath(), json{{"records", json::array()}});
    if (!data.contains("records") || !data["records"].is_array()) {
        data["records"] = json::array();
    }
    const std::string encrypted_payload = ProtectProviderAuthPayload(ProviderAuthRecordToJson(record).dump());
    bool updated = false;
    for (auto& item : data["records"]) {
        if (!item.is_object() || item.value("credential_id", "") != record.credential_id) {
            continue;
        }
        item = json{
            {"credential_id", record.credential_id},
            {"provider_id", record.provider_id},
            {"encrypted_payload", encrypted_payload},
        };
        updated = true;
        break;
    }
    if (!updated) {
        data["records"].push_back(json{
            {"credential_id", record.credential_id},
            {"provider_id", record.provider_id},
            {"encrypted_payload", encrypted_payload},
        });
    }
    SaveJsonFile(ProviderAuthPath(), data);
}

void AppStorage::DeleteProviderAuthRecord(const std::string& credential_id) const {
    EnsureInitialized();
    if (credential_id.empty()) {
        return;
    }
    json data = LoadJsonFile(ProviderAuthPath(), json{{"records", json::array()}});
    if (!data.contains("records") || !data["records"].is_array()) {
        return;
    }
    auto& records = data["records"];
    records.erase(
        std::remove_if(records.begin(), records.end(), [&](const json& item) {
            return item.is_object() && item.value("credential_id", "") == credential_id;
        }),
        records.end());
    SaveJsonFile(ProviderAuthPath(), data);
}

std::vector<McpServerConfig> AppStorage::LoadMcpServers() const {
    EnsureInitialized();
    const json data = LoadJsonFile(McpServersPath(), json{{"servers", json::array()}});

    std::vector<McpServerConfig> servers;
    if (data.contains("servers") && data["servers"].is_array()) {
        for (const auto& server_item : data["servers"]) {
            servers.push_back(McpServerFromJson(server_item));
        }
    }
    return servers;
}

void AppStorage::SaveMcpServers(const std::vector<McpServerConfig>& servers) const {
    SaveMcpConfiguration(servers, LoadMcpGlobalVariables());
}

std::vector<McpServerVariable> AppStorage::LoadMcpGlobalVariables() const {
    EnsureInitialized();
    const json data = LoadJsonFile(McpServersPath(), json{{"variables", json::array()}});

    std::vector<McpServerVariable> variables;
    if (data.contains("variables") && data["variables"].is_array()) {
        for (const auto& variable_item : data["variables"]) {
            McpServerVariable variable = McpServerVariableFromJson(variable_item);
            if (!variable.name.empty()) {
                variables.push_back(std::move(variable));
            }
        }
    }
    return variables;
}

void AppStorage::SaveMcpGlobalVariables(const std::vector<McpServerVariable>& variables) const {
    SaveMcpConfiguration(LoadMcpServers(), variables);
}

void AppStorage::SaveMcpConfiguration(const std::vector<McpServerConfig>& servers, const std::vector<McpServerVariable>& variables) const {
    EnsureInitialized();
    json payload;
    payload["servers"] = json::array();
    for (const auto& server : servers) {
        payload["servers"].push_back(McpServerToJson(server));
    }
    payload["variables"] = json::array();
    for (const auto& variable : variables) {
        if (!variable.name.empty()) {
            payload["variables"].push_back(McpServerVariableToJson(variable));
        }
    }
    SaveJsonFile(McpServersPath(), payload);
}

std::vector<ProjectRecord> AppStorage::LoadProjects() const {
    EnsureInitialized();

    std::vector<ProjectRecord> projects;
    if (!std::filesystem::exists(ConfigProjectsRoot())) {
        return projects;
    }

    for (const auto& entry : std::filesystem::directory_iterator(ConfigProjectsRoot())) {
        if (!entry.is_directory()) {
            continue;
        }

        const std::string project_id = entry.path().filename().string();
        const json project_json = LoadJsonFile(ProjectMetaPath(project_id), json::object());

        ProjectRecord record;
        record.info = ProjectFromJson(project_json, project_id);
        if (ProjectNameLooksMissing(record.info)) {
            const json settings_json =
                LoadJsonFile(ProjectSettingsPath(project_id), json::object());
            const std::string recovered_name =
                ProjectNameFromSettingsJson(settings_json);
            if (!recovered_name.empty()) {
                record.info.id = project_id;
                record.info.name = recovered_name;
                SaveJsonFile(ProjectMetaPath(project_id), ProjectToJson(record.info));
                Logger::Warn(
                    "Storage",
                    "Repaired missing project metadata for " + project_id +
                        " using project_settings.json name \"" +
                        recovered_name + "\".");
            }
        }

        const auto chats_root = ChatsRoot(project_id);
        if (std::filesystem::exists(chats_root)) {
            for (const auto& chat_entry : std::filesystem::directory_iterator(chats_root)) {
                if (!chat_entry.is_directory()) {
                    continue;
                }

                const std::string chat_id = chat_entry.path().filename().string();
                const json chat_json = LoadJsonFile(ChatMetaPath(project_id, chat_id), json::object());
                record.chats.push_back(ChatFromJson(chat_json, chat_id));
            }
        }

        projects.push_back(std::move(record));
    }

    return projects;
}

ProjectInfo AppStorage::CreateProject(const std::string& name) const {
    EnsureInitialized();

    ProjectInfo project;
    project.id = MakeId("project");
    project.name = name;

    std::filesystem::create_directories(ProjectConfigPath(project.id));
    std::filesystem::create_directories(ChatsRoot(project.id));
    SaveProject(project);
    return project;
}

ChatInfo AppStorage::CreateChat(const std::string& project_id, const std::string& name, const std::string& provider_id, const std::string& model_id) const {
    EnsureInitialized();

    ChatInfo chat;
    chat.id = MakeId("chat");
    chat.name = name;
    chat.provider_id = provider_id;
    chat.model_id = model_id;
    chat.temperature = 0.2;
    chat.max_tokens = 1024;

    std::filesystem::create_directories(ChatPath(project_id, chat.id));
    SaveChat(project_id, chat);
    SaveMessages(project_id, chat.id, {});
    return chat;
}

void AppStorage::SaveProject(const ProjectInfo& project) const {
    EnsureInitialized();
    std::filesystem::create_directories(ProjectConfigPath(project.id));
    std::filesystem::create_directories(ProjectDataPath(project.id));
    std::filesystem::create_directories(ChatsRoot(project.id));
    SaveJsonFile(ProjectMetaPath(project.id), ProjectToJson(project));
}

void AppStorage::SaveChat(const std::string& project_id, const ChatInfo& chat) const {
    EnsureInitialized();
    std::filesystem::create_directories(ChatPath(project_id, chat.id));
    SaveJsonFile(ChatMetaPath(project_id, chat.id), ChatToJson(chat));
}

std::vector<MessageRecord> AppStorage::LoadMessages(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatMessagesPath(project_id, chat_id), json::array());
    return MessagesFromJsonArray(data);
}

std::vector<MessageRecord> AppStorage::LoadMessagesIncludingArchives(const std::string& project_id, const std::string& chat_id) const {
    const auto chat_dir = ChatPath(project_id, chat_id);
    std::vector<MessageRecord> messages;

    for (const auto& [sequence, archive_path] : MessageArchivePaths(chat_dir)) {
        (void)sequence;
        auto archived = MessagesFromJsonArray(LoadJsonFile(archive_path, json::array()));
        messages.insert(
            messages.end(),
            std::make_move_iterator(archived.begin()),
            std::make_move_iterator(archived.end()));
    }

    auto active = LoadMessages(project_id, chat_id);
    messages.insert(
        messages.end(),
        std::make_move_iterator(active.begin()),
        std::make_move_iterator(active.end()));
    return messages;
}

void AppStorage::SaveMessages(const std::string& project_id, const std::string& chat_id, const std::vector<MessageRecord>& messages) const {
    SaveJsonFile(ChatMessagesPath(project_id, chat_id), MessagesToJsonArray(messages));
}

bool AppStorage::RolloverMessagesAfterCompression(
    const std::string& project_id,
    const std::string& chat_id,
    size_t compressed_model_visible_messages,
    const std::optional<MessageRecord>& compression_record,
    std::uintmax_t min_size_bytes) const {
    if (compressed_model_visible_messages == 0) {
        return false;
    }

    const auto active_path = ChatMessagesPath(project_id, chat_id);
    std::error_code ec;
    const auto active_size = std::filesystem::exists(active_path, ec)
        ? std::filesystem::file_size(active_path, ec)
        : static_cast<std::uintmax_t>(0);
    if (ec || active_size < min_size_bytes) {
        return false;
    }

    const auto path_mutex = JsonFileMutexForPath(active_path);
    std::lock_guard<std::mutex> path_lock(*path_mutex);
    ScopedJsonInterprocessLock file_lock(active_path);

    ec.clear();
    const auto locked_size = std::filesystem::exists(active_path, ec)
        ? std::filesystem::file_size(active_path, ec)
        : static_cast<std::uintmax_t>(0);
    if (ec || locked_size < min_size_bytes) {
        return false;
    }

    const json data = LoadJsonFileUnlocked(active_path, json::array());
    auto messages = MessagesFromJsonArray(data);
    if (messages.empty()) {
        return false;
    }

    size_t visible_seen = 0;
    size_t split_index = 0;
    for (; split_index < messages.size(); ++split_index) {
        if (MessageCountsAsModelVisibleForArchive(messages[split_index])) {
            ++visible_seen;
        }
        if (visible_seen >= compressed_model_visible_messages) {
            ++split_index;
            break;
        }
    }

    if (visible_seen < compressed_model_visible_messages || split_index == 0) {
        return false;
    }

    std::vector<MessageRecord> archived(
        std::make_move_iterator(messages.begin()),
        std::make_move_iterator(messages.begin() + static_cast<std::ptrdiff_t>(split_index)));
    std::vector<MessageRecord> active;
    if (compression_record) {
        active.push_back(*compression_record);
    }
    active.insert(
        active.end(),
        std::make_move_iterator(messages.begin() + static_cast<std::ptrdiff_t>(split_index)),
        std::make_move_iterator(messages.end()));

    const auto chat_dir = ChatPath(project_id, chat_id);
    const auto archive_path =
        MessageArchivePath(chat_dir, NextMessageArchiveSequence(chat_dir));
    SaveJsonFile(archive_path, MessagesToJsonArray(archived));
    SaveJsonFileUnlocked(active_path, MessagesToJsonArray(active));
    return true;
}

std::vector<ChatContextDebugEntry> AppStorage::LoadChatContextDebugEntries(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatContextDebugPath(project_id, chat_id), json{{"entries", json::array()}});
    std::vector<ChatContextDebugEntry> entries;
    if (data.contains("entries") && data["entries"].is_array()) {
        for (const auto& item : data["entries"]) {
            entries.push_back(ChatContextDebugEntryFromJson(item));
        }
    }
    return entries;
}

void AppStorage::SaveChatContextDebugEntries(const std::string& project_id, const std::string& chat_id, const std::vector<ChatContextDebugEntry>& entries) const {
    json payload;
    payload["entries"] = json::array();
    for (const auto& entry : entries) {
        payload["entries"].push_back(ChatContextDebugEntryToJson(entry));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatContextDebugPath(project_id, chat_id), payload);
}

void AppStorage::AppendChatContextDebugEntry(const std::string& project_id, const std::string& chat_id, const ChatContextDebugEntry& entry) const {
    auto entries = LoadChatContextDebugEntries(project_id, chat_id);
    entries.push_back(entry);
    SaveChatContextDebugEntries(project_id, chat_id, entries);
}

std::vector<std::string> AppStorage::LoadApprovedMcpServers(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectMcpConsentPath(project_id), json{{"approved_server_ids", json::array()}});
    std::vector<std::string> server_ids;
    if (data.contains("approved_server_ids") && data["approved_server_ids"].is_array()) {
        for (const auto& item : data["approved_server_ids"]) {
            if (item.is_string()) {
                server_ids.push_back(item.get<std::string>());
            }
        }
    }
    return server_ids;
}

void AppStorage::SaveApprovedMcpServers(const std::string& project_id, const std::vector<std::string>& server_ids) const {
    json payload;
    payload["approved_server_ids"] = server_ids;
    std::filesystem::create_directories(ProjectConfigPath(project_id));
    SaveJsonFile(ProjectMcpConsentPath(project_id), payload);
}

std::vector<ProjectMcpServerBinding> AppStorage::LoadProjectMcpBindings(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectMcpBindingsPath(project_id), json{{"bindings", json::array()}});
    std::vector<ProjectMcpServerBinding> bindings;

    if (data.contains("bindings") && data["bindings"].is_array()) {
        for (const auto& item : data["bindings"]) {
            ProjectMcpServerBinding binding = ProjectMcpServerBindingFromJson(item);
            if (!binding.server_id.empty()) {
                bindings.push_back(std::move(binding));
            }
        }
    }

    for (const auto& approved_id : LoadApprovedMcpServers(project_id)) {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == approved_id; });
        if (it == bindings.end()) {
            ProjectMcpServerBinding binding;
            binding.server_id = approved_id;
            bindings.push_back(std::move(binding));
        }
    }

    return bindings;
}

void AppStorage::SaveProjectMcpBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings) const {
    json payload;
    payload["bindings"] = json::array();
    std::vector<std::string> approved_ids;
    for (const auto& binding : bindings) {
        if (binding.server_id.empty()) {
            continue;
        }
        payload["bindings"].push_back(ProjectMcpServerBindingToJson(binding));
        approved_ids.push_back(binding.server_id);
    }
    std::filesystem::create_directories(ProjectConfigPath(project_id));
    SaveJsonFile(ProjectMcpBindingsPath(project_id), payload);
    SaveApprovedMcpServers(project_id, approved_ids);
}

void AppStorage::RenameProject(const std::string& project_id, const std::string& new_name) const {
    const json existing = LoadJsonFile(ProjectMetaPath(project_id), json::object());
    ProjectInfo info = ProjectFromJson(existing, project_id);
    info.name = new_name;
    SaveProject(info);
}

void AppStorage::RenameChat(const std::string& project_id, const std::string& chat_id, const std::string& new_name) const {
    const json existing = LoadJsonFile(ChatMetaPath(project_id, chat_id), json::object());
    ChatInfo info = ChatFromJson(existing, chat_id);
    info.name = new_name;
    SaveChat(project_id, info);
}

void AppStorage::DeleteProject(const std::string& project_id) const {
    std::filesystem::remove_all(ProjectConfigPath(project_id));
    std::filesystem::remove_all(ProjectDataPath(project_id));
}

void AppStorage::DeleteChat(const std::string& project_id, const std::string& chat_id) const {
    std::filesystem::remove_all(ChatPath(project_id, chat_id));
}

std::filesystem::path AppStorage::ProvidersPath() const {
    return config_root() / "providers.json";
}

std::filesystem::path AppStorage::BindingProviderRuntimePath() const {
    return data_root() / "provider_binding_runtime.json";
}

std::filesystem::path AppStorage::ProviderManifestsRoot() const {
    return config_root() / "provider_manifests";
}

std::filesystem::path AppStorage::ProviderManifestPath(const std::string& provider_type) const {
    return ProviderManifestsRoot() / (NormalizeProviderType(provider_type) + "_models.json");
}

std::filesystem::path AppStorage::ProviderAuthPath() const {
    return config_root() / "provider_auth.json";
}

std::filesystem::path AppStorage::ProviderAuthBridgeRoot() const {
    return config_root() / "provider_auth_bridge";
}

std::filesystem::path AppStorage::McpServersPath() const {
    return config_root() / "mcp_servers.json";
}

std::filesystem::path AppStorage::DataRoot() const {
    return data_root();
}

std::filesystem::path AppStorage::ConfigProjectsRoot() const {
    return config_root() / "projects";
}

std::filesystem::path AppStorage::DataProjectsRoot() const {
    return DataRoot() / "projects";
}

std::filesystem::path AppStorage::ProjectConfigPath(const std::string& project_id) const {
    return ConfigProjectsRoot() / project_id;
}

std::filesystem::path AppStorage::ProjectDataPath(const std::string& project_id) const {
    return DataProjectsRoot() / project_id;
}

std::filesystem::path AppStorage::ProjectMetaPath(const std::string& project_id) const {
    return ProjectConfigPath(project_id) / "project.json";
}

std::filesystem::path AppStorage::ProjectMcpConsentPath(const std::string& project_id) const {
    return ProjectConfigPath(project_id) / "mcp_consent.json";
}

std::filesystem::path AppStorage::ProjectMcpBindingsPath(const std::string& project_id) const {
    return ProjectConfigPath(project_id) / "project_mcp.json";
}

std::filesystem::path AppStorage::ChatsRoot(const std::string& project_id) const {
    return ProjectDataPath(project_id) / "chats";
}

std::filesystem::path AppStorage::ChatPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatsRoot(project_id) / chat_id;
}

std::filesystem::path AppStorage::ChatMetaPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "chat.json";
}

std::filesystem::path AppStorage::ChatMessagesPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "messages.json";
}

std::filesystem::path AppStorage::ChatContextDebugPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "context_debug.json";
}

std::filesystem::path AppStorage::CompressionConfigsPath() const {
    return config_root() / "context_compression_configs.json";
}

std::filesystem::path AppStorage::ModelToolsPath() const {
    return config_root() / "model_tools.json";
}

std::filesystem::path AppStorage::AgenticModesPath() const {
    return config_root() / "agentic_modes.json";
}

std::filesystem::path AppStorage::ProjectCompressionPath(const std::string& project_id) const {
    return ProjectConfigPath(project_id) / "context_compression.json";
}

std::filesystem::path AppStorage::ChatCompressionStatePath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "compression_state.json";
}

std::filesystem::path AppStorage::ChatCompressionHistoryPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "compression_history.json";
}

std::filesystem::path AppStorage::ChatRagWorkingSetPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "rag_working_set.json";
}

std::filesystem::path AppStorage::ProjectSettingsPath(const std::string& project_id) const {
    return ProjectConfigPath(project_id) / "project_settings.json";
}

// ===== Compression Config JSON Helpers =====

json Layer1ConfigToJson(const Layer1Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"max_pins", cfg.max_pins},
        {"pin_code_blocks", cfg.pin_code_blocks},
        {"pin_urls", cfg.pin_urls},
        {"pin_numbers", cfg.pin_numbers},
        {"pin_first_message", cfg.pin_first_message},
        {"pin_explicit_instructions", cfg.pin_explicit_instructions},
        {"pin_user_flagged", cfg.pin_user_flagged},
    };
}

Layer1Config Layer1ConfigFromJson(const json& item) {
    Layer1Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.max_pins = item.value("max_pins", 10);
    cfg.pin_code_blocks = item.value("pin_code_blocks", true);
    cfg.pin_urls = item.value("pin_urls", true);
    cfg.pin_numbers = item.value("pin_numbers", true);
    cfg.pin_first_message = item.value("pin_first_message", true);
    cfg.pin_explicit_instructions = item.value("pin_explicit_instructions", true);
    cfg.pin_user_flagged = item.value("pin_user_flagged", true);
    return cfg;
}

json Layer0ConfigToJson(const Layer0Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"capture_model_id", cfg.capture_model_id},
        {"capture_model_provider_id", cfg.capture_model_provider_id},
        {"capture_prompt_template", cfg.capture_prompt_template},
        {"selection_model_id", cfg.selection_model_id},
        {"selection_model_provider_id", cfg.selection_model_provider_id},
        {"selection_prompt_template", cfg.selection_prompt_template},
        {"storage_folder_template", cfg.storage_folder_template},
        {"max_injected_rows", cfg.max_injected_rows},
    };
}

Layer0Config Layer0ConfigFromJson(const json& item) {
    Layer0Config cfg;
    cfg.enabled = item.value("enabled", false);
    cfg.capture_model_id = item.value("capture_model_id", "");
    cfg.capture_model_provider_id = item.value("capture_model_provider_id", "");
    cfg.capture_prompt_template = item.value("capture_prompt_template", "");
    cfg.selection_model_id = item.value("selection_model_id", "");
    cfg.selection_model_provider_id = item.value("selection_model_provider_id", "");
    cfg.selection_prompt_template = item.value("selection_prompt_template", "");
    cfg.storage_folder_template = item.value("storage_folder_template", cfg.storage_folder_template);
    cfg.max_injected_rows = item.value("max_injected_rows", 12);
    return cfg;
}

json Layer2ConfigToJson(const Layer2Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"model_id", cfg.model_id},
        {"model_provider_id", cfg.model_provider_id},
        {"max_tokens", cfg.max_tokens},
        {"trigger_threshold_turns", cfg.trigger_threshold_turns},
        {"prompt_template", cfg.prompt_template},
    };
}

Layer2Config Layer2ConfigFromJson(const json& item) {
    Layer2Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.model_id = item.value("model_id", "");
    cfg.model_provider_id = item.value("model_provider_id", "");
    cfg.max_tokens = item.value("max_tokens", 500);
    cfg.trigger_threshold_turns = item.value("trigger_threshold_turns", 8);
    cfg.prompt_template = item.value("prompt_template", "");
    return cfg;
}

json Layer3ConfigToJson(const Layer3Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"model_id", cfg.model_id},
        {"model_provider_id", cfg.model_provider_id},
        {"max_tokens", cfg.max_tokens},
        {"prompt_template", cfg.prompt_template},
    };
}

Layer3Config Layer3ConfigFromJson(const json& item) {
    Layer3Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.model_id = item.value("model_id", "");
    cfg.model_provider_id = item.value("model_provider_id", "");
    cfg.max_tokens = item.value("max_tokens", 800);
    cfg.prompt_template = item.value("prompt_template", "");
    return cfg;
}

json Layer4ConfigToJson(const Layer4Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"min_recent_turns", cfg.min_recent_turns},
    };
}

Layer4Config Layer4ConfigFromJson(const json& item) {
    Layer4Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.min_recent_turns = item.value("min_recent_turns", 2);
    return cfg;
}

std::string StrategyToString(ContextCompressionStrategy strategy) {
    switch (strategy) {
    case ContextCompressionStrategy::TruncateTop:
        return "truncate_top";
    case ContextCompressionStrategy::RollingSummary:
        return "rolling_summary";
    case ContextCompressionStrategy::ToolTraceDistillation:
        return "tool_trace_distillation";
    case ContextCompressionStrategy::HierarchicalStructured:
        return "hierarchical_structured";
    case ContextCompressionStrategy::None:
    default:
        return "none";
    }
}

ContextCompressionStrategy StrategyFromString(const std::string& value) {
    if (value == "truncate_top") {
        return ContextCompressionStrategy::TruncateTop;
    }
    if (value == "rolling_summary") {
        return ContextCompressionStrategy::RollingSummary;
    }
    if (value == "tool_trace_distillation") {
        return ContextCompressionStrategy::ToolTraceDistillation;
    }
    if (value == "hierarchical_structured") {
        return ContextCompressionStrategy::HierarchicalStructured;
    }
    return ContextCompressionStrategy::None;
}

json ContextCompressionLayerSettingsToJson(const ContextCompressionLayerSettings& layers) {
    return json{
        {"layer0", Layer0ConfigToJson(layers.layer0)},
        {"layer1", Layer1ConfigToJson(layers.layer1)},
        {"layer2", Layer2ConfigToJson(layers.layer2)},
        {"layer3", Layer3ConfigToJson(layers.layer3)},
        {"layer4", Layer4ConfigToJson(layers.layer4)},
    };
}

ContextCompressionLayerSettings ContextCompressionLayerSettingsFromJson(const json& item) {
    ContextCompressionLayerSettings layers;
    if (item.contains("layer0")) {
        layers.layer0 = Layer0ConfigFromJson(item["layer0"]);
    }
    if (item.contains("layer1")) {
        layers.layer1 = Layer1ConfigFromJson(item["layer1"]);
    }
    if (item.contains("layer2")) {
        layers.layer2 = Layer2ConfigFromJson(item["layer2"]);
    }
    if (item.contains("layer3")) {
        layers.layer3 = Layer3ConfigFromJson(item["layer3"]);
    }
    if (item.contains("layer4")) {
        layers.layer4 = Layer4ConfigFromJson(item["layer4"]);
    }
    return layers;
}

json ContextCompressionConfigToJson(const ContextCompressionConfig& config) {
    return json{
        {"id", config.id},
        {"name", config.name},
        {"strategy", StrategyToString(config.strategy)},
        {"pre_pass_config_id", config.pre_pass_config_id},
        {"layers", ContextCompressionLayerSettingsToJson(config.layers)},
        {"frequency_every_n_prompts", config.frequency_every_n_prompts},
        {"context_window_trigger_percent", config.context_window_trigger_percent},
        {"truncate_top_keep_messages", config.truncate_top_keep_messages},
    };
}

ContextCompressionConfig ContextCompressionConfigFromJson(const json& item) {
    ContextCompressionConfig config;
    config.id = item.value("id", "");
    config.name = item.value("name", "Unnamed Config");
    config.strategy = StrategyFromString(item.value("strategy", "none"));
    config.pre_pass_config_id = item.value("pre_pass_config_id", "");
    if (item.contains("layers")) {
        config.layers = ContextCompressionLayerSettingsFromJson(item["layers"]);
    }
    config.frequency_every_n_prompts = item.value("frequency_every_n_prompts", 10);
    config.context_window_trigger_percent = item.value("context_window_trigger_percent", 70);
    config.truncate_top_keep_messages = item.value("truncate_top_keep_messages", 20);
    return config;
}

json ChatCompressionStateToJson(const ChatCompressionState& state) {
    json pinned = json::array();
    for (const auto& msg : state.layer1_pinned_messages) {
        pinned.push_back(MessageToJson(msg));
    }
    return json{
        {"last_compression_message_index", state.last_compression_message_index},
        {"latest_snapshot_id", state.latest_snapshot_id},
        {"current_compressed_context", state.current_compressed_context},
        {"layer0_last_processed_message_index", state.layer0_last_processed_message_index},
        {"layer0_current_index_block", state.layer0_current_index_block},
        {"layer0_last_index_hash", state.layer0_last_index_hash},
        {"layer0_storage_path", state.layer0_storage_path},
        {"layer2_previous_summary", state.layer2_previous_summary},
        {"layer3_previous_state_json", state.layer3_previous_state_json},
        {"layer1_pinned_messages", std::move(pinned)},
    };
}

ChatCompressionState ChatCompressionStateFromJson(const json& item) {
    ChatCompressionState state;
    state.last_compression_message_index = item.value("last_compression_message_index", static_cast<size_t>(0));
    state.latest_snapshot_id = item.value("latest_snapshot_id", "");
    state.current_compressed_context = item.value("current_compressed_context", "");
    state.layer0_last_processed_message_index = item.value("layer0_last_processed_message_index", static_cast<size_t>(0));
    state.layer0_current_index_block = item.value("layer0_current_index_block", "");
    state.layer0_last_index_hash = item.value("layer0_last_index_hash", "");
    state.layer0_storage_path = item.value("layer0_storage_path", "");
    state.layer2_previous_summary = item.value("layer2_previous_summary", "");
    state.layer3_previous_state_json = item.value("layer3_previous_state_json", "");
    if (item.contains("layer1_pinned_messages") && item["layer1_pinned_messages"].is_array()) {
        for (const auto& msg_item : item["layer1_pinned_messages"]) {
            state.layer1_pinned_messages.push_back(MessageFromJson(msg_item));
        }
    }
    return state;
}

json ChatCompressionSnapshotToJson(const ChatCompressionSnapshot& snapshot) {
    json pinned = json::array();
    for (const auto& message : snapshot.pinned_messages) {
        pinned.push_back(MessageToJson(message));
    }

    json source_messages = json::array();
    for (const auto& message : snapshot.source_messages) {
        source_messages.push_back(MessageToJson(message));
    }

    json layer0_selected_artifact_ids = json::array();
    for (const auto& artifact_id : snapshot.layer0_selected_artifact_ids) {
        layer0_selected_artifact_ids.push_back(artifact_id);
    }

    return json{
        {"id", snapshot.id},
        {"created_at", snapshot.created_at},
        {"trigger_reason", snapshot.trigger_reason},
        {"config_id", snapshot.config_id},
        {"config_name", snapshot.config_name},
        {"strategy", snapshot.strategy},
        {"previous_snapshot_id", snapshot.previous_snapshot_id},
        {"previous_message_index", snapshot.previous_message_index},
        {"compressed_through_message_index", snapshot.compressed_through_message_index},
        {"previous_compressed_context", snapshot.previous_compressed_context},
        {"compressed_context", snapshot.compressed_context},
        {"layer0_selected_artifact_ids", std::move(layer0_selected_artifact_ids)},
        {"layer0_index_block", snapshot.layer0_index_block},
        {"layer0_previous_index_hash", snapshot.layer0_previous_index_hash},
        {"layer0_index_hash", snapshot.layer0_index_hash},
        {"layer2_summary", snapshot.layer2_summary},
        {"layer3_state_json", snapshot.layer3_state_json},
        {"pinned_messages", std::move(pinned)},
        {"source_messages", std::move(source_messages)},
    };
}

ChatCompressionSnapshot ChatCompressionSnapshotFromJson(const json& item) {
    ChatCompressionSnapshot snapshot;
    snapshot.id = item.value("id", "");
    snapshot.created_at = item.value("created_at", "");
    snapshot.trigger_reason = item.value("trigger_reason", "");
    snapshot.config_id = item.value("config_id", "");
    snapshot.config_name = item.value("config_name", "");
    snapshot.strategy = item.value("strategy", "");
    snapshot.previous_snapshot_id = item.value("previous_snapshot_id", "");
    snapshot.previous_message_index = item.value("previous_message_index", static_cast<size_t>(0));
    snapshot.compressed_through_message_index = item.value("compressed_through_message_index", static_cast<size_t>(0));
    snapshot.previous_compressed_context = item.value("previous_compressed_context", "");
    snapshot.compressed_context = item.value("compressed_context", "");
    if (item.contains("layer0_selected_artifact_ids") && item["layer0_selected_artifact_ids"].is_array()) {
        for (const auto& artifact_id : item["layer0_selected_artifact_ids"]) {
            if (artifact_id.is_string()) {
                snapshot.layer0_selected_artifact_ids.push_back(artifact_id.get<std::string>());
            }
        }
    }
    snapshot.layer0_index_block = item.value("layer0_index_block", "");
    snapshot.layer0_previous_index_hash = item.value("layer0_previous_index_hash", "");
    snapshot.layer0_index_hash = item.value("layer0_index_hash", "");
    snapshot.layer2_summary = item.value("layer2_summary", "");
    snapshot.layer3_state_json = item.value("layer3_state_json", "");
    if (item.contains("pinned_messages") && item["pinned_messages"].is_array()) {
        for (const auto& message : item["pinned_messages"]) {
            snapshot.pinned_messages.push_back(MessageFromJson(message));
        }
    }
    if (item.contains("source_messages") && item["source_messages"].is_array()) {
        for (const auto& message : item["source_messages"]) {
            snapshot.source_messages.push_back(MessageFromJson(message));
        }
    }
    return snapshot;
}

json ProjectCompressionSettingsToJson(const ProjectCompressionSettings& settings) {
    return json{
        {"enabled", settings.enabled},
        {"config_id", settings.config_id},
    };
}

ProjectCompressionSettings ProjectCompressionSettingsFromJson(const json& item) {
    ProjectCompressionSettings settings;
    settings.enabled = item.value("enabled", false);
    settings.config_id = item.value("config_id", "");
    return settings;
}

// ===== Project Settings JSON Helpers =====

json ProjectSettingsToJson(const ProjectSettings& settings) {
    json j;
    j["project_name"] = settings.project_name;
    j["project_description"] = NormalizeProjectDescription(settings.project_description);
    j["project_instructions"] = settings.project_instructions;

    json mcp_arr = json::array();
    for (const auto& binding : settings.mcp_bindings) {
        mcp_arr.push_back(ProjectMcpServerBindingToJson(binding));
    }
    j["mcp_bindings"] = mcp_arr;

    json compression_arr = json::array();
    for (const auto& cfg : settings.compression_configs) {
        compression_arr.push_back(ContextCompressionConfigToJson(cfg));
    }
    j["compression_configs"] = compression_arr;

    j["selected_compression_config_id"] = settings.selected_compression_config_id;
    j["preferred_provider_id"] = settings.preferred_provider_id;
    j["preferred_model_id"] = settings.preferred_model_id;
    j["user_select_model_enabled"] = settings.user_select_model_enabled;

    json user_model_arr = json::array();
    for (const auto& model : settings.user_selectable_models) {
        if (model.provider_id.empty() || model.model_id.empty()) continue;
        user_model_arr.push_back({
            {"provider_id", model.provider_id},
            {"model_id", model.model_id},
        });
    }
    j["user_selectable_models"] = std::move(user_model_arr);

    json rag_arr = json::array();
    for (const auto& binding : settings.rag_bindings) {
        rag_arr.push_back(ProjectRagBindingToJson(binding));
    }
    j["rag_bindings"] = rag_arr;

    json mt_arr = json::array();
    for (const auto& id : settings.model_tool_ids) {
        mt_arr.push_back(id);
    }
    j["model_tool_ids"] = mt_arr;

    json pv_arr = json::array();
    for (const auto& pv : settings.project_variables) {
        pv_arr.push_back(ProjectMcpVariableValueToJson(pv));
    }
    j["project_variables"] = pv_arr;

    j["selected_agentic_mode_id"] = settings.selected_agentic_mode_id;

    json am_arr = json::array();
    for (const auto& id : settings.enabled_agentic_mode_ids) {
        am_arr.push_back(id);
    }
    j["enabled_agentic_mode_ids"] = am_arr;
    j["enable_chat_logging"] = settings.enable_chat_logging;
    j["allow_manual_context_compression"] = settings.allow_manual_context_compression;
    j["force_context_compression_token_threshold"] =
        settings.force_context_compression_token_threshold;
    j["context_compression_token_threshold"] =
        settings.context_compression_token_threshold;
    j["enable_web_debugging"] = settings.enable_web_debugging;
    j["serve_web_links_inline"] = settings.serve_web_links_inline;
    j["enable_automation"] = settings.enable_automation;
    j["allow_privileged_user_project_folder_browse"] =
        settings.allow_privileged_user_project_folder_browse;
    j["built_in_powershell_enabled"] = settings.built_in_powershell_enabled;
    j["built_in_powershell_working_directory"] = settings.built_in_powershell_working_directory;
    j["built_in_artifact_memory_enabled"] = settings.built_in_artifact_memory_enabled;
    j["built_in_planner_enabled"] = settings.built_in_planner_enabled;
    j["built_in_planner_storage_folder"] = settings.built_in_planner_storage_folder;
    j["built_in_completion_driver_enabled"] = settings.built_in_completion_driver_enabled;
    json cd_modes_arr = json::array();
    for (const auto& id : settings.completion_driver_allowed_mode_ids) {
        cd_modes_arr.push_back(id);
    }
    j["completion_driver_allowed_mode_ids"] = std::move(cd_modes_arr);
    j["completion_driver_max_continuations"] = settings.completion_driver_max_continuations;
    j["completion_driver_overload_delay_seconds"] = settings.completion_driver_overload_delay_seconds;
    j["built_in_questionnaire_enabled"] = settings.built_in_questionnaire_enabled;
    j["questionnaire_max_options"] = settings.questionnaire_max_options;
    j["questionnaire_restrict_by_mode"] = settings.questionnaire_restrict_by_mode;
    j["questionnaire_allowed_mode_id"] = settings.questionnaire_allowed_mode_id;
    j["built_in_filesystem_enabled"] = settings.built_in_filesystem_enabled;
    j["built_in_filesystem_auto_archive"] = settings.built_in_filesystem_auto_archive;
    j["built_in_filesystem_working_directory"] = settings.built_in_filesystem_working_directory;
    j["built_in_sleep_enabled"] = settings.built_in_sleep_enabled;
    j["built_in_sleep_max_seconds"] = settings.built_in_sleep_max_seconds;
    j["built_in_browser_search_enabled"] = settings.built_in_browser_search_enabled;
    j["built_in_window_automation_enabled"] = settings.built_in_window_automation_enabled;
    j["browser_search_primary"] = settings.browser_search_primary;
    j["browser_search_google_enabled"] = settings.browser_search_google_enabled;
    j["browser_search_bing_enabled"] = settings.browser_search_bing_enabled;
    json browser_engine_order = json::array();
    for (const auto& engine : settings.browser_search_engine_order) {
        browser_engine_order.push_back(engine);
    }
    j["browser_search_engine_order"] = std::move(browser_engine_order);
    j["browser_search_default_engine"] = settings.browser_search_default_engine;
    j["browser_search_open_visual_browser"] = settings.browser_search_open_visual_browser;
    j["browser_search_default_content_mode"] = settings.browser_search_default_content_mode;
    j["browser_search_context_description"] = settings.browser_search_context_description;
    j["browser_search_page_load_delay_min_ms"] = settings.browser_search_page_load_delay_min_ms;
    j["browser_search_page_load_delay_max_ms"] = settings.browser_search_page_load_delay_max_ms;
    j["browser_search_keystroke_delay_min_ms"] = settings.browser_search_keystroke_delay_min_ms;
    j["browser_search_keystroke_delay_max_ms"] = settings.browser_search_keystroke_delay_max_ms;
    j["browser_search_click_delay_min_ms"] = settings.browser_search_click_delay_min_ms;
    j["browser_search_click_delay_max_ms"] = settings.browser_search_click_delay_max_ms;
    j["browser_search_pre_submit_delay_min_ms"] = settings.browser_search_pre_submit_delay_min_ms;
    j["browser_search_pre_submit_delay_max_ms"] = settings.browser_search_pre_submit_delay_max_ms;
    j["browser_search_post_results_delay_min_ms"] = settings.browser_search_post_results_delay_min_ms;
    j["browser_search_post_results_delay_max_ms"] = settings.browser_search_post_results_delay_max_ms;
    j["browser_search_timeout_seconds"] = settings.browser_search_timeout_seconds;
    j["model_timeout_seconds"] = settings.model_timeout_seconds;

    return j;
}

ProjectSettings ProjectSettingsFromJson(const json& j) {
    ProjectSettings settings;
    settings.project_name = j.value("project_name", "");
    settings.project_description =
        NormalizeProjectDescription(j.value("project_description", ""));
    settings.project_instructions = j.value("project_instructions", "");
    settings.selected_compression_config_id = j.value("selected_compression_config_id", "");
    settings.preferred_provider_id = j.value("preferred_provider_id", "");
    settings.preferred_model_id = j.value("preferred_model_id", "");
    settings.user_select_model_enabled = j.value("user_select_model_enabled", false);

    if (j.contains("user_selectable_models") && j["user_selectable_models"].is_array()) {
        for (const auto& item : j["user_selectable_models"]) {
            if (!item.is_object()) continue;
            ProjectModelSelection model;
            model.provider_id = item.value("provider_id", "");
            model.model_id = item.value("model_id", "");
            if (!model.provider_id.empty() && !model.model_id.empty()) {
                settings.user_selectable_models.push_back(std::move(model));
            }
        }
    }

    if (j.contains("mcp_bindings") && j["mcp_bindings"].is_array()) {
        for (const auto& item : j["mcp_bindings"]) {
            settings.mcp_bindings.push_back(ProjectMcpServerBindingFromJson(item));
        }
    }

    if (j.contains("compression_configs") && j["compression_configs"].is_array()) {
        for (const auto& item : j["compression_configs"]) {
            settings.compression_configs.push_back(ContextCompressionConfigFromJson(item));
        }
    }

    if (j.contains("rag_bindings") && j["rag_bindings"].is_array()) {
        for (const auto& item : j["rag_bindings"]) {
            settings.rag_bindings.push_back(ProjectRagBindingFromJson(item));
        }
    }

    if (j.contains("model_tool_ids") && j["model_tool_ids"].is_array()) {
        for (const auto& item : j["model_tool_ids"]) {
            if (item.is_string()) {
                settings.model_tool_ids.push_back(item.get<std::string>());
            }
        }
    }

    if (j.contains("project_variables") && j["project_variables"].is_array()) {
        for (const auto& item : j["project_variables"]) {
            if (item.is_object()) {
                ProjectMcpVariableValue pv = ProjectMcpVariableValueFromJson(item);
                if (!pv.name.empty()) settings.project_variables.push_back(std::move(pv));
            }
        }
    }

    settings.selected_agentic_mode_id = j.value("selected_agentic_mode_id", "");

    if (j.contains("enabled_agentic_mode_ids") && j["enabled_agentic_mode_ids"].is_array()) {
        for (const auto& item : j["enabled_agentic_mode_ids"]) {
            if (item.is_string()) {
                settings.enabled_agentic_mode_ids.push_back(item.get<std::string>());
            }
        }
    }

    settings.enable_chat_logging = j.value("enable_chat_logging", false);
    settings.allow_manual_context_compression = j.value("allow_manual_context_compression", false);
    settings.force_context_compression_token_threshold =
        j.value("force_context_compression_token_threshold", false);
    settings.context_compression_token_threshold =
        j.value("context_compression_token_threshold", 0);
    if (settings.context_compression_token_threshold < 0) {
        settings.context_compression_token_threshold = 0;
    }
    settings.enable_web_debugging = j.value("enable_web_debugging", false);
    settings.serve_web_links_inline = j.value("serve_web_links_inline", false);
    settings.enable_automation = j.value("enable_automation", false);
    settings.allow_privileged_user_project_folder_browse =
        j.value("allow_privileged_user_project_folder_browse", false);
    settings.built_in_powershell_enabled = j.value("built_in_powershell_enabled", false);
    settings.built_in_powershell_working_directory = j.value(
        "built_in_powershell_working_directory", "$ProjectFolder$");
    if (Trim(settings.built_in_powershell_working_directory).empty()) {
        settings.built_in_powershell_working_directory = "$ProjectFolder$";
    }
    settings.built_in_artifact_memory_enabled = j.value("built_in_artifact_memory_enabled", false);
    settings.built_in_planner_enabled = j.value("built_in_planner_enabled", false);
    settings.built_in_planner_storage_folder = j.value(
        "built_in_planner_storage_folder", "$ProjectFolder$\\.agent");
    if (Trim(settings.built_in_planner_storage_folder).empty()) {
        settings.built_in_planner_storage_folder = "$ProjectFolder$\\.agent";
    }
    settings.built_in_completion_driver_enabled = j.value("built_in_completion_driver_enabled", false);
    if (j.contains("completion_driver_allowed_mode_ids") && j["completion_driver_allowed_mode_ids"].is_array()) {
        for (const auto& item : j["completion_driver_allowed_mode_ids"]) {
            if (item.is_string()) {
                settings.completion_driver_allowed_mode_ids.push_back(item.get<std::string>());
            }
        }
    }
    settings.completion_driver_max_continuations = j.value("completion_driver_max_continuations", 0);
    if (settings.completion_driver_max_continuations < 0) {
        settings.completion_driver_max_continuations = 0;
    }
    settings.completion_driver_overload_delay_seconds = j.value(
        "completion_driver_overload_delay_seconds",
        kDefaultCompletionDriverOverloadDelaySeconds);
    if (settings.completion_driver_overload_delay_seconds < 0) {
        settings.completion_driver_overload_delay_seconds = 0;
    }
    settings.built_in_questionnaire_enabled = j.value("built_in_questionnaire_enabled", false);
    settings.questionnaire_max_options = j.value("questionnaire_max_options", 8);
    if (settings.questionnaire_max_options < 2) settings.questionnaire_max_options = 2;
    if (settings.questionnaire_max_options > 50) settings.questionnaire_max_options = 50;
    settings.questionnaire_restrict_by_mode = j.value("questionnaire_restrict_by_mode", false);
    settings.questionnaire_allowed_mode_id = j.value("questionnaire_allowed_mode_id", "");
    settings.built_in_filesystem_enabled = j.value("built_in_filesystem_enabled", false);
    settings.built_in_filesystem_auto_archive = j.value("built_in_filesystem_auto_archive", false);
    settings.built_in_filesystem_working_directory = j.value("built_in_filesystem_working_directory", "$ProjectFolder$");
    if (Trim(settings.built_in_filesystem_working_directory).empty()) {
        settings.built_in_filesystem_working_directory = "$ProjectFolder$";
    }
    settings.built_in_sleep_enabled = j.value("built_in_sleep_enabled", false);
    settings.built_in_sleep_max_seconds = j.value("built_in_sleep_max_seconds", 0);
    if (settings.built_in_sleep_max_seconds < 0) {
        settings.built_in_sleep_max_seconds = 0;
    }
    settings.built_in_browser_search_enabled = j.value("built_in_browser_search_enabled", false);
    settings.built_in_window_automation_enabled = j.value("built_in_window_automation_enabled", false);
    settings.browser_search_primary = j.value("browser_search_primary", false);
    settings.browser_search_google_enabled = j.value("browser_search_google_enabled", true);
    settings.browser_search_bing_enabled = j.value("browser_search_bing_enabled", true);
    if (!settings.browser_search_google_enabled && !settings.browser_search_bing_enabled) {
        settings.browser_search_google_enabled = true;
    }
    settings.browser_search_engine_order.clear();
    if (j.contains("browser_search_engine_order") && j["browser_search_engine_order"].is_array()) {
        for (const auto& item : j["browser_search_engine_order"]) {
            if (!item.is_string()) continue;
            std::string engine = Trim(item.get<std::string>());
            std::transform(engine.begin(), engine.end(), engine.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if ((engine == "google" || engine == "bing") &&
                std::find(settings.browser_search_engine_order.begin(),
                          settings.browser_search_engine_order.end(),
                          engine) == settings.browser_search_engine_order.end()) {
                settings.browser_search_engine_order.push_back(engine);
            }
        }
    }
    auto ensure_browser_engine = [&](const std::string& engine) {
        if (std::find(settings.browser_search_engine_order.begin(),
                      settings.browser_search_engine_order.end(),
                      engine) == settings.browser_search_engine_order.end()) {
            settings.browser_search_engine_order.push_back(engine);
        }
    };
    ensure_browser_engine("google");
    ensure_browser_engine("bing");
    settings.browser_search_default_engine = Trim(j.value("browser_search_default_engine", "google"));
    std::transform(settings.browser_search_default_engine.begin(), settings.browser_search_default_engine.end(),
                   settings.browser_search_default_engine.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (settings.browser_search_default_engine != "google" &&
        settings.browser_search_default_engine != "bing") {
        settings.browser_search_default_engine = settings.browser_search_engine_order.empty()
            ? std::string("google")
            : settings.browser_search_engine_order.front();
    }
    if (settings.browser_search_default_engine == "google" && !settings.browser_search_google_enabled) {
        settings.browser_search_default_engine = settings.browser_search_bing_enabled ? "bing" : "google";
    }
    if (settings.browser_search_default_engine == "bing" && !settings.browser_search_bing_enabled) {
        settings.browser_search_default_engine = settings.browser_search_google_enabled ? "google" : "bing";
    }
    settings.browser_search_open_visual_browser = j.value("browser_search_open_visual_browser", false);
    settings.browser_search_default_content_mode =
        Trim(j.value("browser_search_default_content_mode", "text"));
    std::transform(settings.browser_search_default_content_mode.begin(),
                   settings.browser_search_default_content_mode.end(),
                   settings.browser_search_default_content_mode.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (settings.browser_search_default_content_mode != "text" &&
        settings.browser_search_default_content_mode != "html" &&
        settings.browser_search_default_content_mode != "text_html" &&
        settings.browser_search_default_content_mode != "pdf" &&
        settings.browser_search_default_content_mode != "all") {
        settings.browser_search_default_content_mode = "text";
    }
    settings.browser_search_context_description =
        j.value("browser_search_context_description", std::string(kDefaultBrowserSearchDescription));
    if (Trim(settings.browser_search_context_description).empty()) {
        settings.browser_search_context_description = kDefaultBrowserSearchDescription;
    }
    settings.browser_search_page_load_delay_min_ms =
        std::max(0, j.value("browser_search_page_load_delay_min_ms", kDefaultBrowserSearchPageLoadDelayMinMs));
    settings.browser_search_page_load_delay_max_ms =
        std::max(settings.browser_search_page_load_delay_min_ms,
                 j.value("browser_search_page_load_delay_max_ms", kDefaultBrowserSearchPageLoadDelayMaxMs));
    settings.browser_search_keystroke_delay_min_ms =
        std::max(0, j.value("browser_search_keystroke_delay_min_ms", kDefaultBrowserSearchKeystrokeDelayMinMs));
    settings.browser_search_keystroke_delay_max_ms =
        std::max(settings.browser_search_keystroke_delay_min_ms,
                 j.value("browser_search_keystroke_delay_max_ms", kDefaultBrowserSearchKeystrokeDelayMaxMs));
    settings.browser_search_click_delay_min_ms =
        std::max(0, j.value("browser_search_click_delay_min_ms", kDefaultBrowserSearchClickDelayMinMs));
    settings.browser_search_click_delay_max_ms =
        std::max(settings.browser_search_click_delay_min_ms,
                 j.value("browser_search_click_delay_max_ms", kDefaultBrowserSearchClickDelayMaxMs));
    settings.browser_search_pre_submit_delay_min_ms =
        std::max(0, j.value("browser_search_pre_submit_delay_min_ms", kDefaultBrowserSearchPreSubmitDelayMinMs));
    settings.browser_search_pre_submit_delay_max_ms =
        std::max(settings.browser_search_pre_submit_delay_min_ms,
                 j.value("browser_search_pre_submit_delay_max_ms", kDefaultBrowserSearchPreSubmitDelayMaxMs));
    settings.browser_search_post_results_delay_min_ms =
        std::max(0, j.value("browser_search_post_results_delay_min_ms", kDefaultBrowserSearchPostResultsDelayMinMs));
    settings.browser_search_post_results_delay_max_ms =
        std::max(settings.browser_search_post_results_delay_min_ms,
                 j.value("browser_search_post_results_delay_max_ms", kDefaultBrowserSearchPostResultsDelayMaxMs));
    settings.browser_search_timeout_seconds =
        std::clamp(j.value("browser_search_timeout_seconds", kDefaultBrowserSearchTimeoutSeconds), 1, 600);
    settings.model_timeout_seconds = j.value("model_timeout_seconds", 0);
    if (settings.model_timeout_seconds < 0) {
        settings.model_timeout_seconds = 0;
    }

    return settings;
}

ProjectSettings AppStorage::LoadProjectSettings(const std::string& project_id) const {
    auto settings_path = ProjectSettingsPath(project_id);
    if (std::filesystem::exists(settings_path)) {
        const json data = LoadJsonFile(settings_path, json::object());
        ProjectSettings settings = ProjectSettingsFromJson(data);
        if (settings.selected_compression_config_id.empty()) {
            auto comp_path = ProjectCompressionPath(project_id);
            if (std::filesystem::exists(comp_path)) {
                const json comp_data = LoadJsonFile(comp_path, json::object());
                ProjectCompressionSettings legacy_compression = ProjectCompressionSettingsFromJson(comp_data);
                if (legacy_compression.enabled && !legacy_compression.config_id.empty()) {
                    settings.selected_compression_config_id = legacy_compression.config_id;
                }
            }
        }
        return settings;
    }

    // Migration: load from legacy files
    ProjectSettings settings;
    settings.mcp_bindings = LoadProjectMcpBindings(project_id);

    // Load compression config id from context_compression.json
    auto comp_path = ProjectCompressionPath(project_id);
    if (std::filesystem::exists(comp_path)) {
        const json comp_data = LoadJsonFile(comp_path, json::object());
        settings.selected_compression_config_id = comp_data.value("config_id", "");
    }

    // Load rag bindings from project_rag.json
    auto rag_path = ProjectConfigPath(project_id) / "project_rag.json";
    if (std::filesystem::exists(rag_path)) {
        const json rag_data = LoadJsonFile(rag_path, json::object());
        if (rag_data.contains("bindings") && rag_data["bindings"].is_array()) {
            for (const auto& item : rag_data["bindings"]) {
                settings.rag_bindings.push_back(ProjectRagBindingFromJson(item));
            }
        }
    }

    // Save unified file after migration
    std::filesystem::create_directories(ProjectConfigPath(project_id));
    SaveJsonFile(settings_path, ProjectSettingsToJson(settings));

    return settings;
}

void AppStorage::SaveProjectSettings(const std::string& project_id, const ProjectSettings& settings) const {
    std::filesystem::create_directories(ProjectConfigPath(project_id));
    SaveJsonFile(ProjectSettingsPath(project_id), ProjectSettingsToJson(settings));
}

// ===== Compression Storage Methods =====

std::vector<ContextCompressionConfig> AppStorage::LoadCompressionConfigs() const {
    const json data = LoadJsonFile(CompressionConfigsPath(), json{{"configs", json::array()}});
    std::vector<ContextCompressionConfig> configs;
    std::unordered_set<std::string> used_ids;
    bool repaired = false;
    if (data.contains("configs") && data["configs"].is_array()) {
        for (const auto& item : data["configs"]) {
            ContextCompressionConfig config = ContextCompressionConfigFromJson(item);
            if (config.id.empty() || used_ids.find(config.id) != used_ids.end()) {
                do {
                    config.id = MakeId("cc");
                } while (used_ids.find(config.id) != used_ids.end());
                repaired = true;
            }
            used_ids.insert(config.id);
            configs.push_back(std::move(config));
        }
    }
    if (repaired) {
        SaveCompressionConfigs(configs);
    }
    return configs;
}

void AppStorage::SaveCompressionConfigs(const std::vector<ContextCompressionConfig>& configs) const {
    json payload;
    payload["configs"] = json::array();
    for (const auto& config : configs) {
        payload["configs"].push_back(ContextCompressionConfigToJson(config));
    }
    SaveJsonFile(CompressionConfigsPath(), payload);
}

ProjectCompressionSettings AppStorage::LoadProjectCompressionSettings(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectCompressionPath(project_id), json::object());
    return ProjectCompressionSettingsFromJson(data);
}

void AppStorage::SaveProjectCompressionSettings(const std::string& project_id, const ProjectCompressionSettings& settings) const {
    std::filesystem::create_directories(ProjectConfigPath(project_id));
    SaveJsonFile(ProjectCompressionPath(project_id), ProjectCompressionSettingsToJson(settings));
}

ChatCompressionState AppStorage::LoadChatCompressionState(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatCompressionStatePath(project_id, chat_id), json::object());
    return ChatCompressionStateFromJson(data);
}

void AppStorage::SaveChatCompressionState(const std::string& project_id, const std::string& chat_id, const ChatCompressionState& state) const {
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatCompressionStatePath(project_id, chat_id), ChatCompressionStateToJson(state));
}

std::vector<ChatCompressionSnapshot> AppStorage::LoadChatCompressionHistory(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatCompressionHistoryPath(project_id, chat_id), json{{"snapshots", json::array()}});
    std::vector<ChatCompressionSnapshot> snapshots;
    if (data.contains("snapshots") && data["snapshots"].is_array()) {
        for (const auto& item : data["snapshots"]) {
            snapshots.push_back(ChatCompressionSnapshotFromJson(item));
        }
    }
    return snapshots;
}

void AppStorage::SaveChatCompressionHistory(const std::string& project_id, const std::string& chat_id, const std::vector<ChatCompressionSnapshot>& snapshots) const {
    json data;
    data["snapshots"] = json::array();
    for (const auto& snapshot : snapshots) {
        data["snapshots"].push_back(ChatCompressionSnapshotToJson(snapshot));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatCompressionHistoryPath(project_id, chat_id), data);
}

void AppStorage::AppendChatCompressionSnapshot(const std::string& project_id, const std::string& chat_id, const ChatCompressionSnapshot& snapshot) const {
    auto snapshots = LoadChatCompressionHistory(project_id, chat_id);
    snapshots.push_back(snapshot);
    SaveChatCompressionHistory(project_id, chat_id, snapshots);
}

std::vector<RagWorkingSetEntry> AppStorage::LoadChatRagWorkingSet(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatRagWorkingSetPath(project_id, chat_id), json{{"entries", json::array()}});
    std::vector<RagWorkingSetEntry> entries;
    if (data.contains("entries") && data["entries"].is_array()) {
        for (const auto& item : data["entries"]) {
            entries.push_back(RagWorkingSetEntryFromJson(item));
        }
    }
    return entries;
}

void AppStorage::SaveChatRagWorkingSet(const std::string& project_id, const std::string& chat_id, const std::vector<RagWorkingSetEntry>& entries) const {
    json payload;
    payload["entries"] = json::array();
    for (const auto& entry : entries) {
        payload["entries"].push_back(RagWorkingSetEntryToJson(entry));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatRagWorkingSetPath(project_id, chat_id), payload);
}

std::vector<ModelToolConfig> AppStorage::LoadModelTools() const {
    const json data = LoadJsonFile(ModelToolsPath(), json{{"tools", json::array()}});
    std::vector<ModelToolConfig> tools;
    std::unordered_set<std::string> used_ids;
    bool repaired = false;
    if (data.contains("tools") && data["tools"].is_array()) {
        for (const auto& item : data["tools"]) {
            ModelToolConfig tool = ModelToolConfigFromJson(item);
            if (tool.id.empty() || used_ids.find(tool.id) != used_ids.end()) {
                do {
                    tool.id = MakeId("mt");
                } while (used_ids.find(tool.id) != used_ids.end());
                repaired = true;
            }
            used_ids.insert(tool.id);
            tools.push_back(std::move(tool));
        }
    }
    if (repaired) {
        SaveModelTools(tools);
    }
    return tools;
}

void AppStorage::SaveModelTools(const std::vector<ModelToolConfig>& tools) const {
    json payload;
    payload["tools"] = json::array();
    for (const auto& tool : tools) {
        payload["tools"].push_back(ModelToolConfigToJson(tool));
    }
    SaveJsonFile(ModelToolsPath(), payload);
}

// ΓöÇΓöÇ Agentic Mode config serialization (matching free-function pattern above) ΓöÇΓöÇ

static json AgenticModeConfigToJson(const AgenticModeConfig& mode) {
    json j;
    j["id"] = mode.id;
    j["name"] = mode.name;
    j["instructions"] = mode.instructions;
    return j;
}

static AgenticModeConfig AgenticModeConfigFromJson(const json& j) {
    AgenticModeConfig mode;
    mode.id = j.value("id", "");
    mode.name = j.value("name", "");
    mode.instructions = j.value("instructions", "");
    return mode;
}

std::vector<AgenticModeConfig> AppStorage::LoadAgenticModes() const {
    const json data = LoadJsonFile(AgenticModesPath(), json{{"modes", json::array()}});
    std::vector<AgenticModeConfig> modes;
    if (data.contains("modes") && data["modes"].is_array()) {
        for (const auto& item : data["modes"]) {
            modes.push_back(AgenticModeConfigFromJson(item));
        }
    }
    return modes;
}

void AppStorage::SaveAgenticModes(const std::vector<AgenticModeConfig>& modes) const {
    json payload;
    payload["modes"] = json::array();
    for (const auto& mode : modes) {
        payload["modes"].push_back(AgenticModeConfigToJson(mode));
    }
    SaveJsonFile(AgenticModesPath(), payload);
}
