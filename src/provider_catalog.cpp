#include "provider_catalog.h"

#include "ollama_cli_bridge.h"
#include "provider_profiles.h"
#include "util.h"

#include <nlohmann/json.hpp>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace {
struct ProviderHttpResponse {
    DWORD status = 0;
    std::string body;
};

struct ProviderParsedUrl {
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring host;
    std::wstring path;
};

ProviderParsedUrl CrackProviderUrl(const std::string& url_utf8) {
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
        throw std::runtime_error("Invalid URL.");
    }

    ProviderParsedUrl parsed;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.port = components.nPort;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    return parsed;
}

std::string JoinUrlPath(std::string base_url, const std::string& path) {
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    return base_url + path;
}

std::string LowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string ReadProviderResponse(HINTERNET request) {
    std::string response;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
            break;
        }
        std::vector<char> buffer(static_cast<size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read)) {
            break;
        }
        response.append(buffer.data(), buffer.data() + read);
    }
    return response;
}

ProviderHttpResponse SendProviderRequest(const ProviderConfig& provider,
                                        const std::string& method,
                                        const std::string& url,
                                        const std::string& body = {},
                                        const std::string& content_type = "application/json") {
    const auto parsed = CrackProviderUrl(url);
    struct HandleCloser {
        void operator()(void* handle) const {
            if (handle) {
                WinHttpCloseHandle(static_cast<HINTERNET>(handle));
            }
        }
    };
    using Handle = std::unique_ptr<void, HandleCloser>;

    Handle session(WinHttpOpen(L"AgentProviderCatalog/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        throw std::runtime_error("Could not open WinHTTP session.");
    }
    Handle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
    if (!connection) {
        throw std::runtime_error("Could not connect to provider endpoint.");
    }
    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), Utf8ToWide(method).c_str(), parsed.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        throw std::runtime_error("Could not create provider request.");
    }

    if (parsed.secure && !provider.tls_certificate_fingerprint.empty()) {
        DWORD security_flags =
            SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(static_cast<HINTERNET>(request.get()), WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));
    }
    WinHttpSetTimeouts(static_cast<HINTERNET>(request.get()), 10000, 10000, 15000, 15000);
    std::wstring headers = L"Accept: application/json\r\n";
    if (!body.empty() || LowerAsciiCopy(method) == "post") {
        headers += L"Content-Type: " + Utf8ToWide(content_type) + L"\r\n";
    }
    if (!provider.api_key.empty()) {
        headers += L"Authorization: Bearer " + Utf8ToWide(provider.api_key) + L"\r\n";
    }
    if (!WinHttpSendRequest(static_cast<HINTERNET>(request.get()), headers.c_str(),
            static_cast<DWORD>(headers.size()),
            body.empty() ? nullptr : reinterpret_cast<LPVOID>(const_cast<char*>(body.data())),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0)) {
        throw std::runtime_error("Could not send provider request.");
    }
    if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        throw std::runtime_error("Could not receive provider response.");
    }

    ProviderHttpResponse response;
    DWORD status_size = sizeof(response.status);
    WinHttpQueryHeaders(static_cast<HINTERNET>(request.get()),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &response.status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);
    response.body = ReadProviderResponse(static_cast<HINTERNET>(request.get()));
    return response;
}

std::optional<std::pair<std::string, std::string>> ParseAlignedKeyValue(const std::string& line) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    for (size_t i = 0; i + 1 < trimmed.size(); ++i) {
        if (trimmed[i] != ' ' || trimmed[i + 1] != ' ') {
            continue;
        }
        size_t split = i;
        while (split < trimmed.size() && trimmed[split] == ' ') {
            ++split;
        }
        const std::string key = Trim(trimmed.substr(0, i));
        const std::string value = Trim(trimmed.substr(split));
        if (!key.empty() && !value.empty()) {
            return std::make_pair(key, value);
        }
    }
    return std::nullopt;
}

std::optional<int> ParseOllamaPositiveInt(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), ','), value.end());
    const auto parsed = ParseInt(Utf8ToWide(Trim(value)));
    if (!parsed || *parsed <= 0) {
        return std::nullopt;
    }
    return *parsed;
}

bool IsOllamaContextLengthKey(const std::string& key) {
    return key == "context length" ||
           key == "context window";
}

bool IsOllamaMaxOutputTokensKey(const std::string& key) {
    return key == "max output tokens" ||
           key == "maximum output tokens" ||
           key == "output tokens" ||
           key == "output token limit";
}

