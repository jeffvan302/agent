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

#include "remote_ollama_worker.h"

#include "util.h"

#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <chrono>
#include <ctime>
#include <deque>
#include <functional>
#include <fstream>
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

namespace {
constexpr time_t kLongRunningStreamTimeoutSeconds = 43200;


struct RemoteAgentServerConfig {
    std::string bind_address = "0.0.0.0";
    int https_port = 8765;
    std::string shared_secret;
    fs::path certificate_path = fs::path("certs") / "remote_ollama_worker.crt";
    fs::path private_key_path = fs::path("certs") / "remote_ollama_worker.key";
    std::string certificate_pem;
    std::string private_key_pem;
    std::string certificate_fingerprint;
};

struct RemoteOllamaConfig {
    int start_port = 11434;
    int instance_count = 1;
    std::string accelerator = "both"; // cpu, gpu, both
    int cpu_thread_percent = 50;
    int num_thread = 0;        // 0 = auto: logical CPU threads / instance_count.
    int num_parallel = 1;
    int max_loaded_models = 1;
    int max_queue = 512;
    int context_length = 0;
    std::string keep_alive;
    bool flash_attention = false;
};

struct RemoteModelConfig {
    std::string name = "qwen2.5vl:7b";
    std::string purpose = "image_ingestion";
};

struct RemoteQueueConfig {
    int max_queue_size = 100;
};

struct RemoteWorkerConfig {
    int version = 1;
    std::string worker_name = "Remote Ollama Worker";
    RemoteAgentServerConfig agent_server;
    RemoteOllamaConfig ollama;
    RemoteModelConfig model;
    RemoteQueueConfig queue;
    fs::path source_path;
};

struct ManagedOllamaProcess {
    HANDLE process = nullptr;
    DWORD process_id = 0;
    int port = 0;
};

struct QueueJob {
    std::string id;
    std::string endpoint;
    std::string body;
    std::string status = "queued";
    std::string error;
    std::string response_body;
    int response_status = 502;
    int worker_port = 0;
    std::chrono::system_clock::time_point submitted_at = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    mutable std::mutex mutex;
    std::condition_variable cv;
};

std::atomic_uint64_t g_job_counter = 0;
std::atomic_bool g_console_stop_requested = false;
httplib::Server* g_active_server = nullptr;

int ClampInstanceCount(int value) {
    return std::clamp(value <= 0 ? 1 : value, 1, 32);
}

int ClampPort(int value, int fallback) {
    return std::clamp(value <= 0 ? fallback : value, 1, 65535);
}

int ClampPositiveOrZero(int value, int max_value) {
    return std::clamp(value, 0, max_value);
}

std::string NormalizeOllamaAccelerator(std::string value) {
    value = Trim(value);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "cpu" || value == "cpu_only" || value == "none") {
        return "cpu";
    }
    if (value == "gpu" || value == "gpu_only") {
        return "gpu";
    }
    if (value == "hybrid" || value == "auto" || value == "gpu_cpu" || value == "cpu_gpu") {
        return "both";
    }
    return "both";
}

int ClampCpuThreadPercent(int value) {
    return std::clamp(value <= 0 ? 50 : value, 1, 100);
}

int EffectiveOllamaNumThread(const RemoteOllamaConfig& config) {
    if (config.num_thread > 0) {
        return config.num_thread;
    }
    const unsigned int logical_threads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned int usable_threads = std::max(
        1u,
        (logical_threads * static_cast<unsigned int>(ClampCpuThreadPercent(config.cpu_thread_percent)) + 99u) / 100u);
    const int instances = ClampInstanceCount(config.instance_count);
    return std::max(1, static_cast<int>(usable_threads / static_cast<unsigned int>(instances)));
}

std::optional<int> OllamaNumGpuOverride(const RemoteOllamaConfig& config) {
    const std::string accelerator = NormalizeOllamaAccelerator(config.accelerator);
    if (accelerator == "cpu") {
        return 0;
    }
    if (accelerator == "gpu") {
        // Ollama/llama.cpp treats a very high layer count as "offload everything
        // that fits". If the model is larger than VRAM, Ollama may still fail or
        // partially offload depending on backend/model support.
        return 999;
    }
    return std::nullopt;
}

void EnsureConsoleAttached() {
    static bool attempted = false;
    if (attempted) {
        return;
    }
    attempted = true;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) {
        AllocConsole();
    }
}

void WriteLine(const std::wstring& text) {
    EnsureConsoleAttached();
    const std::wstring line = text + L"\r\n";
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (WriteConsoleW(output, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr)) {
            return;
        }
        const std::string utf8 = WideToUtf8(line);
        WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        return;
    }
    OutputDebugStringW(line.c_str());
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring ExpandEnvironmentPath(const std::wstring& value) {
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) {
        return value;
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
    if (written == 0 || written > needed) {
        return value;
    }
    expanded.resize(written > 0 ? written - 1 : 0);
    return expanded;
}

std::string ReadTextFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool WriteTextFile(const fs::path& path, const std::string& text, std::string* error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error) {
            *error = "Could not write " + WideToUtf8(path.wstring()) + ".";
        }
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
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
    while (!result.empty() && (result.front() == '.' || result.front() == '_')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == '.' || result.back() == '_')) {
        result.pop_back();
    }
    return result.empty() ? "worker" : result;
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
std::string CertificateFingerprintFromX509(X509* x509, std::string* error) {
    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    unsigned int digest_len = 0;
    const bool ok = X509_digest(x509, EVP_sha256(), digest, &digest_len) == 1;
    if (!ok) {
        if (error) {
            *error = "Could not compute certificate fingerprint.";
        }
        return {};
    }

    std::ostringstream out;
    out << "SHA256:";
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        if (i > 0) {
            out << ":";
        }
        out << std::setw(2) << static_cast<int>(digest[i]);
    }
    return out.str();
}

std::string BioToString(BIO* bio, std::string* error, const char* label) {
    BUF_MEM* mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    if (!mem || !mem->data || mem->length == 0) {
        if (error) {
            *error = std::string("Could not export ") + label + " PEM.";
        }
        return {};
    }
    return std::string(mem->data, mem->length);
}

std::string RemoteWorkerCertificateFingerprintFromPem(const std::string& cert_pem, std::string* error) {
    BIO* bio = BIO_new_mem_buf(cert_pem.data(), static_cast<int>(cert_pem.size()));
    if (!bio) {
        if (error) {
            *error = "Could not allocate certificate PEM parser.";
        }
        return {};
    }

    X509* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!x509) {
        if (error) {
            *error = "Could not parse embedded certificate PEM.";
        }
        return {};
    }

    std::string fingerprint = CertificateFingerprintFromX509(x509, error);
    X509_free(x509);
    return fingerprint;
}
#endif

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
std::string RemoteWorkerCertificateFingerprintFromPem(const std::string&, std::string* error) {
    if (error) {
        *error = "OpenSSL support is not compiled into this build.";
    }
    return {};
}
#endif

std::optional<fs::path> FindExecutable(const std::wstring& executable_name) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD found = SearchPathW(nullptr, executable_name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (found > 0 && found < buffer.size()) {
        return fs::path(buffer.data());
    }

    if (_wcsicmp(executable_name.c_str(), L"ollama.exe") == 0) {
        const std::vector<std::wstring> candidates = {
            ExpandEnvironmentPath(L"%LOCALAPPDATA%\\Programs\\Ollama\\ollama.exe"),
            ExpandEnvironmentPath(L"%ProgramFiles%\\Ollama\\ollama.exe"),
            ExpandEnvironmentPath(L"%ProgramFiles(x86)%\\Ollama\\ollama.exe"),
        };
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!candidate.empty() && fs::exists(candidate, ec)) {
                return fs::path(candidate);
            }
        }
    }

    return std::nullopt;
}

bool RunCommandLine(std::wstring command_line, DWORD timeout_ms, DWORD* exit_code) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    HANDLE std_output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE std_error = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE std_input = GetStdHandle(STD_INPUT_HANDLE);
    if (std_output && std_output != INVALID_HANDLE_VALUE &&
        std_error && std_error != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = std_output;
        startup.hStdError = std_error;
        startup.hStdInput = (std_input && std_input != INVALID_HANDLE_VALUE) ? std_input : nullptr;
        inherit_handles = TRUE;
    }

    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, inherit_handles, 0, nullptr, nullptr, &startup, &process)) {
        WriteLine(L"Failed to start command. CreateProcess error: " + std::to_wstring(GetLastError()));
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    DWORD process_exit_code = 1;
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        process_exit_code = 1;
    } else {
        GetExitCodeProcess(process.hProcess, &process_exit_code);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code) {
        *exit_code = process_exit_code;
    }
    return wait_result != WAIT_TIMEOUT && process_exit_code == 0;
}

