#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define CPPHTTPLIB_THREAD_POOL_SIZE 8
#include <httplib.h>

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#endif

#include "remote_provider_worker.h"
#include "provider_profiles.h"
#include "util.h"

#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Reuse helpers already declared in remote_ollama_worker.cpp via util.h
// We duplicate the tiny helpers here to keep the unit self-contained.
static std::atomic_bool g_provider_worker_stop = false;
static httplib::Server* g_provider_worker_server = nullptr;

namespace {

int ClampPort(int value, int fallback) {
    return std::clamp(value <= 0 ? fallback : value, 1, 65535);
}

std::wstring ExpandEnvironmentPath(const std::wstring& value) {
    DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) return value;
    std::wstring expanded(needed, L'\0');
    DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
    if (written == 0 || written > needed) return value;
    expanded.resize(written > 0 ? written - 1 : 0);
    return expanded;
}

std::string SafePathSegment(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            result.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            result.push_back('_');
        }
    }
    while (!result.empty() && (result.front() == '.' || result.front() == '_')) result.erase(result.begin());
    while (!result.empty() && (result.back() == '.' || result.back() == '_')) result.pop_back();
    return result.empty() ? "worker" : result;
}

std::string JsonString(const json& data, const char* key, const std::string& fallback = {}) {
    if (!data.contains(key) || data.at(key).is_null()) return fallback;
    if (data.at(key).is_string()) return data.at(key).get<std::string>();
    return fallback;
}

int JsonInt(const json& data, const char* key, int fallback) {
    if (!data.contains(key) || data.at(key).is_null()) return fallback;
    if (data.at(key).is_number_integer()) return data.at(key).get<int>();
    if (data.at(key).is_string()) {
        try { return std::stoi(data.at(key).get<std::string>()); } catch (...) {}
    }
    return fallback;
}

std::optional<fs::path> FindOllamaExe() {
    wchar_t buffer[32768] = {};
    DWORD found = SearchPathW(nullptr, L"ollama.exe", nullptr, static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
    if (found > 0 && found < std::size(buffer)) return fs::path(buffer);
    const std::vector<std::wstring> candidates = {
        ExpandEnvironmentPath(L"%LOCALAPPDATA%\\Programs\\Ollama\\ollama.exe"),
        ExpandEnvironmentPath(L"%ProgramFiles%\\Ollama\\ollama.exe"),
        ExpandEnvironmentPath(L"%ProgramFiles(x86)%\\Ollama\\ollama.exe"),
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec)) return fs::path(c);
    }
    return std::nullopt;
}

void EnsureConsoleAttached() {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) AllocConsole();
}

void WriteLine(const std::wstring& text) {
    EnsureConsoleAttached();
    const std::wstring line = text + L"\r\n";
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr)) return;
        const std::string u8 = WideToUtf8(line);
        WriteFile(out, u8.data(), static_cast<DWORD>(u8.size()), &written, nullptr);
        return;
    }
    OutputDebugStringW(line.c_str());
}

std::wstring QuoteArg(const std::wstring& value) {
    std::wstring q = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') q += L"\\\"";
        else q.push_back(ch);
    }
    q += L"\"";
    return q;
}

std::optional<std::pair<fs::path, fs::path>> ResolveRuntimeCertificateFiles(
    const RemoteProviderWorkerConfig& config,
    std::string* error) {
    if (!config.certificate_pem.empty() && !config.private_key_pem.empty()) {
        wchar_t temp_path[MAX_PATH] = {};
        DWORD temp_len = GetTempPathW(static_cast<DWORD>(std::size(temp_path)), temp_path);
        fs::path temp_root;
        if (temp_len > 0 && temp_len < std::size(temp_path)) temp_root = fs::path(temp_path);
        else temp_root = fs::temp_directory_path();

        // sanitize worker name for path
        std::wstring safe_name;
        for (wchar_t ch : Utf8ToWide(config.worker_name)) {
            if (std::iswalnum(ch) || ch == L'-' || ch == L'_' || ch == L'.') safe_name.push_back(ch);
            else if (std::iswspace(ch)) safe_name.push_back(L'_');
        }
        if (safe_name.empty()) safe_name = L"worker";

        fs::path runtime_dir = temp_root / L"agent_remote_workers" / safe_name;
        fs::path cert_path = runtime_dir / L"server.crt";
        fs::path key_path = runtime_dir / L"server.key";
        std::error_code ec;
        fs::create_directories(runtime_dir, ec);
        {
            std::ofstream c(cert_path, std::ios::binary | std::ios::trunc);
            if (!c.is_open()) { if (error) *error = "Cannot write runtime cert."; return std::nullopt; }
            c << config.certificate_pem;
        }
        {
            std::ofstream k(key_path, std::ios::binary | std::ios::trunc);
            if (!k.is_open()) { if (error) *error = "Cannot write runtime key."; return std::nullopt; }
            k << config.private_key_pem;
        }
        return std::make_pair(cert_path, key_path);
    }
    return std::nullopt;
}

bool Authorized(const httplib::Request& req, const std::string& shared_secret) {
    if (shared_secret.empty()) return false;
    const std::string auth = req.get_header_value("Authorization");
    if (auth == "Bearer " + shared_secret) return true;
    return req.get_header_value("X-Agent-Shared-Secret") == shared_secret;
}

void SetJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(2), "application/json");
}

