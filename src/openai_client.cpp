#include "openai_client.h"

#include "ollama_api_client.h"
#include "provider_profiles.h"

#include <windows.h>
#include <winhttp.h>

#include "util.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace {
bool IsOllamaLocalProvider(const ProviderConfig& provider) {
    return NormalizeProviderType(provider.provider_type) == "ollama_local";
}
} // namespace

// Static provider cache for compression model calls
static std::vector<ProviderConfig> s_provider_cache;

namespace {
struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using UniqueInternetHandle = std::unique_ptr<void, InternetHandleCloser>;

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

std::string FormatWinHttpError(const std::string& prefix, DWORD error_code) {
    std::ostringstream stream;
    stream << prefix;
    if (error_code != ERROR_SUCCESS) {
        stream << " (WinHTTP error " << error_code << ")";
    }
    return stream.str();
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

json BuildRequestBody(const ChatRequestOptions& request, bool stream, const std::vector<ChatToolDefinition>& tools = {}) {
    json body;
    body["model"] = request.model.id;
    body["temperature"] = request.temperature;
    body["max_tokens"] = request.max_tokens;
    body["stream"] = stream;
    body["messages"] = json::array();

    if (!request.system_prompt.empty()) {
        body["messages"].push_back({
            {"role", "system"},
            {"content", request.system_prompt},
        });
    }

    for (const auto& message : request.messages) {
        json payload{
            {"role", message.role},
        };
        if (!message.content.empty() || message.role != "assistant") {
            payload["content"] = message.content;
        }
        if (!message.name.empty()) {
            payload["name"] = message.name;
        }
        if (!message.tool_call_id.empty()) {
            payload["tool_call_id"] = message.tool_call_id;
        }
        if (!message.tool_calls_json.empty()) {
            try {
                payload["tool_calls"] = NormalizeToolCallsForProvider(json::parse(message.tool_calls_json));
            } catch (...) {
            }
        }
        body["messages"].push_back(std::move(payload));
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
    } catch (...) {
    }
    return body;
}

std::string ReadEntireResponse(HINTERNET request) {
    std::string response;

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            break;
        }
        if (available == 0) {
            break;
        }

        std::vector<char> bytes(static_cast<size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data(), available, &read)) {
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

ChatExecutionResult RunRequest(const ChatRequestOptions& request, bool stream, const std::function<void(const std::string&)>& on_delta) {
    const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
    AppendDetail("RunRequest: POST " + url);
    AppendDetail("  stream=" + std::to_string(stream) + " provider=" + request.provider.name + " model=" + request.model.id);
    const ParsedUrl parsed = CrackUrl(url);
    const std::string body = BuildRequestBody(request, stream).dump();
    AppendDetail("  body: " + body);
    AppendDetail("  parsed host=" + WideToUtf8(parsed.host) + " port=" + std::to_string(parsed.port) + " path=" + WideToUtf8(parsed.path));
    constexpr int kMaxAttempts = 4;
    std::string last_error;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        AppendDetail("  attempt " + std::to_string(attempt) + "/" + std::to_string(kMaxAttempts));
        ChatExecutionResult result;

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

        WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);
        ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

        std::wstring headers = L"Content-Type: application/json\r\nAccept: ";
        headers += stream ? L"text/event-stream" : L"application/json";
        headers += L"\r\n";
        if (!request.provider.api_key.empty()) {
            headers += L"Authorization: Bearer ";
            headers += Utf8ToWide(request.provider.api_key);
            headers += L"\r\n";
        }

        if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
            last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
            AppendDetail("  FAIL attempt " + std::to_string(attempt) + ": WinHttpSendRequest: " + last_error);
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
            last_error = FormatWinHttpError("Failed to receive HTTP response.", GetLastError());
            AppendDetail("  FAIL attempt " + std::to_string(attempt) + ": WinHttpReceiveResponse: " + last_error);
            if (attempt < kMaxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                continue;
            }
            result.error = last_error;
            return result;
        }