fs::path ResolveRelativeToConfig(const fs::path& config_path, const fs::path& value) {
    if (value.empty() || value.is_absolute()) {
        return value;
    }
    const fs::path base = config_path.empty() ? fs::current_path() : config_path.parent_path();
    return (base / value).lexically_normal();
}

std::string JsonString(const json& data, const char* key, const std::string& fallback = {}) {
    if (!data.contains(key) || data.at(key).is_null()) {
        return fallback;
    }
    if (data.at(key).is_string()) {
        return data.at(key).get<std::string>();
    }
    return fallback;
}

int JsonInt(const json& data, const char* key, int fallback) {
    if (!data.contains(key) || data.at(key).is_null()) {
        return fallback;
    }
    if (data.at(key).is_number_integer()) {
        return data.at(key).get<int>();
    }
    if (data.at(key).is_string()) {
        try {
            return std::stoi(data.at(key).get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

std::optional<RemoteWorkerConfig> LoadRemoteWorkerConfig(const fs::path& path, std::string* error) {
    try {
        if (path.empty() || !fs::exists(path)) {
            if (error) {
                *error = "Remote worker JSON file was not found.";
            }
            return std::nullopt;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            if (error) {
                *error = "Could not open remote worker JSON file.";
            }
            return std::nullopt;
        }

        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto data = json::parse(text);

        RemoteWorkerConfig config;
        config.source_path = path;
        config.version = JsonInt(data, "version", 1);
        config.worker_name = Trim(JsonString(data, "worker_name", config.worker_name));
        if (config.worker_name.empty()) {
            config.worker_name = "Remote Ollama Worker";
        }

        if (data.contains("agent_server") && data["agent_server"].is_object()) {
            const auto& server = data["agent_server"];
            config.agent_server.bind_address = Trim(JsonString(server, "bind_address", config.agent_server.bind_address));
            config.agent_server.https_port = ClampPort(JsonInt(server, "https_port", config.agent_server.https_port), 8765);
            config.agent_server.shared_secret = Trim(JsonString(server, "shared_secret", ""));
            config.agent_server.certificate_path = fs::path(Utf8ToWide(JsonString(server, "certificate_path", "certs/remote_ollama_worker.crt")));
            config.agent_server.private_key_path = fs::path(Utf8ToWide(JsonString(server, "private_key_path", "certs/remote_ollama_worker.key")));
            config.agent_server.certificate_pem = JsonString(server, "certificate_pem", "");
            config.agent_server.private_key_pem = JsonString(server, "private_key_pem", "");
            config.agent_server.certificate_fingerprint = Trim(JsonString(server, "certificate_fingerprint", ""));
        } else {
            config.agent_server.bind_address = Trim(JsonString(data, "bind_address", config.agent_server.bind_address));
            config.agent_server.https_port = ClampPort(JsonInt(data, "https_port", config.agent_server.https_port), 8765);
            config.agent_server.shared_secret = Trim(JsonString(data, "shared_secret", ""));
            config.agent_server.certificate_path = fs::path(Utf8ToWide(JsonString(data, "certificate_path", "certs/remote_ollama_worker.crt")));
            config.agent_server.private_key_path = fs::path(Utf8ToWide(JsonString(data, "private_key_path", "certs/remote_ollama_worker.key")));
            config.agent_server.certificate_pem = JsonString(data, "certificate_pem", "");
            config.agent_server.private_key_pem = JsonString(data, "private_key_pem", "");
            config.agent_server.certificate_fingerprint = Trim(JsonString(data, "certificate_fingerprint", ""));
        }

        if (data.contains("ollama") && data["ollama"].is_object()) {
            const auto& ollama = data["ollama"];
            config.ollama.start_port = ClampPort(JsonInt(ollama, "start_port", config.ollama.start_port), 11434);
            config.ollama.instance_count = ClampInstanceCount(JsonInt(ollama, "instance_count", config.ollama.instance_count));
            config.ollama.accelerator = NormalizeOllamaAccelerator(JsonString(ollama, "accelerator", config.ollama.accelerator));
            config.ollama.cpu_thread_percent = ClampCpuThreadPercent(JsonInt(ollama, "cpu_thread_percent", config.ollama.cpu_thread_percent));
            config.ollama.num_thread = ClampPositiveOrZero(
                JsonInt(ollama, "num_thread", JsonInt(ollama, "num_threads", config.ollama.num_thread)),
                1024);
            config.ollama.num_parallel = std::clamp(JsonInt(ollama, "num_parallel", config.ollama.num_parallel), 1, 1024);
            config.ollama.max_loaded_models = std::clamp(JsonInt(ollama, "max_loaded_models", config.ollama.max_loaded_models), 1, 1024);
            config.ollama.max_queue = std::clamp(JsonInt(ollama, "max_queue", config.ollama.max_queue), 1, 100000);
            config.ollama.context_length = ClampPositiveOrZero(JsonInt(ollama, "context_length", config.ollama.context_length), 1048576);
            config.ollama.keep_alive = Trim(JsonString(ollama, "keep_alive", config.ollama.keep_alive));
            config.ollama.flash_attention = ollama.value("flash_attention", config.ollama.flash_attention);
        } else {
            config.ollama.start_port = ClampPort(JsonInt(data, "ollama_start_port", config.ollama.start_port), 11434);
            config.ollama.instance_count = ClampInstanceCount(JsonInt(data, "ollama_instance_count", config.ollama.instance_count));
            config.ollama.accelerator = NormalizeOllamaAccelerator(JsonString(data, "ollama_accelerator", config.ollama.accelerator));
            config.ollama.cpu_thread_percent = ClampCpuThreadPercent(JsonInt(data, "ollama_cpu_thread_percent", config.ollama.cpu_thread_percent));
            config.ollama.num_thread = ClampPositiveOrZero(
                JsonInt(data, "ollama_num_thread", JsonInt(data, "ollama_num_threads", config.ollama.num_thread)),
                1024);
            config.ollama.num_parallel = std::clamp(JsonInt(data, "ollama_num_parallel", config.ollama.num_parallel), 1, 1024);
            config.ollama.max_loaded_models = std::clamp(JsonInt(data, "ollama_max_loaded_models", config.ollama.max_loaded_models), 1, 1024);
            config.ollama.max_queue = std::clamp(JsonInt(data, "ollama_max_queue", config.ollama.max_queue), 1, 100000);
            config.ollama.context_length = ClampPositiveOrZero(JsonInt(data, "ollama_context_length", config.ollama.context_length), 1048576);
            config.ollama.keep_alive = Trim(JsonString(data, "ollama_keep_alive", config.ollama.keep_alive));
            config.ollama.flash_attention = data.value("ollama_flash_attention", config.ollama.flash_attention);
        }

        if (data.contains("model") && data["model"].is_object()) {
            const auto& model = data["model"];
            config.model.name = Trim(JsonString(model, "name", config.model.name));
            config.model.purpose = Trim(JsonString(model, "purpose", config.model.purpose));
        } else {
            config.model.name = Trim(JsonString(data, "vision_model", config.model.name));
            config.model.purpose = "image_ingestion";
        }
        if (config.model.name.empty()) {
            config.model.name = "qwen2.5vl:7b";
        }
        if (config.model.purpose.empty()) {
            config.model.purpose = "provider";
        }

        if (data.contains("queue") && data["queue"].is_object()) {
            config.queue.max_queue_size = std::clamp(JsonInt(data["queue"], "max_queue_size", config.queue.max_queue_size), 1, 10000);
        }

        if (config.ollama.start_port + config.ollama.instance_count - 1 > 65535) {
            if (error) {
                *error = "Ollama port range exceeds 65535.";
            }
            return std::nullopt;
        }

        config.agent_server.certificate_path = ResolveRelativeToConfig(path, config.agent_server.certificate_path);
        config.agent_server.private_key_path = ResolveRelativeToConfig(path, config.agent_server.private_key_path);
        return config;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
    } catch (...) {
        if (error) {
            *error = "Unexpected error loading remote worker JSON.";
        }
    }
    return std::nullopt;
}

json RemoteWorkerConfigToJson(const RemoteWorkerConfig& config, bool relative_paths) {
    auto path_to_string = [&](const fs::path& value) {
        fs::path out = value;
        if (relative_paths && !config.source_path.empty()) {
            std::error_code ec;
            fs::path rel = fs::relative(value, config.source_path.parent_path(), ec);
            if (!ec && !rel.empty()) {
                out = rel;
            }
        }
        return WideToUtf8(out.wstring());
    };

    return json{
        {"version", config.version},
        {"worker_name", config.worker_name},
        {"agent_server", {
            {"bind_address", config.agent_server.bind_address},
            {"https_port", config.agent_server.https_port},
            {"shared_secret", config.agent_server.shared_secret},
            {"certificate_path", path_to_string(config.agent_server.certificate_path)},
            {"private_key_path", path_to_string(config.agent_server.private_key_path)},
            {"certificate_pem", config.agent_server.certificate_pem},
            {"private_key_pem", config.agent_server.private_key_pem},
            {"certificate_fingerprint", config.agent_server.certificate_fingerprint}
        }},
        {"ollama", {
            {"start_port", config.ollama.start_port},
            {"instance_count", config.ollama.instance_count},
            {"accelerator", NormalizeOllamaAccelerator(config.ollama.accelerator)},
            {"cpu_thread_percent", ClampCpuThreadPercent(config.ollama.cpu_thread_percent)},
            {"num_thread", config.ollama.num_thread},
            {"effective_num_thread", EffectiveOllamaNumThread(config.ollama)},
            {"num_gpu", OllamaNumGpuOverride(config.ollama).value_or(-1)},
            {"num_parallel", config.ollama.num_parallel},
            {"max_loaded_models", config.ollama.max_loaded_models},
            {"max_queue", config.ollama.max_queue},
            {"context_length", config.ollama.context_length},
            {"keep_alive", config.ollama.keep_alive},
            {"flash_attention", config.ollama.flash_attention}
        }},
        {"model", {
            {"name", config.model.name},
            {"purpose", config.model.purpose}
        }},
        {"queue", {
            {"enabled", true},
            {"max_queue_size", config.queue.max_queue_size},
            {"status_updates", true}
        }}
    };
}

bool SaveRemoteWorkerConfig(RemoteWorkerConfig config, std::string* error) {
    try {
        if (config.source_path.empty()) {
            if (error) {
                *error = "Remote worker JSON path is empty.";
            }
            return false;
        }
        std::error_code ec;
        fs::create_directories(config.source_path.parent_path(), ec);
        std::ofstream output(config.source_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error) {
                *error = "Could not write remote worker JSON file.";
            }
            return false;
        }
        output << RemoteWorkerConfigToJson(config, true).dump(2);
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
}

std::string FingerprintPathFragment(const std::string& fingerprint) {
    std::string result;
    for (char ch : fingerprint) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    if (result.size() > 16) {
        result.resize(16);
    }
    return result.empty() ? "default" : result;
}

std::optional<std::pair<fs::path, fs::path>> ResolveRuntimeCertificateFiles(
    const RemoteWorkerConfig& config,
    std::string* error) {
    if (!config.agent_server.certificate_pem.empty() && !config.agent_server.private_key_pem.empty()) {
        fs::path temp_root;
        wchar_t temp_path[MAX_PATH] = {};
        const DWORD temp_len = GetTempPathW(static_cast<DWORD>(std::size(temp_path)), temp_path);
        if (temp_len > 0 && temp_len < std::size(temp_path)) {
            temp_root = fs::path(temp_path);
        } else {
            temp_root = fs::temp_directory_path();
        }

        fs::path runtime_dir = temp_root / "agent_remote_workers" /
            fs::path(Utf8ToWide(SafePathSegment(config.worker_name))) /
            FingerprintPathFragment(config.agent_server.certificate_fingerprint);
        fs::path cert_path = runtime_dir / "server.crt";
        fs::path key_path = runtime_dir / "server.key";
        if (!WriteTextFile(cert_path, config.agent_server.certificate_pem, error) ||
            !WriteTextFile(key_path, config.agent_server.private_key_pem, error)) {
            return std::nullopt;
        }
        return std::make_pair(cert_path, key_path);
    }

    if (fs::exists(config.agent_server.certificate_path) &&
        fs::exists(config.agent_server.private_key_path)) {
        return std::make_pair(config.agent_server.certificate_path, config.agent_server.private_key_path);
    }

    if (error) {
        *error = "HTTPS certificate/key are missing. Generate or embed them in the Remote Ollama Setup JSON first.";
    }
    return std::nullopt;
}

bool EnsureOllamaInstalled(fs::path* ollama_path) {
    if (auto existing = FindExecutable(L"ollama.exe")) {
        if (ollama_path) {
            *ollama_path = *existing;
        }
        WriteLine(L"Ollama is already installed: " + existing->wstring());
        return true;
    }

    if (!FindExecutable(L"winget.exe")) {
        WriteLine(L"Ollama is missing and winget.exe was not found. Install Ollama manually first.");
        return false;
    }

    WriteLine(L"Ollama is not installed. Installing Ollama with winget...");
    DWORD exit_code = 1;
    const bool installed = RunCommandLine(
        L"cmd.exe /c winget install --id Ollama.Ollama -e --accept-package-agreements --accept-source-agreements",
        INFINITE,
        &exit_code);
    if (!installed) {
        WriteLine(L"Ollama installer failed with exit code " + std::to_wstring(exit_code) + L".");
        return false;
    }

    if (auto installed_path = FindExecutable(L"ollama.exe")) {
        if (ollama_path) {
            *ollama_path = *installed_path;
        }
        WriteLine(L"Ollama installed: " + installed_path->wstring());
        return true;
    }

    WriteLine(L"Ollama installation finished, but ollama.exe is not visible to this process yet. Restart this shell or provide Ollama on PATH.");
    return false;
}

bool PullOllamaModel(const fs::path& ollama_path, const std::string& model) {
    WriteLine(L"Pulling Ollama model: " + Utf8ToWide(model));
    DWORD exit_code = 1;
    const std::wstring command = QuoteArgument(ollama_path.wstring()) + L" pull " + QuoteArgument(Utf8ToWide(model));
    const bool pulled = RunCommandLine(command, INFINITE, &exit_code);
    if (!pulled) {
        WriteLine(L"Model pull failed with exit code " + std::to_wstring(exit_code) + L".");
    }
    return pulled;
}

bool IsOllamaEndpointAvailable(int port) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(1, 0);
    auto response = client.Get("/api/tags");
    return response && response->status >= 200 && response->status < 500;
}

bool SetTemporaryEnvironmentVariable(
    const std::wstring& name,
    const std::wstring& value,
    std::wstring* previous_value,
    bool* had_previous_value) {
    DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (had_previous_value) {
        *had_previous_value = size > 0;
    }
    if (previous_value) {
        previous_value->clear();
        if (size > 0) {
            previous_value->resize(size, L'\0');
            const DWORD written = GetEnvironmentVariableW(name.c_str(), previous_value->data(), size);
            previous_value->resize(written);
        }
    }
    return SetEnvironmentVariableW(name.c_str(), value.c_str()) != FALSE;
}

void RestoreEnvironmentVariable(const std::wstring& name, const std::wstring& previous_value, bool had_previous_value) {
    if (had_previous_value) {
        SetEnvironmentVariableW(name.c_str(), previous_value.c_str());
    } else {
        SetEnvironmentVariableW(name.c_str(), nullptr);
    }
}

bool StartOllamaServer(
    const fs::path& ollama_path,
    int port,
    const RemoteOllamaConfig& config,
    ManagedOllamaProcess* managed_process) {
    const std::wstring host = L"127.0.0.1:" + std::to_wstring(port);

    struct TemporaryEnvironmentChange {
        std::wstring name;
        std::wstring previous_value;
        bool had_previous_value = false;
    };
    std::vector<TemporaryEnvironmentChange> environment_changes;
    auto set_environment = [&](const std::wstring& name, const std::wstring& value) {
        TemporaryEnvironmentChange change;
        change.name = name;
        SetTemporaryEnvironmentVariable(name, value, &change.previous_value, &change.had_previous_value);
        environment_changes.push_back(std::move(change));
    };
    auto restore_environment = [&]() {
        for (auto it = environment_changes.rbegin(); it != environment_changes.rend(); ++it) {
            RestoreEnvironmentVariable(it->name, it->previous_value, it->had_previous_value);
        }
        environment_changes.clear();
    };

    const int effective_threads = EffectiveOllamaNumThread(config);
    set_environment(L"OLLAMA_HOST", host);
    set_environment(L"OLLAMA_NUM_THREAD", std::to_wstring(effective_threads));
    set_environment(L"OLLAMA_NUM_PARALLEL", std::to_wstring(std::max(1, config.num_parallel)));
    set_environment(L"OLLAMA_MAX_LOADED_MODELS", std::to_wstring(std::max(1, config.max_loaded_models)));
    set_environment(L"OLLAMA_MAX_QUEUE", std::to_wstring(std::max(1, config.max_queue)));
    if (config.context_length > 0) {
        set_environment(L"OLLAMA_CONTEXT_LENGTH", std::to_wstring(config.context_length));
    }
    if (!config.keep_alive.empty()) {
        set_environment(L"OLLAMA_KEEP_ALIVE", Utf8ToWide(config.keep_alive));
    }
    if (config.flash_attention) {
        set_environment(L"OLLAMA_FLASH_ATTENTION", L"1");
    }
    if (NormalizeOllamaAccelerator(config.accelerator) == "cpu") {
        set_environment(L"CUDA_VISIBLE_DEVICES", L"-1");
        set_environment(L"HIP_VISIBLE_DEVICES", L"-1");
        set_environment(L"ROCR_VISIBLE_DEVICES", L"-1");
        set_environment(L"GPU_DEVICE_ORDINAL", L"-1");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    HANDLE std_output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE std_error = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE std_input = GetStdHandle(STD_INPUT_HANDLE);
    if (std_output && std_output != INVALID_HANDLE_VALUE &&
        std_error && std_error != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = std_output;
        startup.hStdError = std_error;
        startup.hStdInput = (std_input && std_input != INVALID_HANDLE_VALUE) ? std_input : nullptr;
        inherit_handles = TRUE;
    }

    std::wstring command_line = QuoteArgument(ollama_path.wstring()) + L" serve";
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    PROCESS_INFORMATION process{};

    WriteLine(L"Starting local Ollama on 127.0.0.1:" + std::to_wstring(port) + L"...");
    const BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, inherit_handles, 0, nullptr, nullptr, &startup, &process);

    restore_environment();

    if (!created) {
        WriteLine(L"Failed to start Ollama on port " + std::to_wstring(port) + L". CreateProcess error: " + std::to_wstring(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);

    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(500);
        if (IsOllamaEndpointAvailable(port)) {
            if (managed_process) {
                managed_process->process = process.hProcess;
                managed_process->process_id = process.dwProcessId;
                managed_process->port = port;
            } else {
                CloseHandle(process.hProcess);
            }
            WriteLine(L"Ollama is responding on local port " + std::to_wstring(port) + L".");
            WriteLine(L"  Runtime tuning: num_thread=" + std::to_wstring(effective_threads) +
                L", OLLAMA_NUM_PARALLEL=" + std::to_wstring(std::max(1, config.num_parallel)) +
                L", OLLAMA_MAX_LOADED_MODELS=" + std::to_wstring(std::max(1, config.max_loaded_models)) +
                L", OLLAMA_MAX_QUEUE=" + std::to_wstring(std::max(1, config.max_queue)) + L".");
            return true;
        }
    }

    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    WriteLine(L"Ollama started but did not become ready on local port " + std::to_wstring(port) + L".");
    return false;
}

HANDLE CreateKillOnCloseJobObject() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        WriteLine(L"Warning: could not create an Ollama worker job object. Child Ollama cleanup will rely on graceful shutdown.");
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        WriteLine(L"Warning: could not enable kill-on-close for the Ollama worker job object.");
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

void AssignManagedProcessToJob(HANDLE job, const ManagedOllamaProcess& process) {
    if (!job || !process.process) {
        return;
    }
    if (!AssignProcessToJobObject(job, process.process)) {
        WriteLine(L"Warning: could not attach Ollama on local port " +
            std::to_wstring(process.port) +
            L" to the cleanup job object. It will still be stopped during graceful shutdown.");
    }
}

void StopManagedProcesses(std::vector<ManagedOllamaProcess>& processes) {
    for (auto& process : processes) {
        if (!process.process) {
            continue;
        }
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process.process, &exit_code) && exit_code == STILL_ACTIVE) {
            WriteLine(L"Stopping Ollama on local port " + std::to_wstring(process.port) + L"...");
            TerminateProcess(process.process, 0);
            WaitForSingleObject(process.process, 5000);
        }
        CloseHandle(process.process);
        process.process = nullptr;
    }
}

std::string TimePointToIso(const std::chrono::system_clock::time_point& time_point) {
    if (time_point.time_since_epoch().count() == 0) {
        return {};
    }
    const std::time_t tt = std::chrono::system_clock::to_time_t(time_point);
    std::tm tm{};
    gmtime_s(&tm, &tt);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string NewJobId() {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "job_" + std::to_string(now) + "_" + std::to_string(++g_job_counter);
}

std::mutex& OllamaPortMutex(int port) {
    static std::mutex map_mutex;
    static std::unordered_map<int, std::shared_ptr<std::mutex>> mutexes;
    std::lock_guard<std::mutex> lock(map_mutex);
    auto& value = mutexes[port];
    if (!value) {
        value = std::make_shared<std::mutex>();
    }
    return *value;
}

struct OllamaPortLease {
    int port = 0;
    std::unique_lock<std::mutex> lock;
};

OllamaPortLease AcquireOllamaPortLease(const std::vector<int>& ports) {
    for (int port : ports) {
        std::unique_lock<std::mutex> candidate(OllamaPortMutex(port), std::try_to_lock);
        if (candidate.owns_lock()) {
            return OllamaPortLease{port, std::move(candidate)};
        }
    }
    const int fallback_port = ports.empty() ? 11434 : ports.front();
    return OllamaPortLease{fallback_port, std::unique_lock<std::mutex>(OllamaPortMutex(fallback_port))};
}

bool IsAllowedEndpoint(const std::string& endpoint) {
    return endpoint == "/api/generate" ||
           endpoint == "/api/chat" ||
           endpoint == "/api/embeddings";
}

std::string ApplyOllamaRequestOptions(
    const std::string& endpoint,
    const std::string& body,
    const RemoteWorkerConfig& config) {
    if (endpoint != "/api/generate" && endpoint != "/api/chat") {
        return body;
    }

    try {
        auto payload = json::parse(body);
        if (!payload.is_object()) {
            return body;
        }
        if (!payload.contains("options") || !payload["options"].is_object()) {
            payload["options"] = json::object();
        }

        auto& options = payload["options"];
        const int num_thread = EffectiveOllamaNumThread(config.ollama);
        if (num_thread > 0 && !options.contains("num_thread")) {
            options["num_thread"] = num_thread;
        }
        if (const auto num_gpu = OllamaNumGpuOverride(config.ollama)) {
            if (!options.contains("num_gpu")) {
                options["num_gpu"] = *num_gpu;
            }
        }
        if (config.ollama.context_length > 0 && !options.contains("num_ctx")) {
            options["num_ctx"] = config.ollama.context_length;
        }
        if (!config.ollama.keep_alive.empty() && !payload.contains("keep_alive")) {
            payload["keep_alive"] = config.ollama.keep_alive;
        }
        return payload.dump();
    } catch (...) {
        return body;
    }
}

class RemoteOllamaQueue {
public:
    RemoteOllamaQueue(std::vector<int> ports, RemoteWorkerConfig config)
        : ports_(std::move(ports)), config_(std::move(config)) {}

    ~RemoteOllamaQueue() {
        Stop();
    }

    void Start() {
        stop_.store(false);
        for (int port : ports_) {
            worker_threads_.emplace_back([this, port]() {
                WorkerLoop(port);
            });
        }
    }

    void Stop() {
        stop_.store(true);
        queue_cv_.notify_all();
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();
    }

    std::shared_ptr<QueueJob> Submit(const std::string& endpoint, const std::string& body, std::string* error) {
        if (!IsAllowedEndpoint(endpoint)) {
            if (error) {
                *error = "Unsupported Ollama endpoint.";
            }
            return nullptr;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (static_cast<int>(queue_.size()) >= config_.queue.max_queue_size) {
            if (error) {
                *error = "Remote worker queue is full.";
            }
            return nullptr;
        }

        auto job = std::make_shared<QueueJob>();
        job->id = NewJobId();
        job->endpoint = endpoint;
        job->body = ApplyOllamaRequestOptions(endpoint, body, config_);
        jobs_[job->id] = job;
        queue_.push_back(job);
        queue_cv_.notify_one();
        return job;
    }

    std::optional<json> SnapshotJob(const std::string& job_id) const {
        std::shared_ptr<QueueJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) {
                return std::nullopt;
            }
            job = it->second;
        }

        std::lock_guard<std::mutex> job_lock(job->mutex);
        json result = {
            {"id", job->id},
            {"status", job->status},
            {"endpoint", job->endpoint},
            {"queue_position", QueuePositionNoJobLock(job->id)},
            {"worker_port", job->worker_port},
            {"submitted_at", TimePointToIso(job->submitted_at)},
            {"started_at", TimePointToIso(job->started_at)},
            {"completed_at", TimePointToIso(job->completed_at)}
        };
        if (!job->error.empty()) {
            result["error"] = job->error;
        }
        if (job->status == "completed" || job->status == "failed") {
            result["response_status"] = job->response_status;
            result["response_body"] = job->response_body;
        }
        return result;
    }

    json QueueSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        int queued = 0;
        int processing = 0;
        int completed = 0;
        int failed = 0;
        for (const auto& [id, job] : jobs_) {
            (void)id;
            std::lock_guard<std::mutex> job_lock(job->mutex);
            if (job->status == "queued") {
                ++queued;
            } else if (job->status == "processing") {
                ++processing;
            } else if (job->status == "completed") {
                ++completed;
            } else if (job->status == "failed") {
                ++failed;
            }
        }
        return json{
            {"queued", queued},
            {"processing", processing},
            {"completed", completed},
            {"failed", failed},
            {"workers", static_cast<int>(ports_.size())},
            {"ports", ports_}
        };
    }

    int QueuePosition(const std::string& job_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return QueuePositionLocked(job_id);
    }

private:
    int QueuePositionNoJobLock(const std::string& job_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return QueuePositionLocked(job_id);
    }

    int QueuePositionLocked(const std::string& job_id) const {
        int position = 1;
        for (const auto& job : queue_) {
            if (job && job->id == job_id) {
                return position;
            }
            ++position;
        }
        return 0;
    }

    void WorkerLoop(int port) {
        for (;;) {
            std::shared_ptr<QueueJob> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                queue_cv_.wait(lock, [this]() {
                    return stop_.load() || !queue_.empty();
                });
                if (stop_.load() && queue_.empty()) {
                    return;
                }
                job = queue_.front();
                queue_.pop_front();
            }

            if (!job) {
                continue;
            }

            {
                std::lock_guard<std::mutex> job_lock(job->mutex);
                job->status = "processing";
                job->worker_port = port;
                job->started_at = std::chrono::system_clock::now();
            }
            job->cv.notify_all();

            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(10, 0);
            client.set_read_timeout(kLongRunningStreamTimeoutSeconds, 0);
            client.set_write_timeout(kLongRunningStreamTimeoutSeconds, 0);

            std::unique_lock<std::mutex> port_lock(OllamaPortMutex(port));
            auto response = client.Post(job->endpoint.c_str(), job->body, "application/json");
            {
                std::lock_guard<std::mutex> job_lock(job->mutex);
                job->completed_at = std::chrono::system_clock::now();
                if (response) {
                    job->response_status = response->status;
                    job->response_body = response->body;
                    job->status = response->status >= 200 && response->status < 300 ? "completed" : "failed";
                    if (job->status == "failed") {
                        job->error = "Ollama returned HTTP " + std::to_string(response->status) + ".";
                    }
                } else {
                    job->status = "failed";
                    job->response_status = 502;
                    job->error = "No response from local Ollama worker on port " + std::to_string(port) + ".";
                    job->response_body = json{{"error", job->error}}.dump();
                }
            }
            job->cv.notify_all();
        }
    }

    std::vector<int> ports_;
    RemoteWorkerConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::shared_ptr<QueueJob>> queue_;
    std::unordered_map<std::string, std::shared_ptr<QueueJob>> jobs_;
    std::vector<std::thread> worker_threads_;
    std::atomic_bool stop_ = false;
};