bool JsonCapabilitiesContain(const json& item, const std::vector<std::string>& names) {
    auto matches = [&](std::string value) {
        value = LowerAsciiCopy(Trim(value));
        return std::find(names.begin(), names.end(), value) != names.end();
    };
    if (item.contains("capabilities") && item["capabilities"].is_array()) {
        for (const auto& capability : item["capabilities"]) {
            if (capability.is_string() && matches(capability.get<std::string>())) {
                return true;
            }
        }
    }
    if (item.contains("type") && item["type"].is_string() && matches(item["type"].get<std::string>())) {
        return true;
    }
    return false;
}
}  // namespace

std::vector<ModelConfig> LoadProviderCatalog(AppStorage* storage, const ProviderConfig& provider, std::string* error) {
    const std::string normalized_type = NormalizeProviderType(provider.provider_type);
    if (normalized_type == "openai_codex_oauth") {
        if (!storage) {
            if (error) {
                *error = "Storage is not available for bundled model catalog lookup.";
            }
            return {};
        }
        return storage->LoadProviderManifestModels(normalized_type);
    }

    if (normalized_type == "agent_https") {
        try {
            const ProviderHttpResponse response = SendProviderRequest(provider, "GET", JoinUrlPath(provider.base_url, "/v1/models"));
            if (response.status < 200 || response.status >= 300) {
                std::ostringstream stream;
                stream << "Remote worker returned HTTP " << response.status;
                if (!response.body.empty()) {
                    stream << ": " << response.body;
                }
                throw std::runtime_error(stream.str());
            }

            const auto data = json::parse(response.body);
            std::vector<ModelConfig> models;
            if (data.contains("data") && data["data"].is_array()) {
                for (const auto& item : data["data"]) {
                    if (!item.is_object()) {
                        continue;
                    }
                    ModelConfig model;
                    model.id = item.value("id", "");
                    if (model.id.empty()) {
                        continue;
                    }
                    model.display_name = item.value("display_name", model.id);
                    model.supports_streaming = item.value("supports_streaming", true);
                    model.supports_tools = item.value("supports_tools", false);
                    model.supports_vision = item.value("supports_vision", false);
                    model.supports_embedding = item.value("supports_embedding", false) || JsonCapabilitiesContain(item, {"embedding", "embeddings"});
                    model.supports_thinking = item.value("supports_thinking", false) || JsonCapabilitiesContain(item, {"thinking", "reasoning"});
                    model.max_active_requests = std::max(0, item.value("max_active_requests", 0));
                    model.max_queue_size = std::max(0, item.value("max_queue_size", 0));
                    model.self_managed_queue = item.value("self_managed_queue", false);
                    model.output_tokens_parameter = "auto";
                    model.catalog_source = "discovered";
                    models.push_back(std::move(model));
                }
            }
            return models;
        } catch (const std::exception& ex) {
            if (error) {
                *error = ex.what();
            }
            return {};
        }
    }

    if (normalized_type == "lmstudio_local") {
        try {
            const ProviderHttpResponse response = SendProviderRequest(provider, "GET", JoinUrlPath(provider.base_url, "/models"));
            if (response.status < 200 || response.status >= 300) {
                std::ostringstream stream;
                stream << "Provider returned HTTP " << response.status;
                if (!response.body.empty()) {
                    stream << ": " << response.body;
                }
                throw std::runtime_error(stream.str());
            }

            const auto data = json::parse(response.body);
            std::vector<ModelConfig> models;
            if (data.contains("data") && data["data"].is_array()) {
                for (const auto& item : data["data"]) {
                    if (!item.is_object()) {
                        continue;
                    }
                    ModelConfig model;
                    model.id = item.value("id", "");
                    if (model.id.empty()) {
                        continue;
                    }
                    model.display_name = item.value("display_name", model.id);
                    model.supports_streaming = item.value("supports_streaming", true);
                    model.supports_tools = item.value("supports_tools", false);
                    model.supports_vision = item.value("supports_vision", false);
                    model.supports_embedding = item.value("supports_embedding", false) || JsonCapabilitiesContain(item, {"embedding", "embeddings"});
                    model.supports_thinking = item.value("supports_thinking", false) || JsonCapabilitiesContain(item, {"thinking", "reasoning"});
                    model.output_tokens_parameter = "auto";
                    model.catalog_source = "discovered";
                    models.push_back(std::move(model));
                }
            }
            return models;
        } catch (const std::exception& ex) {
            if (error) {
                *error = ex.what();
            }
            return {};
        }
    }

    if (error) {
        *error = "This provider type does not expose a managed model catalog.";
    }
    return {};
}