// Binding runtime state (not persisted; round-robin per process lifetime)
struct WorkerBindingState {
    int next_round_robin_index = 0;
    std::unordered_map<std::string, int> cooldown_end_sec; // key = "provider_id|model_id"
};

static std::unordered_map<std::string, WorkerBindingState> g_worker_binding_states;
static std::mutex g_worker_binding_mutex;

// Forward declaration needed before ResolveBindingUpstream
const RemoteProviderWorkerExportedProvider* FindProviderById(
    const RemoteProviderWorkerConfig& config,
    const std::string& provider_id);

// Find upstream provider/model for a binding model request.
// Mutates binding state for round-robin and cooldown tracking.
const RemoteProviderWorkerExportedProvider* ResolveBindingUpstream(
    const RemoteProviderWorkerConfig& config,
    const ModelConfig& binding_model,
    std::string* out_model_id,
    std::string* out_error) {
    if (binding_model.binding_targets.empty()) {
        if (out_error) *out_error = "Binding model has no targets.";
        return nullptr;
    }
    const auto now = static_cast<int>(std::time(nullptr));
    const bool round_robin = binding_model.binding_routing_mode == BindingRoutingMode::RoundRobin;
    WorkerBindingState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_worker_binding_mutex);
        state = &g_worker_binding_states[binding_model.id];
    }

    auto is_cooldown = [&](const BindingTargetConfig& t) -> bool {
        if (!t.enabled) return true;
        auto it = state->cooldown_end_sec.find(t.provider_id + "|" + t.model_id);
        if (it == state->cooldown_end_sec.end()) return false;
        return it->second > now;
    };

    std::vector<const BindingTargetConfig*> candidates;
    for (const auto& t : binding_model.binding_targets) {
        if (!t.enabled) continue;
        if (is_cooldown(t)) continue;
        candidates.push_back(&t);
    }
    if (candidates.empty()) {
        // All targets in cooldown; pick first enabled
        for (const auto& t : binding_model.binding_targets) {
            if (t.enabled) candidates.push_back(&t);
        }
    }
    if (candidates.empty()) {
        if (out_error) *out_error = "No enabled binding targets.";
        return nullptr;
    }

    const BindingTargetConfig* selected = nullptr;
    if (round_robin) {
        int start_idx = state->next_round_robin_index;
        for (size_t i = 0; i < candidates.size(); ++i) {
            int idx = (start_idx + static_cast<int>(i)) % static_cast<int>(candidates.size());
            const auto& t = *candidates[idx];
            auto it = state->cooldown_end_sec.find(t.provider_id + "|" + t.model_id);
            if (it == state->cooldown_end_sec.end() || it->second <= now) {
                selected = &t;
                state->next_round_robin_index = (idx + 1) % static_cast<int>(candidates.size());
                break;
            }
        }
        if (!selected) selected = candidates.front();
    } else {
        // Top-down failover
        for (const auto* t : candidates) {
            const std::string key = t->provider_id + "|" + t->model_id;
            auto it = state->cooldown_end_sec.find(key);
            if (it == state->cooldown_end_sec.end() || it->second <= now) {
                selected = t;
                break;
            }
        }
        if (!selected) selected = candidates.front();
    }

    if (selected) {
        const auto* exp = FindProviderById(config, selected->provider_id);
        if (out_model_id) *out_model_id = selected->model_id;
        return exp;
    }
    if (out_error) *out_error = "Failed to resolve binding target.";
    return nullptr;
}

// Helper to route based on model id in JSON body
const RemoteProviderWorkerExportedProvider* FindProviderForModel(
    const RemoteProviderWorkerConfig& config,
    const std::string& model_id) {
    for (const auto& exp : config.exported_providers) {
        for (const auto& m : exp.provider.models) {
            if (m.id == model_id) return &exp;
        }
        // Also match on provider id itself if no model matched
        if (exp.provider.id == model_id) return &exp;
    }
    return nullptr;
}

const RemoteProviderWorkerExportedProvider* FindProviderById(
    const RemoteProviderWorkerConfig& config,
    const std::string& provider_id) {
    for (const auto& exp : config.exported_providers) {
        if (exp.provider.id == provider_id) return &exp;
    }
    return nullptr;
}

// Returns effective auth header value for upstream request (api_key or access_token)
std::string UpstreamAuthHeader(const RemoteProviderWorkerExportedProvider& exp) {
    if (exp.auth_record.has_value()) {
        const auto& ar = *exp.auth_record;
        if (!ar.access_token.empty()) return "Bearer " + ar.access_token;
        if (!ar.api_key.empty()) return "Bearer " + ar.api_key;
    }
    if (!exp.provider.api_key.empty()) return "Bearer " + exp.provider.api_key;
    return {};
}

std::string UpstreamBaseUrl(const RemoteProviderWorkerExportedProvider& exp) {
    const std::string ptype = NormalizeProviderType(exp.provider.provider_type);
    if (ptype == "ollama_local") {
        int port = exp.provider.ollama_local_port;
        if (port <= 0) port = 12434;
        return "http://127.0.0.1:" + std::to_string(port);
    }
    return exp.provider.base_url;
}

std::string UpstreamChatPath(const RemoteProviderWorkerExportedProvider& exp) {
    const std::string ptype = NormalizeProviderType(exp.provider.provider_type);
    if (ptype == "ollama_local") return "/api/chat";
    return "/v1/chat/completions";
}