bool Authorized(const httplib::Request& req, const std::string& shared_secret) {
    if (shared_secret.empty()) {
        return false;
    }
    const std::string auth = req.get_header_value("Authorization");
    if (auth == "Bearer " + shared_secret) {
        return true;
    }
    const std::string secret_header = req.get_header_value("X-Agent-Shared-Secret");
    return secret_header == shared_secret;
}

void SetJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(2), "application/json");
}

json ConvertOllamaChatResponseToOpenAi(const std::string& response_body, const std::string& fallback_model, const std::string& job_id) {
    const auto ollama = json::parse(response_body);
    json message = {
        {"role", "assistant"},
        {"content", ""}
    };

    if (ollama.contains("message") && ollama["message"].is_object()) {
        const auto& source_message = ollama["message"];
        message["role"] = source_message.value("role", "assistant");
        if (source_message.contains("content")) {
            message["content"] = source_message["content"];
        }
        if (source_message.contains("tool_calls") && source_message["tool_calls"].is_array()) {
            json tool_calls = json::array();
            int index = 0;
            for (const auto& source_tool : source_message["tool_calls"]) {
                json normalized_tool = source_tool;
                if (!normalized_tool.contains("id")) {
                    normalized_tool["id"] = "call_" + job_id + "_" + std::to_string(++index);
                }
                if (!normalized_tool.contains("type")) {
                    normalized_tool["type"] = "function";
                }
                if (normalized_tool.contains("function") && normalized_tool["function"].is_object()) {
                    auto& function = normalized_tool["function"];
                    if (function.contains("arguments") && !function["arguments"].is_string()) {
                        function["arguments"] = function["arguments"].dump();
                    } else if (!function.contains("arguments")) {
                        function["arguments"] = "{}";
                    }
                }
                tool_calls.push_back(std::move(normalized_tool));
            }
            if (!tool_calls.empty()) {
                message["tool_calls"] = std::move(tool_calls);
            }
        }
    } else if (ollama.contains("response")) {
        message["content"] = ollama["response"];
    }

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return json{
        {"id", "chatcmpl_" + job_id},
        {"object", "chat.completion"},
        {"created", now},
        {"model", ollama.value("model", fallback_model)},
        {"choices", json::array({
            {
                {"index", 0},
                {"message", message},
                {"finish_reason", message.contains("tool_calls") ? "tool_calls" : "stop"}
            }
        })}
    };
}