        const DWORD status_code = QueryStatusCode(static_cast<HINTERNET>(request_handle.get()));
        AppendDetail("  attempt " + std::to_string(attempt) + " HTTP status=" + std::to_string(status_code));
        if (status_code < 200 || status_code >= 300) {
            const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
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
            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            AppendDetail("  response length=" + std::to_string(response.size()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    if (choice.contains("message")) {
                        result.full_text = ExtractContentString(choice["message"].value("content", json{}));
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

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                result.error = FormatWinHttpError("Failed while streaming response.", GetLastError());
                return result;
            }
            if (available == 0) {
                break;
            }

            std::vector<char> bytes(static_cast<size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                result.error = FormatWinHttpError("Failed to read a streaming response chunk.", GetLastError());
                return result;
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
                    if (choice.contains("delta")) {
                        const auto& delta = choice["delta"];
                        if (delta.contains("content")) {
                            const std::string piece = ExtractContentString(delta["content"]);
                            if (!piece.empty()) {
                                result.full_text += piece;
                                on_delta(piece);
                            }
                        }
                    }
                } catch (...) {
                    result.error = "Failed to parse a streaming SSE chunk.";
                    return result;
                }
            }
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

        const std::string body_str = body.dump();
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
            } else {
                detail += "  IsOllamaLocalProvider=false, calling TestOpenAICompatibleEmbeddingConnection\r\n";
                result.success = TestOpenAICompatibleEmbeddingConnection(provider, model, &result.message);
            }
            detail += "  result success=" + std::to_string(result.success) + " message=" + result.message + "\r\n";
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

        detail += "  standard OpenAI-compatible non-embedding path\r\n";
        ChatRequestOptions request;
        request.provider = provider;
        request.model = model;
        request.temperature = 0.0;
        request.max_tokens = 8;
        request.messages.push_back(MessageRecord{"user", "Reply with the single word pong.", CurrentTimestampUtc()});
        detail += "  sending ping\r\n";
        const ChatExecutionResult response = RunRequest(request, false, [](const std::string&) {});
        result.success = response.success;
        result.message = response.success ? response.full_text : response.error;
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
                                          const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
                                          const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    try {
        if (IsOllamaLocalProvider(request.provider)) {
            return RunOllamaLocalHttpChat(request, on_delta, {}, {});
        }
        return RunRequest(request, true, on_delta);
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
                                                           const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpToolPrompt(request, tools, {}, {}, {});
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = BuildRequestBody(request, false, tools).dump();
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

            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
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
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    result.finish_reason = choice.value("finish_reason", "");
                    if (choice.contains("message")) {
                        json raw_message = choice["message"];
                        result.assistant_text = ExtractContentString(raw_message.value("content", json{}));
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
                                                        const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
                                                        const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpCompletion(request, {}, {});
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = BuildRequestBody(request, false, {}).dump();
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

            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
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
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
                const std::string details = ExtractErrorMessage(error_body);
                last_error = FormatHttpErrorMessage(status_code, details, attempt);
                if (IsRetryableStatusCode(status_code) && attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, QueryRetryAfterSeconds(static_cast<HINTERNET>(request_handle.get())))));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            const std::string response = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
            try {
                const auto payload = json::parse(response);
                result.success = true;
                if (payload.contains("choices") && payload["choices"].is_array() && !payload["choices"].empty()) {
                    const auto& choice = payload["choices"][0];
                    result.finish_reason = choice.value("finish_reason", "");
                    if (choice.contains("message")) {
                        const json& msg_json = choice["message"];
                        result.message.role = msg_json.value("role", "assistant");
                        result.message.content = ExtractContentString(msg_json.value("content", json{}));
                        result.assistant_text = result.message.content;
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
                                                           const std::function<void(const ProviderQueueStatus&)>& /*on_queue_status*/,
                                                           const std::function<void(const std::string&, const std::string&)>& /*on_activity_status*/) {
    if (IsOllamaLocalProvider(request.provider)) {
        return RunOllamaLocalHttpToolPrompt(request, tools, on_delta, {}, {});
    }

    ChatCompletionResult result;

    try {
        const std::string url = JoinChatCompletionsUrl(request.provider.base_url);
        const ParsedUrl parsed = CrackUrl(url);
        const std::string body = BuildRequestBody(request, true, tools).dump();
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

            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 15000, 15000, 30000, 30000);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);

            std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
            if (!request.provider.api_key.empty()) {
                headers += L"Authorization: Bearer ";
                headers += Utf8ToWide(request.provider.api_key);
                headers += L"\r\n";
            }

            if (!WinHttpSendRequest(static_cast<HINTERNET>(request_handle.get()), headers.c_str(), static_cast<DWORD>(headers.size()), reinterpret_cast<LPVOID>(const_cast<char*>(body.data())), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
                last_error = FormatWinHttpError("Failed to send HTTP request.", GetLastError());
                if (attempt < kMaxAttempts) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ComputeRetryDelayMs(attempt - 1, std::nullopt)));
                    continue;
                }
                result.error = last_error;
                return result;
            }

            if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request_handle.get()), nullptr)) {
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
                const std::string error_body = ReadEntireResponse(static_cast<HINTERNET>(request_handle.get()));
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
            std::vector<ChatToolCall> streamed_tool_calls;

            while (true) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request_handle.get()), &available)) {
                    result.error = FormatWinHttpError("Failed while streaming tool-aware response.", GetLastError());
                    return result;
                }
                if (available == 0) {
                    break;
                }

                std::vector<char> bytes(static_cast<size_t>(available));
                DWORD read = 0;
                if (!WinHttpReadData(static_cast<HINTERNET>(request_handle.get()), bytes.data(), available, &read)) {
                    result.error = FormatWinHttpError("Failed to read a streaming tool-aware response chunk.", GetLastError());
                    return result;
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

                    std::string payload = Trim(line.substr(5));
                    if (payload == "[DONE]") {
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
                        if (!result.assistant_text.empty()) {
                            raw_message["content"] = result.assistant_text;
                        }
                        if (!result.tool_calls.empty()) {
                            raw_message["tool_calls"] = SerializeToolCallsForProvider(result.tool_calls);
                        }
                        result.raw_message_json = raw_message.dump(2);
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
                        if (delta.contains("content")) {
                            const std::string piece = ExtractContentString(delta["content"]);
                            if (!piece.empty()) {
                                result.assistant_text += piece;
                                on_delta(piece);
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

            result.success = true;
            result.tool_calls.clear();
            for (auto tool_call : streamed_tool_calls) {
                if (!tool_call.name.empty()) {
                    NormalizeToolCall(tool_call);
                    result.tool_calls.push_back(std::move(tool_call));
                }
            }
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

void OpenAIClient::SetStorage(AppStorage* /*storage*/) {
    // No-op for now
}