// SSE helpers for OpenAI-compatible streaming
std::string OpenAiSseDeltaChunk(const std::string& model,
                                 const std::string& id,
                                 json delta,
                                 const std::string& finish_reason = {}) {
    json choice = {{"index", 0}, {"delta", std::move(delta)}};
    if (!finish_reason.empty()) choice["finish_reason"] = finish_reason;
    json event = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", static_cast<int64_t>(std::time(nullptr))},
        {"model", model},
        {"choices", json::array({choice})}
    };
    return "data: " + event.dump() + "\n\n";
}

std::string OpenAiSseChunk(const std::string& model,
                           const std::string& id,
                           const std::string& content,
                           const std::string& finish_reason = {}) {
    json delta = json::object();
    if (!content.empty()) delta["content"] = content;
    return OpenAiSseDeltaChunk(model, id, std::move(delta), finish_reason);
}

// Start Ollama locally if needed
bool EnsureOllamaLocalRunning(const ProviderConfig& provider, std::string* error) {
    if (NormalizeProviderType(provider.provider_type) != "ollama_local") return true;
    int port = provider.ollama_local_port;
    if (port <= 0) port = 12434;
    // Quick health check
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    auto res = client.Get("/api/tags");
    if (res && res->status >= 200 && res->status < 500) return true;

    auto exe = FindOllamaExe();
    if (!exe) { if (error) *error = "ollama.exe not found."; return false; }
    const std::wstring host = L"127.0.0.1:" + std::to_wstring(port);
    std::wstring cmd = QuoteArg(exe->wstring()) + L" serve";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');

    std::vector<wchar_t> envbuf;
    { LPWCH raw = GetEnvironmentStringsW(); if (raw) {
        const wchar_t* cur = raw;
        while (*cur) { std::wstring e = cur; envbuf.insert(envbuf.end(), e.begin(), e.end()); envbuf.push_back(L'\0'); cur += e.size() + 1; }
        FreeEnvironmentStringsW(raw);
    }}
    auto addEnv = [&](const std::wstring& name, const std::wstring& value) {
        envbuf.insert(envbuf.end(), name.begin(), name.end()); envbuf.push_back(L'=');
        envbuf.insert(envbuf.end(), value.begin(), value.end()); envbuf.push_back(L'\0');
    };
    addEnv(L"OLLAMA_HOST", host);
    envbuf.push_back(L'\0');

    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        envbuf.empty() ? nullptr : envbuf.data(), nullptr, &si, &pi)) {
        if (error) *error = "Could not start ollama serve: " + std::to_string(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // Wait up to 30s
    for (int i = 0; i < 60; ++i) {
        Sleep(500);
        httplib::Client hc("127.0.0.1", port);
        hc.set_connection_timeout(1, 0);
        auto r = hc.Get("/api/tags");
        if (r && r->status >= 200 && r->status < 500) return true;
    }
    if (error) *error = "Ollama did not become ready on port " + std::to_string(port);
    return false;
}

// Build a simple OpenAI-compatible /v1/models response from exported providers
json BuildModelsList(const RemoteProviderWorkerConfig& config) {
    json models = json::array();
    for (const auto& exp : config.exported_providers) {
        for (const auto& m : exp.provider.models) {
            models.push_back({
                {"id", m.id},
                {"object", "model"},
                {"owned_by", exp.provider.name},
                {"provider_id", exp.provider.id},
            });
        }
    }
    return {{"object", "list"}, {"data", models}};
}

} // namespace

// ==================== Public API ====================

std::string GenerateRemoteProviderWorkerSharedSecret() {
    unsigned char buffer[32] = {};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer, sizeof(buffer), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return MakeId("secret");
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : buffer) out << std::setw(2) << static_cast<int>(byte);
    return out.str();
}

bool GenerateRemoteProviderWorkerSelfSignedCertificateMaterial(
    RemoteProviderWorkerCertificateMaterial* material,
    std::string* error) {
    if (!material) { if (error) *error = "Output was null."; return false; }
    *material = {};
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (error) *error = "OpenSSL support is not compiled into this build.";
    return false;
#else
    EVP_PKEY* raw_pkey = nullptr;
    EVP_PKEY_CTX* raw_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(raw_ctx, EVP_PKEY_CTX_free);
    if (!ctx) { if (error) *error = "EVP_PKEY_CTX_new_id failed."; return false; }
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) <= 0 ||
        EVP_PKEY_keygen(ctx.get(), &raw_pkey) <= 0) {
        if (error) *error = "EVP_PKEY_keygen failed.";
        return false;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(raw_pkey, EVP_PKEY_free);
    std::unique_ptr<X509, decltype(&X509_free)> x509(X509_new(), X509_free);
    if (!x509) { if (error) *error = "X509_new failed."; return false; }
    X509_set_version(x509.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509.get()),
        static_cast<long>(std::chrono::system_clock::now().time_since_epoch().count() & 0x7fffffff));
    X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(x509.get()), 60LL * 60 * 24 * 365 * 3);
    X509_set_pubkey(x509.get(), pkey.get());
    X509_NAME* name = X509_get_subject_name(x509.get());
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Agent Remote Worker"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("agent-remote-worker"), -1, -1, 0);
    X509_set_issuer_name(x509.get(), name);
    if (X509_sign(x509.get(), pkey.get(), EVP_sha256()) == 0) { if (error) *error = "X509_sign failed."; return false; }

    std::unique_ptr<BIO, decltype(&BIO_free)> cert_bio(BIO_new(BIO_s_mem()), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!cert_bio || !key_bio) { if (error) *error = "BIO alloc failed."; return false; }
    if (PEM_write_bio_X509(cert_bio.get(), x509.get()) != 1) { if (error) *error = "PEM_write_bio_X509 failed."; return false; }
    if (PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        if (error) *error = "PEM_write_bio_PrivateKey failed."; return false;
    }
    BUF_MEM* cert_mem = nullptr; BIO_get_mem_ptr(cert_bio.get(), &cert_mem);
    BUF_MEM* key_mem = nullptr; BIO_get_mem_ptr(key_bio.get(), &key_mem);
    if (!cert_mem || !cert_mem->data || !key_mem || !key_mem->data) { if (error) *error = "BIO export empty."; return false; }
    material->certificate_pem = std::string(cert_mem->data, cert_mem->length);
    material->private_key_pem = std::string(key_mem->data, key_mem->length);

    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    unsigned int digest_len = 0;
    if (X509_digest(x509.get(), EVP_sha256(), digest, &digest_len) != 1) { if (error) *error = "X509_digest failed."; return false; }
    std::ostringstream fp;
    fp << "SHA256:";
    fp << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        if (i > 0) fp << ":";
        fp << std::setw(2) << static_cast<int>(digest[i]);
    }
    material->fingerprint = fp.str();
    return true;