std::string OpenAiSseDeltaChunk(
    const std::string& model,
    const std::string& id,
    json delta,
    const std::string& finish_reason = {}) {
    json choice = {
        {"index", 0},
        {"delta", std::move(delta)}
    };
    if (!finish_reason.empty()) {
        choice["finish_reason"] = finish_reason;
    }

    json event = {
        {"id", id},
        {"object", "chat.completion.chunk"},
        {"created", static_cast<int64_t>(std::time(nullptr))},
        {"model", model},
        {"choices", json::array({choice})}
    };
    return "data: " + event.dump() + "\n\n";
}

std::string OpenAiSseChunk(
    const std::string& model,
    const std::string& id,
    const std::string& content,
    const std::string& finish_reason = {}) {
    json delta = json::object();
    if (!content.empty()) {
        delta["content"] = content;
    }
    return OpenAiSseDeltaChunk(model, id, std::move(delta), finish_reason);
}

bool WriteOllamaChatStreamAsOpenAiSse(
    const std::string& data,
    std::string& line_buffer,
    httplib::DataSink& sink,
    const std::string& fallback_model,
    const std::string& stream_id) {
    line_buffer.append(data);
    for (;;) {
        const size_t newline = line_buffer.find('\n');
        if (newline == std::string::npos) {
            break;
        }
        std::string line = Trim(line_buffer.substr(0, newline));
        line_buffer.erase(0, newline + 1);
        if (line.empty()) {
            continue;
        }
        try {
            const auto item = json::parse(line);
            std::string content;
            if (item.contains("message") && item["message"].is_object()) {
                content = item["message"].value("content", "");
            } else {
                content = item.value("response", "");
            }
            if (!content.empty()) {
                const std::string event = OpenAiSseChunk(item.value("model", fallback_model), stream_id, content);
                if (!sink.write(event.data(), event.size())) {
                    return false;
                }
            }
            if (item.contains("message") && item["message"].is_object()) {
                const auto& message = item["message"];
                if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                    json tool_call_deltas = json::array();
                    int index = 0;
                    for (const auto& tool_call : message["tool_calls"]) {
                        if (!tool_call.is_object()) {
                            ++index;
                            continue;
                        }
                        json delta = {
                            {"index", index},
                            {"id", tool_call.value("id", "call_" + stream_id + "_" + std::to_string(index))},
                            {"type", "function"},
                            {"function", json::object()}
                        };
                        if (tool_call.contains("function") && tool_call["function"].is_object()) {
                            const auto& function = tool_call["function"];
                            if (function.contains("name") && function["name"].is_string()) {
                                delta["function"]["name"] = function["name"].get<std::string>();
                            }
                            if (function.contains("arguments")) {
                                delta["function"]["arguments"] = function["arguments"].is_string()
                                    ? function["arguments"].get<std::string>()
                                    : function["arguments"].dump();
                            }
                        }
                        tool_call_deltas.push_back(std::move(delta));
                        ++index;
                    }
                    if (!tool_call_deltas.empty()) {
                        const std::string event = OpenAiSseDeltaChunk(
                            item.value("model", fallback_model),
                            stream_id,
                            json{{"tool_calls", std::move(tool_call_deltas)}});
                        if (!sink.write(event.data(), event.size())) {
                            return false;
                        }
                    }
                }
            }
        } catch (...) {
        }
    }
    return true;
}

