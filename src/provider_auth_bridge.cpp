#include "provider_auth_bridge.h"

#include "util.h"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace {
std::string Base64UrlDecode(std::string value) {
    std::replace(value.begin(), value.end(), '-', '+');
    std::replace(value.begin(), value.end(), '_', '/');
    while (value.size() % 4 != 0) {
        value.push_back('=');
    }

    constexpr unsigned char kInvalid = 0xFF;
    static unsigned char table[256];
    static bool initialized = false;
    if (!initialized) {
        std::fill(std::begin(table), std::end(table), kInvalid);
        for (int i = 0; i < 26; ++i) {
            table[static_cast<unsigned char>('A' + i)] = static_cast<unsigned char>(i);
            table[static_cast<unsigned char>('a' + i)] = static_cast<unsigned char>(26 + i);
        }
        for (int i = 0; i < 10; ++i) {
            table[static_cast<unsigned char>('0' + i)] = static_cast<unsigned char>(52 + i);
        }
        table[static_cast<unsigned char>('+')] = 62;
        table[static_cast<unsigned char>('/')] = 63;
        initialized = true;
    }

    std::string output;
    output.reserve((value.size() * 3) / 4);
    for (size_t i = 0; i < value.size(); i += 4) {
        unsigned char block[4] = {0, 0, 0, 0};
        int padding = 0;
        for (int j = 0; j < 4; ++j) {
            const char ch = i + static_cast<size_t>(j) < value.size() ? value[i + static_cast<size_t>(j)] : '=';
            if (ch == '=') {
                ++padding;
                block[j] = 0;
                continue;
            }
            const unsigned char decoded = table[static_cast<unsigned char>(ch)];
            if (decoded == kInvalid) {
                return {};
            }
            block[j] = decoded;
        }

        output.push_back(static_cast<char>((block[0] << 2) | (block[1] >> 4)));
        if (padding < 2) {
            output.push_back(static_cast<char>((block[1] << 4) | (block[2] >> 2)));
        }
        if (padding < 1) {
            output.push_back(static_cast<char>((block[2] << 6) | block[3]));
        }
    }
    return output;
}

json ParseJwtPayload(const std::string& token) {
    const size_t first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return json::object();
    }
    const size_t second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos || second_dot <= first_dot + 1) {
        return json::object();
    }
    const std::string payload = Base64UrlDecode(token.substr(first_dot + 1, second_dot - first_dot - 1));
    if (payload.empty()) {
        return json::object();
    }
    try {
        return json::parse(payload);
    } catch (...) {
        return json::object();
    }
}

std::wstring BuildCodexLoginCommand(const ProviderConfig& provider) {
    std::wstring command = L"codex login";
    if (provider.auth_mode == "device_code") {
        command += L" --device-auth";
    }
    command += L" -c cli_auth_credentials_store=file";
    return command;
}

bool WaitForProcessWithMessagePump(HANDLE process_handle) {
    while (true) {
        const DWORD wait_result = MsgWaitForMultipleObjects(1, &process_handle, FALSE, 100, QS_ALLINPUT);
        if (wait_result == WAIT_OBJECT_0) {
            return true;
        }
        if (wait_result == WAIT_FAILED) {
            return false;
        }
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

std::vector<wchar_t> BuildEnvironmentWithCodexHome(const std::filesystem::path& codex_home) {
    std::vector<wchar_t> block;
    LPWCH environment = GetEnvironmentStringsW();
    if (environment) {
        for (LPWCH cursor = environment; *cursor; ) {
            const size_t length = wcslen(cursor);
            block.insert(block.end(), cursor, cursor + length + 1);
            cursor += length + 1;
        }
        FreeEnvironmentStringsW(environment);
    }

    const std::wstring codex_home_key = L"CODEX_HOME=";
    for (size_t index = 0; index < block.size();) {
        const wchar_t* entry = block.data() + index;
        const size_t length = wcslen(entry);
        if (_wcsnicmp(entry, codex_home_key.c_str(), codex_home_key.size()) == 0) {
            block.erase(block.begin() + static_cast<std::ptrdiff_t>(index),
                        block.begin() + static_cast<std::ptrdiff_t>(index + length + 1));
            continue;
        }
        index += length + 1;
    }

    const std::wstring codex_home_entry = codex_home_key + codex_home.wstring();
    block.insert(block.end(), codex_home_entry.begin(), codex_home_entry.end());
    block.push_back(L'\0');
    block.push_back(L'\0');
    return block;
}

std::string JsonStringOrEmpty(const json& value, std::string_view key) {
    if (!value.is_object()) {
        return {};
    }
    const auto it = value.find(std::string(key));
    if (it == value.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}
}  // namespace

std::filesystem::path ProviderCodexBridgeHome(const AppStorage& storage, const std::string& provider_id) {
    return storage.ProviderAuthBridgeRoot() / Utf8ToWide(provider_id) / L"codex_home";
}

std::filesystem::path ProviderCodexBridgeAuthPath(const AppStorage& storage, const std::string& provider_id) {
    return ProviderCodexBridgeHome(storage, provider_id) / "auth.json";
}

std::filesystem::path ProviderCodexBridgeLoginLogPath(const AppStorage& storage, const std::string& provider_id) {
    return ProviderCodexBridgeHome(storage, provider_id) / "logs" / "codex-login.log";
}

bool RunProviderCodexLoginFlow(const AppStorage& storage, const ProviderConfig& provider, std::string* error) {
    if (provider.id.empty()) {
        if (error) {
            *error = "Provider ID is required before starting Codex login.";
        }
        return false;
    }

    const std::filesystem::path codex_home = ProviderCodexBridgeHome(storage, provider.id);
    std::error_code ec;
    std::filesystem::create_directories(codex_home, ec);
    if (ec) {
        if (error) {
            *error = "Could not create isolated Codex auth directory: " + ec.message();
        }
        return false;
    }

    std::wstring command_line = L"cmd.exe /c " + BuildCodexLoginCommand(provider);
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    std::vector<wchar_t> environment_block = BuildEnvironmentWithCodexHome(codex_home);
    const std::wstring working_directory = storage.startup_root().wstring();

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
            environment_block.data(),
            working_directory.c_str(),
            &startup_info,
            &process_info)) {
        if (error) {
            *error = "Could not launch the Codex login flow.";
        }
        return false;
    }

    CloseHandle(process_info.hThread);
    const bool wait_ok = WaitForProcessWithMessagePump(process_info.hProcess);
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);
    if (!wait_ok) {
        if (error) {
            *error = "The app could not wait for the Codex login process to finish.";
        }
        return false;
    }
    if (exit_code != 0) {
        if (error) {
            std::ostringstream stream;
            stream << "Codex login exited with code " << exit_code << ". Check "
                   << WideToUtf8(ProviderCodexBridgeLoginLogPath(storage, provider.id).wstring())
                   << " for details.";
            *error = stream.str();
        }
        return false;
    }
    return true;
}