#endif
}

std::optional<RemoteProviderWorkerConfig> LoadRemoteProviderWorkerConfig(
    const std::filesystem::path& path, std::string* error) {
    try {
        if (path.empty() || !fs::exists(path)) {
            if (error) *error = "Remote worker JSON file was not found.";
            return std::nullopt;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) { if (error) *error = "Could not open remote worker JSON."; return std::nullopt; }
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto data = json::parse(text);
        RemoteProviderWorkerConfig config;
        config.source_path = path;
        config.version = JsonInt(data, "version", 2);
        config.worker_name = Trim(JsonString(data, "worker_name", config.worker_name));
        if (config.worker_name.empty()) config.worker_name = "Agent Remote Worker";

        if (data.contains("agent_server") && data["agent_server"].is_object()) {
            const auto& s = data["agent_server"];
            config.bind_address = Trim(JsonString(s, "bind_address", config.bind_address));
            config.https_port = ClampPort(JsonInt(s, "https_port", config.https_port), 8765);
            config.shared_secret = Trim(JsonString(s, "shared_secret", ""));
            config.certificate_pem = JsonString(s, "certificate_pem", "");
            config.private_key_pem = JsonString(s, "private_key_pem", "");
            config.certificate_fingerprint = Trim(JsonString(s, "certificate_fingerprint", ""));
        } else {
            // Legacy flat layout migration
            config.bind_address = Trim(JsonString(data, "bind_address", config.bind_address));
            config.https_port = ClampPort(JsonInt(data, "https_port", config.https_port), 8765);
            config.shared_secret = Trim(JsonString(data, "shared_secret", ""));
            if (data.contains("agent_server") && data["agent_server"].is_object()) {
                const auto& s = data["agent_server"];
                config.certificate_pem = JsonString(s, "certificate_pem", "");
                config.private_key_pem = JsonString(s, "private_key_pem", "");
                config.certificate_fingerprint = Trim(JsonString(s, "certificate_fingerprint", ""));
            } else {
                config.certificate_pem = JsonString(data, "certificate_pem", "");
                config.private_key_pem = JsonString(data, "private_key_pem", "");
                config.certificate_fingerprint = Trim(JsonString(data, "certificate_fingerprint", ""));
            }
        }

        if (data.contains("providers") && data["providers"].is_array()) {
            for (const auto& p : data["providers"]) {
                RemoteProviderWorkerExportedProvider exp;
                if (p.contains("provider") && p["provider"].is_object()) {
                    // Full nested format
                    const auto& prov_json = p["provider"];
                    exp.provider.id = prov_json.value("id", MakeId("provider"));
                    exp.provider.name = prov_json.value("name", "Unnamed");
                    exp.provider.provider_type = NormalizeProviderType(prov_json.value("provider_type", "openai_compatible"));
                    exp.provider.base_url = prov_json.value("base_url", "");
                    exp.provider.api_key = prov_json.value("api_key", "");
                    exp.provider.tls_certificate_fingerprint = prov_json.value("tls_certificate_fingerprint", "");
                    exp.provider.auth_mode = prov_json.value("auth_mode", "");
                    exp.provider.model_catalog_mode = prov_json.value("model_catalog_mode", "manual");
                    if (prov_json.contains("models") && prov_json["models"].is_array()) {
                        for (const auto& m : prov_json["models"]) {
                            ModelConfig mc;
                            mc.id = m.value("id", "");
                            mc.display_name = m.value("display_name", mc.id);
                            mc.context_window = std::max(0, m.value("context_window", 0));
                            mc.max_output_tokens = std::max(0, m.value("max_output_tokens", 0));
                            mc.supports_streaming = m.value("supports_streaming", true);
                            mc.supports_tools = m.value("supports_tools", false);
                            mc.supports_vision = m.value("supports_vision", false);
                            mc.is_binding_model = m.value("is_binding_model", false);
                            std::string routing_str = m.value("binding_routing_mode", "top_down_failover");
                            mc.binding_routing_mode = (routing_str == "round_robin" || routing_str == "round-robin")
                                ? BindingRoutingMode::RoundRobin : BindingRoutingMode::TopDownFailover;
                            if (m.contains("binding_targets") && m["binding_targets"].is_array()) {
                                for (const auto& t : m["binding_targets"]) {
                                    BindingTargetConfig btc;
                                    btc.provider_id = t.value("provider_id", "");
                                    btc.model_id = t.value("model_id", "");
                                    btc.enabled = t.value("enabled", true);
                                    btc.priority = std::max(0, t.value("priority", 100));
                                    btc.busy_retry_interval_seconds = std::max(0, t.value("busy_retry_interval_seconds", 15));
                                    btc.busy_retry_budget_seconds = std::max(0, t.value("busy_retry_budget_seconds", 90));
                                    btc.busy_cooldown_seconds = std::max(0, t.value("busy_cooldown_seconds", 300));
                                    btc.limit_cooldown_seconds = std::max(0, t.value("limit_cooldown_seconds", 900));
                                    btc.error_cooldown_seconds = std::max(0, t.value("error_cooldown_seconds", 300));
                                    mc.binding_targets.push_back(std::move(btc));
                                }
                            }
                            exp.provider.models.push_back(std::move(mc));
                        }
                    }
                    if (p.contains("auth_record") && p["auth_record"].is_object()) {
                        const auto& a = p["auth_record"];
                        ProviderAuthRecord ar;
                        ar.credential_id = a.value("credential_id", "");
                        ar.provider_id = a.value("provider_id", "");
                        ar.auth_mode = a.value("auth_mode", "");
                        ar.api_key = a.value("api_key", "");
                        ar.access_token = a.value("access_token", "");
                        ar.refresh_token = a.value("refresh_token", "");
                        ar.token_type = a.value("token_type", "Bearer");
                        ar.account_id = a.value("account_id", "");
                        ar.account_email = a.value("account_email", "");
                        ar.account_display_name = a.value("account_display_name", "");
                        ar.scope = a.value("scope", "");
                        ar.expires_at = a.value("expires_at", "");
                        exp.auth_record = std::move(ar);
                    }
                } else {
                    // Direct provider JSON (simplified)
                    exp.provider.id = p.value("id", MakeId("provider"));
                    exp.provider.name = p.value("name", "Unnamed");
                    exp.provider.provider_type = NormalizeProviderType(p.value("provider_type", "openai_compatible"));
                    exp.provider.base_url = p.value("base_url", "");
                    exp.provider.api_key = p.value("api_key", "");
                    exp.provider.tls_certificate_fingerprint = p.value("tls_certificate_fingerprint", "");
                    exp.provider.auth_mode = p.value("auth_mode", "");
                    exp.provider.model_catalog_mode = p.value("model_catalog_mode", "manual");
                    if (p.contains("models") && p["models"].is_array()) {
                        for (const auto& m : p["models"]) {
                            ModelConfig mc;
                            mc.id = m.value("id", "");
                            mc.display_name = m.value("display_name", mc.id);
                            mc.context_window = std::max(0, m.value("context_window", 0));
                            mc.max_output_tokens = std::max(0, m.value("max_output_tokens", 0));
                            mc.supports_streaming = m.value("supports_streaming", true);
                            mc.supports_tools = m.value("supports_tools", false);
                            mc.supports_vision = m.value("supports_vision", false);
                            mc.is_binding_model = m.value("is_binding_model", false);
                            std::string routing_str = m.value("binding_routing_mode", "top_down_failover");
                            mc.binding_routing_mode = (routing_str == "round_robin" || routing_str == "round-robin")
                                ? BindingRoutingMode::RoundRobin : BindingRoutingMode::TopDownFailover;
                            if (m.contains("binding_targets") && m["binding_targets"].is_array()) {
                                for (const auto& t : m["binding_targets"]) {
                                    BindingTargetConfig btc;
                                    btc.provider_id = t.value("provider_id", "");
                                    btc.model_id = t.value("model_id", "");
                                    btc.enabled = t.value("enabled", true);
                                    btc.priority = std::max(0, t.value("priority", 100));
                                    btc.busy_retry_interval_seconds = std::max(0, t.value("busy_retry_interval_seconds", 15));
                                    btc.busy_retry_budget_seconds = std::max(0, t.value("busy_retry_budget_seconds", 90));
                                    btc.busy_cooldown_seconds = std::max(0, t.value("busy_cooldown_seconds", 300));
                                    btc.limit_cooldown_seconds = std::max(0, t.value("limit_cooldown_seconds", 900));
                                    btc.error_cooldown_seconds = std::max(0, t.value("error_cooldown_seconds", 300));
                                    mc.binding_targets.push_back(std::move(btc));
                                }
                            }
                            exp.provider.models.push_back(std::move(mc));
                        }
                    }
                }
                config.exported_providers.push_back(std::move(exp));
            }
        }
        return config;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
    } catch (...) {
        if (error) *error = "Unexpected error loading remote worker JSON.";
    }
    return std::nullopt;
}