BOOL WINAPI ConsoleControlHandler(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_console_stop_requested.store(true);
        if (g_active_server) {
            g_active_server->stop();
        }
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

int RunSetup(const fs::path& config_path) {
    std::string error;
    auto config = LoadRemoteWorkerConfig(config_path, &error);
    if (!config) {
        WriteLine(L"Could not load remote Ollama worker config: " + Utf8ToWide(error));
        return 2;
    }

    fs::path ollama_path;
    if (!EnsureOllamaInstalled(&ollama_path)) {
        return 3;
    }

    if (config->agent_server.shared_secret.empty()) {
        config->agent_server.shared_secret = GenerateRemoteWorkerSharedSecret();
        WriteLine(L"Generated a shared secret and saved it into the worker config.");
    }

    if (!config->agent_server.certificate_pem.empty() && !config->agent_server.private_key_pem.empty()) {
        if (config->agent_server.certificate_fingerprint.empty()) {
            config->agent_server.certificate_fingerprint =
                RemoteWorkerCertificateFingerprintFromPem(config->agent_server.certificate_pem, &error);
            if (config->agent_server.certificate_fingerprint.empty()) {
                WriteLine(L"Could not read embedded certificate fingerprint: " + Utf8ToWide(error));
                return 4;
            }
        }
        WriteLine(L"Using embedded HTTPS certificate material from the worker config.");
    } else if (fs::exists(config->agent_server.certificate_path) && fs::exists(config->agent_server.private_key_path)) {
        config->agent_server.certificate_pem = ReadTextFile(config->agent_server.certificate_path);
        config->agent_server.private_key_pem = ReadTextFile(config->agent_server.private_key_path);
        if (config->agent_server.certificate_pem.empty() || config->agent_server.private_key_pem.empty()) {
            WriteLine(L"Could not read certificate/key files for embedding into the worker config.");
            return 4;
        }
        config->agent_server.certificate_fingerprint =
            RemoteWorkerCertificateFingerprintFromPem(config->agent_server.certificate_pem, &error);
        if (config->agent_server.certificate_fingerprint.empty()) {
            WriteLine(L"Could not read certificate fingerprint: " + Utf8ToWide(error));
            return 4;
        }
        WriteLine(L"Embedded existing HTTPS certificate/key material into the worker config.");
    } else {
        RemoteWorkerCertificateMaterial material;
        if (!GenerateRemoteWorkerSelfSignedCertificateMaterial(&material, &error)) {
            WriteLine(L"Certificate generation failed: " + Utf8ToWide(error));
            return 4;
        }
        config->agent_server.certificate_pem = material.certificate_pem;
        config->agent_server.private_key_pem = material.private_key_pem;
        config->agent_server.certificate_fingerprint = material.fingerprint;
        WriteLine(L"Generated embedded HTTPS certificate/key material in the worker config.");
    }

    if (!PullOllamaModel(ollama_path, config->model.name)) {
        return 5;
    }

    if (!SaveRemoteWorkerConfig(*config, &error)) {
        WriteLine(L"Setup finished, but the updated worker config could not be saved: " + Utf8ToWide(error));
        return 6;
    }

    WriteLine(L"Remote Ollama worker setup complete.");
    WriteLine(L"Worker: " + Utf8ToWide(config->worker_name));
    WriteLine(L"HTTPS listener: " + Utf8ToWide(config->agent_server.bind_address) + L":" + std::to_wstring(config->agent_server.https_port));
    WriteLine(L"Ollama local ports: " + std::to_wstring(config->ollama.start_port) + L" through " + std::to_wstring(config->ollama.start_port + config->ollama.instance_count - 1));
    WriteLine(L"Ollama CPU threads per request: " + std::to_wstring(EffectiveOllamaNumThread(config->ollama)) +
        std::wstring(L" (") +
        (config->ollama.num_thread > 0 ? L"configured" : L"auto logical-threads/instances") +
        L").");
    WriteLine(L"Ollama accelerator mode: " + Utf8ToWide(NormalizeOllamaAccelerator(config->ollama.accelerator)) +
        L"; CPU thread budget: " + std::to_wstring(ClampCpuThreadPercent(config->ollama.cpu_thread_percent)) + L"%.");
    WriteLine(L"Run the worker with:");
    WriteLine(L"  agent --olama-remote " + QuoteArgument(config_path.wstring()));
    return 0;
}

int RunRemote(const fs::path& config_path) {
    std::string error;
    auto config = LoadRemoteWorkerConfig(config_path, &error);
    if (!config) {
        WriteLine(L"Could not load remote Ollama worker config: " + Utf8ToWide(error));
        return 2;
    }

    if (config->agent_server.shared_secret.empty()) {
        WriteLine(L"Remote worker config has no shared_secret. Use the Remote Ollama Setup window or run --olama-setup first.");
        return 3;
    }

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    WriteLine(L"HTTPS support is not compiled into this build. OpenSSL is required for remote Ollama workers.");
    return 4;
#else
    if (!config->agent_server.certificate_pem.empty() &&
        !config->agent_server.private_key_pem.empty() &&
        config->agent_server.certificate_fingerprint.empty()) {
        config->agent_server.certificate_fingerprint =
            RemoteWorkerCertificateFingerprintFromPem(config->agent_server.certificate_pem, &error);
        if (config->agent_server.certificate_fingerprint.empty()) {
            WriteLine(L"Could not read embedded certificate fingerprint: " + Utf8ToWide(error));
            return 4;
        }
    }

    const auto tls_files = ResolveRuntimeCertificateFiles(*config, &error);
    if (!tls_files) {
        WriteLine(L"HTTPS certificate/key are missing. Run --olama-setup first. Details: " + Utf8ToWide(error));
        return 4;
    }

    const auto ollama_path = FindExecutable(L"ollama.exe");
    if (!ollama_path) {
        WriteLine(L"ollama.exe was not found. Run --olama-setup first or install Ollama manually.");
        return 5;
    }

    std::vector<ManagedOllamaProcess> managed_processes;
    HANDLE managed_process_job = CreateKillOnCloseJobObject();
    std::vector<int> worker_ports;
    for (int i = 0; i < config->ollama.instance_count; ++i) {
        const int port = config->ollama.start_port + i;
        worker_ports.push_back(port);
        if (IsOllamaEndpointAvailable(port)) {
            WriteLine(L"Ollama is already responding on local port " + std::to_wstring(port) + L"; leaving that process alone.");
            continue;
        }

        ManagedOllamaProcess process;
        if (!StartOllamaServer(*ollama_path, port, config->ollama, &process)) {
            StopManagedProcesses(managed_processes);
            if (managed_process_job) {
                CloseHandle(managed_process_job);
            }
            return 6;
        }
        AssignManagedProcessToJob(managed_process_job, process);
        managed_processes.push_back(process);
    }

    RemoteOllamaQueue queue(worker_ports, *config);
    queue.Start();

    httplib::SSLServer server(
        tls_files->first.string().c_str(),
        tls_files->second.string().c_str());
    if (!server.is_valid()) {
        WriteLine(L"Could not create HTTPS worker server. Check the certificate and private key.");
        queue.Stop();
        StopManagedProcesses(managed_processes);
        if (managed_process_job) {
            CloseHandle(managed_process_job);
        }
        return 7;
    }

    server.set_read_timeout(std::chrono::hours(12));
    server.set_write_timeout(std::chrono::hours(12));
    server.set_keep_alive_timeout(static_cast<time_t>(3600));

    server.Get("/health", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        SetJson(res, json{
            {"worker_name", config->worker_name},
            {"model", config->model.name},
            {"purpose", config->model.purpose},
            {"agent_https_port", config->agent_server.https_port},
            {"ollama_start_port", config->ollama.start_port},
            {"ollama_instance_count", config->ollama.instance_count},
            {"ollama_accelerator", NormalizeOllamaAccelerator(config->ollama.accelerator)},
            {"ollama_cpu_thread_percent", ClampCpuThreadPercent(config->ollama.cpu_thread_percent)},
            {"ollama_num_thread", config->ollama.num_thread},
            {"ollama_effective_num_thread", EffectiveOllamaNumThread(config->ollama)},
            {"ollama_num_gpu", OllamaNumGpuOverride(config->ollama).value_or(-1)},
            {"ollama_num_parallel", config->ollama.num_parallel},
            {"ollama_max_loaded_models", config->ollama.max_loaded_models},
            {"ollama_max_queue", config->ollama.max_queue},
            {"ollama_context_length", config->ollama.context_length},
            {"ollama_keep_alive", config->ollama.keep_alive},
            {"ollama_flash_attention", config->ollama.flash_attention},
            {"queue", queue.QueueSnapshot()}
        });
    });

    server.Get("/queue", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        SetJson(res, queue.QueueSnapshot());
    });

    server.Post("/jobs", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }

        std::string endpoint = "/api/generate";
        std::string body = req.body;
        try {
            const auto payload = json::parse(req.body);
            endpoint = Trim(payload.value("endpoint", endpoint));
            if (payload.contains("payload")) {
                body = payload["payload"].dump();
            } else if (payload.contains("body") && payload["body"].is_string()) {
                body = payload["body"].get<std::string>();
            }
        } catch (...) {
        }

        std::string submit_error;
        auto job = queue.Submit(endpoint, body, &submit_error);
        if (!job) {
            SetJson(res, json{{"error", submit_error}}, 400);
            return;
        }
        SetJson(res, json{
            {"job_id", job->id},
            {"status", "queued"},
            {"queue_position", queue.QueuePosition(job->id)}
        }, 202);
    });

    server.Get(R"(/jobs/([A-Za-z0-9_\-]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        const std::string job_id = req.matches[1];
        auto snapshot = queue.SnapshotJob(job_id);
        if (!snapshot) {
            SetJson(res, json{{"error", "job not found"}}, 404);
            return;
        }
        SetJson(res, *snapshot);
    });

    auto sync_proxy = [&](const httplib::Request& req, httplib::Response& res, const std::string& endpoint) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }
        std::string submit_error;
        auto job = queue.Submit(endpoint, req.body, &submit_error);
        if (!job) {
            SetJson(res, json{{"error", submit_error}}, 400);
            return;
        }
        const int initial_position = queue.QueuePosition(job->id);
        std::unique_lock<std::mutex> lock(job->mutex);
        job->cv.wait(lock, [&]() {
            return job->status == "completed" || job->status == "failed";
        });
        res.status = job->response_status;
        res.set_header("X-Agent-Job-Id", job->id);
        res.set_header("X-Agent-Queue-Position", std::to_string(initial_position));
        res.set_header("X-Agent-Worker-Port", std::to_string(job->worker_port));
        res.set_content(job->response_body, "application/json");
    };

    server.Post("/api/generate", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }

        bool stream_response = false;
        try {
            const auto payload = json::parse(req.body);
            stream_response = payload.value("stream", false);
        } catch (...) {
        }

        if (stream_response) {
            const std::string body = ApplyOllamaRequestOptions("/api/generate", req.body, *config);
            const std::vector<int> stream_ports = worker_ports;
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "application/x-ndjson",
                [body, stream_ports](size_t offset, httplib::DataSink& sink) mutable {
                    if (offset > 0) {
                        sink.done();
                        return true;
                    }

                    auto lease = AcquireOllamaPortLease(stream_ports);
                    httplib::Client client("127.0.0.1", lease.port);
                    client.set_connection_timeout(10, 0);
                    client.set_read_timeout(kLongRunningStreamTimeoutSeconds, 0);
                    client.set_write_timeout(kLongRunningStreamTimeoutSeconds, 0);

                    auto response = client.Post(
                        "/api/generate",
                        httplib::Headers{},
                        body,
                        "application/json",
                        [&](const char* data, size_t data_length) {
                            return sink.write(data, data_length);
                        });

                    if (!response) {
                        const std::string event = json{
                            {"error", "Remote Agent worker could not reach local Ollama on port " + std::to_string(lease.port) + "."},
                            {"done", true}
                        }.dump() + "\n";
                        sink.write(event.data(), event.size());
                    } else if (response->status < 200 || response->status >= 300) {
                        const std::string event = json{
                            {"error", "Remote Ollama returned HTTP " + std::to_string(response->status) + "."},
                            {"done", true}
                        }.dump() + "\n";
                        sink.write(event.data(), event.size());
                    }
                    sink.done();
                    return true;
                });
            return;
        }

        sync_proxy(req, res, "/api/generate");
    });
    server.Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
        sync_proxy(req, res, "/api/chat");
    });
    server.Post("/api/embeddings", [&](const httplib::Request& req, httplib::Response& res) {
        sync_proxy(req, res, "/api/embeddings");
    });

    server.Post("/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
        if (!Authorized(req, config->agent_server.shared_secret)) {
            SetJson(res, json{{"error", "unauthorized"}}, 401);
            return;
        }

        bool stream_response = false;
        try {
            const auto payload = json::parse(req.body);
            stream_response = payload.value("stream", false);
        } catch (...) {
        }

        if (stream_response) {
            const std::string body = ApplyOllamaRequestOptions("/api/chat", req.body, *config);
            const std::string stream_id = NewJobId();
            const std::string fallback_model = config->model.name;
            const std::vector<int> stream_ports = worker_ports;
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            res.set_chunked_content_provider(
                "text/event-stream",
                [body, stream_id, fallback_model, stream_ports](size_t offset, httplib::DataSink& sink) mutable {
                    if (offset > 0) {
                        sink.done();
                        return true;
                    }

                    auto lease = AcquireOllamaPortLease(stream_ports);
                    httplib::Client client("127.0.0.1", lease.port);
                    client.set_connection_timeout(10, 0);
                    client.set_read_timeout(kLongRunningStreamTimeoutSeconds, 0);
                    client.set_write_timeout(kLongRunningStreamTimeoutSeconds, 0);

                    std::string line_buffer;
                    auto response = client.Post(
                        "/api/chat",
                        httplib::Headers{},
                        body,
                        "application/json",
                        [&](const char* data, size_t data_length) {
                            return WriteOllamaChatStreamAsOpenAiSse(
                                std::string(data, data_length),
                                line_buffer,
                                sink,
                                fallback_model,
                                stream_id);
                        });

                    if (!line_buffer.empty()) {
                        if (!WriteOllamaChatStreamAsOpenAiSse("\n", line_buffer, sink, fallback_model, stream_id)) {
                            sink.done();
                            return true;
                        }
                    }

                    if (!response) {
                        const std::string event = OpenAiSseChunk(
                            fallback_model,
                            stream_id,
                            "Remote Agent worker could not reach local Ollama on port " + std::to_string(lease.port) + ".",
                            "stop");
                        sink.write(event.data(), event.size());
                    } else if (response->status < 200 || response->status >= 300) {
                        const std::string event = OpenAiSseChunk(
                            fallback_model,
                            stream_id,
                            "Remote Ollama returned HTTP " + std::to_string(response->status) + ".",
                            "stop");
                        sink.write(event.data(), event.size());
                    } else {
                        const std::string finish = OpenAiSseChunk(fallback_model, stream_id, "", "stop");
                        sink.write(finish.data(), finish.size());
                    }
                    const std::string done = "data: [DONE]\n\n";
                    sink.write(done.data(), done.size());
                    sink.done();
                    return true;
                });
            return;
        }

        std::string submit_error;
        auto job = queue.Submit("/api/chat", req.body, &submit_error);
        if (!job) {
            SetJson(res, json{{"error", submit_error}}, 400);
            return;
        }

        std::unique_lock<std::mutex> lock(job->mutex);
        job->cv.wait(lock, [&]() {
            return job->status == "completed" || job->status == "failed";
        });

        if (job->status != "completed") {
            SetJson(res, json{{"error", job->error}, {"response_body", job->response_body}}, job->response_status);
            return;
        }

        try {
            SetJson(res, ConvertOllamaChatResponseToOpenAi(job->response_body, config->model.name, job->id));
        } catch (const std::exception& ex) {
            SetJson(res, json{{"error", std::string("Could not translate Ollama response: ") + ex.what()}, {"response_body", job->response_body}}, 502);
        }
    });

    g_console_stop_requested.store(false);
    g_active_server = &server;
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);

    WriteLine(L"Remote Ollama worker is ready.");
    WriteLine(L"Worker: " + Utf8ToWide(config->worker_name));
    WriteLine(L"HTTPS listener: " + Utf8ToWide(config->agent_server.bind_address) + L":" + std::to_wstring(config->agent_server.https_port));
    WriteLine(L"Model: " + Utf8ToWide(config->model.name) + L" (" + Utf8ToWide(config->model.purpose) + L")");
    WriteLine(L"Ollama is local-only on 127.0.0.1, ports " + std::to_wstring(config->ollama.start_port) + L" through " + std::to_wstring(config->ollama.start_port + config->ollama.instance_count - 1) + L".");
    WriteLine(L"Ollama CPU threads per request: " + std::to_wstring(EffectiveOllamaNumThread(config->ollama)) +
        std::wstring(L" (") +
        (config->ollama.num_thread > 0 ? L"configured" : L"auto logical-threads/instances") +
        L").");
    WriteLine(L"Ollama accelerator mode: " + Utf8ToWide(NormalizeOllamaAccelerator(config->ollama.accelerator)) +
        L"; CPU thread budget: " + std::to_wstring(ClampCpuThreadPercent(config->ollama.cpu_thread_percent)) + L"%.");
    WriteLine(L"Ollama parallel/env settings: OLLAMA_NUM_PARALLEL=" + std::to_wstring(config->ollama.num_parallel) +
        L", OLLAMA_MAX_LOADED_MODELS=" + std::to_wstring(config->ollama.max_loaded_models) +
        L", OLLAMA_MAX_QUEUE=" + std::to_wstring(config->ollama.max_queue) + L".");
    WriteLine(L"Queue endpoints: GET /health, GET /queue, POST /jobs, GET /jobs/{id}, POST /api/generate, POST /api/chat.");
    WriteLine(L"Press Ctrl+C to stop app-managed Ollama processes.");

    std::thread server_thread([&server, config]() {
        server.listen(config->agent_server.bind_address.c_str(), config->agent_server.https_port);
    });

    while (!g_console_stop_requested.load()) {
        Sleep(100);
        DWORD exit_code = 0;
        for (const auto& proc : managed_processes) {
            if (proc.process && GetExitCodeProcess(proc.process, &exit_code) && exit_code != STILL_ACTIVE) {
                WriteLine(L"A managed Ollama process exited unexpectedly.");
                g_console_stop_requested.store(true);
                break;
            }
        }
    }

    if (g_active_server) {
        g_active_server->stop();
    }
    if (server_thread.joinable()) {
        server_thread.join();
    }
    g_active_server = nullptr;
    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    queue.Stop();
    StopManagedProcesses(managed_processes);
    if (managed_process_job) {
        CloseHandle(managed_process_job);
    }
    return 0;