std::optional<ProviderAuthRecord> ImportProviderAuthFromCodexHome(
    const std::filesystem::path& codex_home,
    const std::string& provider_id,
    const std::string& credential_id,
    std::string* error) {
    try {
        const std::filesystem::path auth_path = codex_home / "auth.json";
        std::ifstream input(auth_path, std::ios::binary);
        if (!input.is_open()) {
            if (error) {
                *error = "No Codex auth cache was found at " + WideToUtf8(auth_path.wstring()) + ".";
            }
            return std::nullopt;
        }

        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const json data = json::parse(text);

        ProviderAuthRecord record;
        record.credential_id = credential_id.empty() ? MakeId("provider_auth") : credential_id;
        record.provider_id = provider_id;
        record.auth_mode = JsonStringOrEmpty(data, "auth_mode");
        record.api_key = JsonStringOrEmpty(data, "OPENAI_API_KEY");
        record.token_type = "Bearer";
        record.last_refresh = JsonStringOrEmpty(data, "last_refresh");

        if (data.contains("tokens") && data["tokens"].is_object()) {
            const json& tokens = data["tokens"];
            record.id_token = JsonStringOrEmpty(tokens, "id_token");
            record.access_token = JsonStringOrEmpty(tokens, "access_token");
            record.refresh_token = JsonStringOrEmpty(tokens, "refresh_token");
            record.account_id = JsonStringOrEmpty(tokens, "account_id");

            const json id_claims = ParseJwtPayload(record.id_token);
            if (id_claims.is_object()) {
                record.account_email = JsonStringOrEmpty(id_claims, "email");
                record.account_display_name = JsonStringOrEmpty(id_claims, "name");
                if (record.account_display_name.empty()) {
                    record.account_display_name = JsonStringOrEmpty(id_claims, "preferred_username");
                }
                record.scope = JsonStringOrEmpty(id_claims, "scope");
                const auto exp_it = id_claims.find("exp");
                if (exp_it != id_claims.end() && exp_it->is_number_integer()) {
                    record.expires_at = std::to_string(exp_it->get<long long>());
                }
            }
        }

        if (record.account_display_name.empty()) {
            record.account_display_name = JsonStringOrEmpty(data, "auth_mode") == "apikey"
                ? "API Key Login"
                : (!record.account_email.empty() ? record.account_email : record.account_id);
        }

        if (record.api_key.empty() && record.access_token.empty()) {
            if (error) {
                *error = "The Codex auth cache did not contain an API key or access token.";
            }
            return std::nullopt;
        }
        return record;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return std::nullopt;
    }
}

bool ClearProviderCodexBridge(const AppStorage& storage, const std::string& provider_id, std::string* error) {
    if (provider_id.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::remove_all(storage.ProviderAuthBridgeRoot() / Utf8ToWide(provider_id), ec);
    if (ec && error) {
        *error = "Could not clear the provider's Codex auth cache: " + ec.message();
    }
    return !ec;
}