bool SaveRemoteProviderWorkerConfig(const RemoteProviderWorkerConfig& config, std::string* error) {
    try {
        if (config.source_path.empty()) { if (error) *error = "Path is empty."; return false; }
        std::error_code ec;
        fs::create_directories(config.source_path.parent_path(), ec);
        json providers = json::array();
        for (const auto& exp : config.exported_providers) {
            json prov = json{
                {"id", exp.provider.id},
                {"name", exp.provider.name},
                {"provider_type", exp.provider.provider_type},
                {"base_url", exp.provider.base_url},
                {"api_key", exp.provider.api_key},
                {"tls_certificate_fingerprint", exp.provider.tls_certificate_fingerprint},
                {"auth_mode", exp.provider.auth_mode},
                {"model_catalog_mode", exp.provider.model_catalog_mode},
            };
            json models = json::array();
            for (const auto& m : exp.provider.models) {
                models.push_back({
                    {"id", m.id},
                    {"display_name", m.display_name},
                    {"context_window", m.context_window},
                    {"max_output_tokens", m.max_output_tokens},
                    {"supports_streaming", m.supports_streaming},
                    {"supports_tools", m.supports_tools},
                    {"supports_vision", m.supports_vision},
                });
            }
            prov["models"] = std::move(models);
            json entry = json{{"provider", std::move(prov)}};
            if (exp.auth_record.has_value()) {
                const auto& a = *exp.auth_record;
                entry["auth_record"] = json{
                    {"credential_id", a.credential_id},
                    {"provider_id", a.provider_id},
                    {"auth_mode", a.auth_mode},
                    {"api_key", a.api_key},
                    {"access_token", a.access_token},
                    {"refresh_token", a.refresh_token},
                    {"token_type", a.token_type},
                    {"account_id", a.account_id},
                    {"account_email", a.account_email},
                    {"account_display_name", a.account_display_name},
                    {"scope", a.scope},
                    {"expires_at", a.expires_at},
                };
            }
            providers.push_back(std::move(entry));
        }
        json root = {
            {"version", config.version},
            {"worker_name", config.worker_name},
            {"agent_server", {
                {"bind_address", config.bind_address},
                {"https_port", config.https_port},
                {"shared_secret", config.shared_secret},
                {"certificate_pem", config.certificate_pem},
                {"private_key_pem", config.private_key_pem},
                {"certificate_fingerprint", config.certificate_fingerprint}
            }},
            {"providers", std::move(providers)}
        };
        std::ofstream output(config.source_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) { if (error) *error = "Could not write remote worker JSON."; return false; }
        output << root.dump(2);
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

// ==================== Headless Run ====================

namespace {

BOOL WINAPI ConsoleControlHandler(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_provider_worker_stop.store(true);
        if (g_provider_worker_server) g_provider_worker_server->stop();
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

void ProxyPostSync(const httplib::Request& req,
                   httplib::Response& res,
                   const RemoteProviderWorkerConfig& config,
                   bool stream,
                   bool ollama_path = false) {
    // Parse model from body
    std::string body = req.body;
    std::string target_model;
    try {
        auto parsed = json::parse(body);
        if (parsed.contains("model")) target_model = Trim(parsed.value("model", ""));
    } catch (...) {}

    const RemoteProviderWorkerExportedProvider* exp = nullptr;
    if (!target_model.empty()) exp = FindProviderForModel(config, target_model);
    if (!exp && !config.exported_providers.empty()) exp = &config.exported_providers.front();
    if (!exp) {
        SetJson(res, json{{"error", "No exported provider available for the requested model."}}, 400);
        return;
    }

    // Handle binding model routing
    const ModelConfig* binding_model = nullptr;
    if (!target_model.empty()) {
        for (const auto& m : exp->provider.models) {
            if (m.id == target_model) {
                binding_model = &m;
                break;
            }
        }
    }
    std::string resolved_model_id = target_model;
    if (binding_model && binding_model->is_binding_model) {
        std::string bind_error;
        const auto* resolved_exp = ResolveBindingUpstream(config, *binding_model, &resolved_model_id, &bind_error);
        if (!resolved_exp) {
            SetJson(res, json{{"error", "Binding resolution failed: " + bind_error}}, 502);
            return;
        }
        exp = resolved_exp;
        // Rewrite model in body
        if (!resolved_model_id.empty() && resolved_model_id != target_model) {
            try {
                auto parsed = json::parse(body);
                parsed["model"] = resolved_model_id;
                body = parsed.dump();
            } catch (...) {}
        }
    }

    // For Ollama local, ensure ollama is running
    std::string upstream_error;
    if (!EnsureOllamaLocalRunning(exp->provider, &upstream_error)) {
        SetJson(res, json{{"error", "Could not start local Ollama: " + upstream_error}}, 502);
        return;
    }

    const std::string base = UpstreamBaseUrl(*exp);
    const std::string path = ollama_path ? "/api/chat" : UpstreamChatPath(*exp);

    // Parse base URL into host/port
    std::string upstream_host = base;
    int upstream_port = 443;
    bool upstream_secure = false;
    {
        std::string u = base;
        if (u.rfind("https://", 0) == 0) { upstream_secure = true; u = u.substr(8); }
        else if (u.rfind("http://", 0) == 0) { upstream_secure = false; u = u.substr(7); }
        size_t colon = u.find(':');
        if (colon != std::string::npos) {
            upstream_host = u.substr(0, colon);
            try { upstream_port = std::stoi(u.substr(colon + 1)); } catch (...) {}
        } else {
            upstream_port = upstream_secure ? 443 : 80;
        }
    }

    if (stream) {
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        res.set_chunked_content_provider(
            ollama_path ? "application/x-ndjson" : "text/event-stream",
            [body, upstream_host, upstream_port, upstream_secure, exp, ollama_path](size_t offset, httplib::DataSink& sink) mutable {
                if (offset > 0) { sink.done(); return true; }
                httplib::Client client(upstream_host, upstream_port);
                client.set_connection_timeout(10, 0);
                client.set_read_timeout(43200, 0); // 12h
                if (upstream_secure) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
                    client.enable_server_certificate_verification(false);
#endif
                }
                std::string auth = UpstreamAuthHeader(*exp);
                httplib::Headers hdrs;
                if (!auth.empty()) hdrs.emplace("Authorization", auth);
                hdrs.emplace("Content-Type", "application/json");
                if (!ollama_path) hdrs.emplace("Accept", "text/event-stream");

                auto response = client.Post(ollama_path ? "/api/chat" : "/v1/chat/completions",
                    hdrs, body, "application/json",
                    [&](const char* data, size_t len) {
                        sink.write(data, len);
                        return true;
                    });
                if (!response) {
                    std::string msg = ollama_path
                        ? json{{"error", "Upstream unreachable"}, {"done", true}}.dump() + "\n"
                        : OpenAiSseChunk("", "", "Upstream unreachable", "stop");
                    sink.write(msg.data(), msg.size());
                }
                sink.done();
                return true;
            });
        return;
    }

    // Non-streaming
    httplib::Client client(upstream_host, upstream_port);
    client.set_connection_timeout(10, 0);
    client.set_read_timeout(43200, 0);
    if (upstream_secure) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        client.enable_server_certificate_verification(false);
#endif
    }
    std::string auth = UpstreamAuthHeader(*exp);
    httplib::Headers hdrs;
    if (!auth.empty()) hdrs.emplace("Authorization", auth);
    auto response = client.Post(path, hdrs, body, "application/json");
    if (!response) {
        SetJson(res, json{{"error", "Could not reach upstream provider."}}, 502);
        return;
    }
    res.status = response->status;
    res.set_content(response->body, "application/json");
}

int RunRemoteProviderWorker(const fs::path& config_path) {
    std::string error;
    auto config = LoadRemoteProviderWorkerConfig(config_path, &error);
    if (!config) {
        WriteLine(L"Could not load remote provider worker config: " + Utf8ToWide(error));
        return 2;
    }
    if (config->shared_secret.empty()) {
        WriteLine(L"Remote worker config has no shared_secret. Generate one first.");
        return 3;
    }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    WriteLine(L"HTTPS support is not compiled into this build. OpenSSL is required.");
    return 4;
#else
    if (config->certificate_fingerprint.empty() && !config->certificate_pem.empty()) {
        // Compute fingerprint from embedded PEM
        BIO* bio = BIO_new_mem_buf(config->certificate_pem.data(), static_cast<int>(config->certificate_pem.size()));
        if (bio) {
            X509* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (x509) {
                unsigned char digest[SHA256_DIGEST_LENGTH] = {};
                unsigned int digest_len = 0;
                if (X509_digest(x509, EVP_sha256(), digest, &digest_len) == 1) {
                    std::ostringstream fp;
                    fp << "SHA256:";
                    fp << std::hex << std::setfill('0');
                    for (unsigned int i = 0; i < digest_len; ++i) {
                        if (i > 0) fp << ":";
                        fp << std::setw(2) << static_cast<int>(digest[i]);
                    }
                    config->certificate_fingerprint = fp.str();
                }
                X509_free(x509);
            }
        }
    }

    const auto tls_files = ResolveRuntimeCertificateFiles(*config, &error);
    if (!tls_files) {
        WriteLine(L"HTTPS certificate/key are missing. Generate them first. Details: " + Utf8ToWide(error));
        return 4;
    }

    httplib::SSLServer server(
        tls_files->first.string().c_str(),
        tls_files->second.string().c_str());
    if (!server.is_valid()) {
        WriteLine(L"Could not create HTTPS worker server. Check the certificate and private key.");
        return 5;
    }
    server.set_read_timeout(std::chrono::hours(12));
    server.set_write_timeout(std::chrono::hours(12));
    server.set_keep_alive_timeout(static_cast<time_t>(3600));

    server.Get("/health", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        json providers = json::array();
        for (const auto& exp : config->exported_providers) {
            providers.push_back({
                {"id", exp.provider.id},
                {"name", exp.provider.name},
                {"provider_type", exp.provider.provider_type},
                {"model_count", static_cast<int>(exp.provider.models.size())},
            });
        }
        SetJson(res, json{
            {"worker_name", config->worker_name},
            {"agent_https_port", config->https_port},
            {"providers", providers}
        });
    });

    server.Get("/v1/models", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        SetJson(res, BuildModelsList(*config));
    });

    server.Get("/api/tags", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        // Ollama model listing: return models from Ollama-local providers
        json models = json::array();
        for (const auto& exp : config->exported_providers) {
            if (NormalizeProviderType(exp.provider.provider_type) != "ollama_local") continue;
            std::string upstream_error;
            if (!EnsureOllamaLocalRunning(exp.provider, &upstream_error)) continue;
            int port = exp.provider.ollama_local_port;
            if (port <= 0) port = 12434;
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(2, 0);
            auto response = client.Get("/api/tags");
            if (response && response->status == 200) {
                try {
                    auto parsed = json::parse(response->body);
                    if (parsed.contains("models") && parsed["models"].is_array()) {
                        for (const auto& m : parsed["models"]) {
                            models.push_back(m);
                        }
                    }
                } catch (...) {}
            }
        }
        SetJson(res, json{{"models", models}});
    });

    server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
        bool stream = false;
        try { auto p = json::parse(req.body); stream = p.value("stream", false); } catch (...) {}
        ProxyPostSync(req, res, *config, stream, false);
    });

    server.Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
        bool stream = false;
        try { auto p = json::parse(req.body); stream = p.value("stream", false); } catch (...) {}
        ProxyPostSync(req, res, *config, stream, true);
    });

    server.Post("/api/generate", [&](const httplib::Request& req, httplib::Response& res) {
        // Route to first Ollama provider
        const RemoteProviderWorkerExportedProvider* exp = nullptr;
        for (const auto& e : config->exported_providers) {
            if (NormalizeProviderType(e.provider.provider_type) == "ollama_local") { exp = &e; break; }
        }
        if (!exp) {
            SetJson(res, json{{"error", "No Ollama provider exported."}}, 400);
            return;
        }
        std::string upstream_error;
        if (!EnsureOllamaLocalRunning(exp->provider, &upstream_error)) {
            SetJson(res, json{{"error", "Could not start local Ollama: " + upstream_error}}, 502);
            return;
        }
        int port = exp->provider.ollama_local_port;
        if (port <= 0) port = 12434;
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(10, 0);
        client.set_read_timeout(43200, 0);
        auto response = client.Post("/api/generate", req.body, "application/json");
        if (!response) {
            SetJson(res, json{{"error", "Upstream Ollama unreachable."}}, 502);
            return;
        }
        res.status = response->status;
        res.set_content(response->body, "application/json");
    });

    g_provider_worker_stop.store(false);
    g_provider_worker_server = &server;
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    WriteLine(L"Remote Provider Worker is ready.");
    WriteLine(L"Worker: " + Utf8ToWide(config->worker_name));
    WriteLine(L"HTTPS listener: " + Utf8ToWide(config->bind_address) + L":" + std::to_wstring(config->https_port));
    WriteLine(L"Exported providers: " + std::to_wstring(config->exported_providers.size()));

    std::thread server_thread([&server, config]() {
        server.listen(config->bind_address.c_str(), config->https_port);
    });

    while (!g_provider_worker_stop.load()) {
        Sleep(100);
    }

    if (g_provider_worker_server) g_provider_worker_server->stop();
    if (server_thread.joinable()) server_thread.join();
    g_provider_worker_server = nullptr;
    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    return 0;