#endif
}

}  // namespace

std::string GenerateRemoteWorkerSharedSecret() {
    unsigned char buffer[32] = {};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer, sizeof(buffer), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return MakeId("remote_secret");
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : buffer) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

bool GenerateRemoteWorkerSelfSignedCertificateMaterial(
    RemoteWorkerCertificateMaterial* material,
    std::string* error) {
    if (!material) {
        if (error) {
            *error = "Certificate material output was not provided.";
        }
        return false;
    }
    *material = {};

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (error) {
        *error = "OpenSSL support is not compiled into this build.";
    }
    return false;
#else
    EVP_PKEY* raw_pkey = nullptr;
    EVP_PKEY_CTX* raw_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(raw_ctx, EVP_PKEY_CTX_free);
    if (!ctx) {
        if (error) {
            *error = "EVP_PKEY_CTX_new_id failed.";
        }
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) <= 0 ||
        EVP_PKEY_keygen(ctx.get(), &raw_pkey) <= 0) {
        if (error) {
            *error = "EVP_PKEY_keygen failed.";
        }
        return false;
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(raw_pkey, EVP_PKEY_free);

    std::unique_ptr<X509, decltype(&X509_free)> x509(X509_new(), X509_free);
    if (!x509) {
        if (error) {
            *error = "X509_new failed.";
        }
        return false;
    }

    X509_set_version(x509.get(), 2);
    ASN1_INTEGER_set(
        X509_get_serialNumber(x509.get()),
        static_cast<long>(std::chrono::system_clock::now().time_since_epoch().count() & 0x7fffffff));
    X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(x509.get()), 60LL * 60 * 24 * 365 * 3);
    X509_set_pubkey(x509.get(), pkey.get());

    std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)> name(X509_NAME_new(), X509_NAME_free);
    if (!name) {
        if (error) {
            *error = "X509_NAME_new failed.";
        }
        return false;
    }
    X509_NAME_add_entry_by_txt(name.get(), "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Agent Remote Worker"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("agent-remote-ollama-worker"), -1, -1, 0);
    X509_set_subject_name(x509.get(), name.get());
    X509_set_issuer_name(x509.get(), name.get());

    if (X509_sign(x509.get(), pkey.get(), EVP_sha256()) == 0) {
        if (error) {
            *error = "X509_sign failed.";
        }
        return false;
    }

    std::unique_ptr<BIO, decltype(&BIO_free)> cert_bio(BIO_new(BIO_s_mem()), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!cert_bio || !key_bio) {
        if (error) {
            *error = "Could not allocate certificate export buffers.";
        }
        return false;
    }

    if (PEM_write_bio_X509(cert_bio.get(), x509.get()) != 1) {
        if (error) {
            *error = "PEM_write_bio_X509 failed.";
        }
        return false;
    }
    if (PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        if (error) {
            *error = "PEM_write_bio_PrivateKey failed.";
        }
        return false;
    }

    material->certificate_pem = BioToString(cert_bio.get(), error, "certificate");
    if (material->certificate_pem.empty()) {
        return false;
    }
    material->private_key_pem = BioToString(key_bio.get(), error, "private key");
    if (material->private_key_pem.empty()) {
        return false;
    }
    material->fingerprint = CertificateFingerprintFromX509(x509.get(), error);
    return !material->fingerprint.empty();