bool LoadOllamaModelMetadata(const ProviderConfig& provider,
                             const std::string& model_id,
                             ModelConfig* model,
                             std::string* error) {
    const std::string provider_type = NormalizeProviderType(provider.provider_type);
    if (provider_type != "ollama_local" && provider_type != "ollama_cloud") {
        if (error) {
            *error = "This provider is not an Ollama provider.";
        }
        return false;
    }
    if (Trim(model_id).empty()) {
        if (error) {
            *error = "Model ID is required before Ollama model info can be loaded.";
        }
        return false;
    }

    try {
        OllamaCliCommandOutput command;
        std::string command_error;
        if (!RunOllamaCliCommand(
                {L"show", Utf8ToWide(model_id)},
                {},
                OllamaCliLaunchOptions{},
                &command,
                &command_error)) {
            throw std::runtime_error(command_error.empty() ? "Could not launch the Ollama CLI." : command_error);
        }
        if (command.exit_code != 0) {
            const std::string details = Trim(!command.stderr_text.empty() ? command.stderr_text : command.stdout_text);
            throw std::runtime_error(details.empty()
                ? "The Ollama CLI show command failed."
                : details);
        }

        const std::string text = command.stdout_text;
        if (Trim(text).empty()) {
            throw std::runtime_error("The Ollama CLI did not return model information.");
        }

        ModelConfig resolved = model ? *model : ModelConfig{};
        resolved.id = model_id;
        if (resolved.display_name.empty()) {
            resolved.display_name = model_id;
        }
        resolved.supports_streaming = false;
        resolved.supports_tools = false;
        resolved.supports_vision = false;
        resolved.supports_embedding = false;
        resolved.supports_thinking = false;
        bool found_any_capability = false;
        resolved.reasoning_efforts.clear();

        std::istringstream stream(text);
        std::string line;
        std::string section;
        while (std::getline(stream, line)) {
            const std::string trimmed = Trim(line);
            if (trimmed.empty()) {
                continue;
            }

            if (trimmed == "Model" ||
                trimmed == "Capabilities" ||
                trimmed == "Parameters" ||
                trimmed == "Metadata" ||
                trimmed == "License" ||
                trimmed == "Tensors") {
                section = trimmed;
                continue;
            }

            if (section == "Model" || section == "Metadata") {
                const auto key_value = ParseAlignedKeyValue(line);
                if (!key_value) {
                    continue;
                }
                const std::string key = LowerAsciiCopy(key_value->first);
                if (IsOllamaContextLengthKey(key)) {
                    if (const auto parsed = ParseOllamaPositiveInt(key_value->second)) {
                        resolved.context_window = *parsed;
                    }
                } else if (IsOllamaMaxOutputTokensKey(key)) {
                    if (const auto parsed = ParseOllamaPositiveInt(key_value->second)) {
                        resolved.max_output_tokens = *parsed;
                    }
                }
                continue;
            }

            if (section == "Capabilities") {
                const std::string capability = LowerAsciiCopy(trimmed);
                if (capability == "tools") {
                    resolved.supports_tools = true;
                    found_any_capability = true;
                } else if (capability == "vision") {
                    resolved.supports_vision = true;
                    found_any_capability = true;
                } else if (capability == "embedding" || capability == "embeddings") {
                    resolved.supports_embedding = true;
                } else if (capability == "thinking") {
                    resolved.supports_thinking = true;
                    found_any_capability = true;
                    resolved.reasoning_efforts = {"none", "low", "medium", "high"};
                }
            }
        }

        // Only mark as streaming-capable if the model has at least one
        // chat-like capability. Embedding-only models are not streaming-capable.
        resolved.supports_streaming = found_any_capability;

        if (model) {
            *model = std::move(resolved);
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
}

void SyncProviderModelsFromCatalog(ProviderConfig* provider, const std::vector<ModelConfig>& catalog_models) {
    if (!provider) {
        return;
    }

    std::vector<ModelConfig> merged;
    merged.reserve(catalog_models.size());
    for (auto model : catalog_models) {
        const auto existing = std::find_if(
            provider->models.begin(),
            provider->models.end(),
            [&](const ModelConfig& current) { return current.id == model.id; });
        if (existing != provider->models.end()) {
            if (!existing->display_name.empty()) {
                model.display_name = existing->display_name;
            }
            if (existing->max_active_requests > 0) {
                model.max_active_requests = existing->max_active_requests;
            }
            if (existing->max_queue_size > 0) {
                model.max_queue_size = existing->max_queue_size;
            }
            model.self_managed_queue = existing->self_managed_queue;
        }
        merged.push_back(std::move(model));
    }

    for (const auto& model : provider->models) {
        if (model.catalog_source == "manual") {
            const auto duplicate = std::find_if(
                merged.begin(),
                merged.end(),
                [&](const ModelConfig& current) { return current.id == model.id; });
            if (duplicate == merged.end()) {
                merged.push_back(model);
            }
        }
    }
    provider->models = std::move(merged);
}