#endif
}

int RunSetupConfig(const fs::path& config_path) {
    std::string error;
    auto config_opt = LoadRemoteProviderWorkerConfig(config_path, &error);
    RemoteProviderWorkerConfig config;
    if (config_opt) {
        config = std::move(*config_opt);
    } else {
        config.source_path = config_path;
        config.worker_name = "Agent Remote Worker";
        config.bind_address = "0.0.0.0";
        config.https_port = 8765;
        config.shared_secret = GenerateRemoteProviderWorkerSharedSecret();
    }
    if (config.shared_secret.empty()) {
        config.shared_secret = GenerateRemoteProviderWorkerSharedSecret();
        WriteLine(L"Generated shared secret.");
    }
    if (config.certificate_pem.empty() || config.private_key_pem.empty()) {
        RemoteProviderWorkerCertificateMaterial material;
        if (!GenerateRemoteProviderWorkerSelfSignedCertificateMaterial(&material, &error)) {
            WriteLine(L"Certificate generation failed: " + Utf8ToWide(error));
            return 4;
        }
        config.certificate_pem = material.certificate_pem;
        config.private_key_pem = material.private_key_pem;
        config.certificate_fingerprint = material.fingerprint;
        WriteLine(L"Generated self-signed certificate.");
    } else if (config.certificate_fingerprint.empty()) {
        BIO* bio = BIO_new_mem_buf(config.certificate_pem.data(), static_cast<int>(config.certificate_pem.size()));
        if (bio) {
            X509* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (x509) {
                unsigned char digest[SHA256_DIGEST_LENGTH] = {};
                unsigned int digest_len = 0;
                if (X509_digest(x509, EVP_sha256(), digest, &digest_len) == 1) {
                    std::ostringstream fp;
                    fp << "SHA256:";
                    fp << std::hex << std::setfill('0');
                    for (unsigned int i = 0; i < digest_len; ++i) {
                        if (i > 0) fp << ":";
                        fp << std::setw(2) << static_cast<int>(digest[i]);
                    }
                    config.certificate_fingerprint = fp.str();
                }
                X509_free(x509);
            }
        }
    }
    if (!SaveRemoteProviderWorkerConfig(config, &error)) {
        WriteLine(L"Setup finished, but config could not be saved: " + Utf8ToWide(error));
        return 6;
    }
    WriteLine(L"Remote Provider Worker setup complete.");
    WriteLine(L"Worker: " + Utf8ToWide(config.worker_name));
    WriteLine(L"HTTPS listener: " + Utf8ToWide(config.bind_address) + L":" + std::to_wstring(config.https_port));
    WriteLine(L"Run the worker with:");
    WriteLine(L"  agent --remote-worker " + QuoteArg(config_path.wstring()));
    return 0;
}

} // namespace

int RunRemoteProviderWorkerCommand(
    RemoteProviderWorkerCommandMode mode,
    const std::filesystem::path& config_path) {
    EnsureConsoleAttached();
    if (config_path.empty()) {
        WriteLine(L"Missing remote provider worker JSON path.");
        WriteLine(L"Usage:");
        WriteLine(L"  agent --remote-worker-setup FILE   Generate shared secret and self-signed certificate");
        WriteLine(L"  agent --remote-worker FILE          Run the HTTPS remote worker");
        return 2;
    }
    if (mode == RemoteProviderWorkerCommandMode::Setup) {
        return RunSetupConfig(config_path);
    }
    return RunRemoteProviderWorker(config_path);
}