#endif
}

bool GenerateRemoteWorkerSelfSignedCert(
    const fs::path& cert_path,
    const fs::path& key_path,
    std::string* fingerprint,
    std::string* error) {
    RemoteWorkerCertificateMaterial material;
    if (!GenerateRemoteWorkerSelfSignedCertificateMaterial(&material, error)) {
        return false;
    }
    if (!WriteTextFile(cert_path, material.certificate_pem, error) ||
        !WriteTextFile(key_path, material.private_key_pem, error)) {
        return false;
    }
    if (fingerprint) {
        *fingerprint = material.fingerprint;
    }
    return true;
}

std::string RemoteWorkerCertificateFingerprint(const fs::path& cert_path, std::string* error) {
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (error) {
        *error = "OpenSSL support is not compiled into this build.";
    }
    return {};
#else
    FILE* cert_file = nullptr;
    if (_wfopen_s(&cert_file, cert_path.wstring().c_str(), L"r") != 0 || !cert_file) {
        if (error) {
            *error = "Could not open certificate file.";
        }
        return {};
    }

    X509* x509 = PEM_read_X509(cert_file, nullptr, nullptr, nullptr);
    fclose(cert_file);
    if (!x509) {
        if (error) {
            *error = "Could not parse certificate PEM.";
        }
        return {};
    }

    std::string fingerprint = CertificateFingerprintFromX509(x509, error);
    X509_free(x509);
    return fingerprint;
#endif
}

int RunRemoteOllamaCommand(RemoteOllamaCommandMode mode, const fs::path& config_path) {
    EnsureConsoleAttached();
    if (config_path.empty()) {
        WriteLine(L"Missing remote Ollama worker JSON path.");
        WriteLine(L"Usage:");
        WriteLine(L"  agent --olama-setup remote_ollama_worker.json");
        WriteLine(L"  agent --olama-remote remote_ollama_worker.json");
        return 2;
    }

    if (mode == RemoteOllamaCommandMode::Setup) {
        return RunSetup(config_path);
    }
    return RunRemote(config_path);
}
