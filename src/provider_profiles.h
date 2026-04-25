#pragma once

#include "types.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <string>

enum class OutputTokensMode {
    Auto,
    MaxTokens,
    MaxCompletionTokens,
};

struct ProviderRequestProfile {
    std::string provider_type = "openai_compatible";
    std::string auth_mode;
    std::string chat_completions_path = "/chat/completions";
    std::string model_catalog_mode = "manual";
    OutputTokensMode output_tokens_mode = OutputTokensMode::Auto;
    bool attach_bearer_authorization = false;
    bool allow_output_tokens_retry = false;
    bool require_tls_fingerprint_validation = false;
};

inline std::string NormalizeProviderType(std::string type) {
    type = Trim(type);
    std::transform(type.begin(), type.end(), type.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (type == "agent" || type == "remote_agent" || type == "agent_https") {
        return "agent_https";
    }
    if (type == "openai_oauth" || type == "openai_codex_oauth" || type == "chatgpt_oauth") {
        return "openai_codex_oauth";
    }
    if (type == "lmstudio" || type == "lm_studio" || type == "lmstudio_local") {
        return "lmstudio_local";
    }
    if (type == "ollama" || type == "ollama_local" || type == "ollama-native") {
        return "ollama_local";
    }
    if (type == "binding" || type == "binding_provider" || type == "provider_binding") {
        return "binding_provider";
    }
    return "openai_compatible";
}

inline std::string NormalizeOutputTokensParameter(std::string value) {
    value = Trim(value);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "max_tokens") {
        return "max_tokens";
    }
    if (value == "max_completion_tokens" || value == "max-completion-tokens") {
        return "max_completion_tokens";
    }
    return "auto";
}

inline OutputTokensMode ResolveOutputTokensMode(const ModelConfig& model, bool force_max_completion_tokens = false) {
    if (force_max_completion_tokens) {
        return OutputTokensMode::MaxCompletionTokens;
    }
    const std::string normalized = NormalizeOutputTokensParameter(model.output_tokens_parameter);
    if (normalized == "max_tokens") {
        return OutputTokensMode::MaxTokens;
    }
    if (normalized == "max_completion_tokens") {
        return OutputTokensMode::MaxCompletionTokens;
    }
    if (model.prefer_max_completion_tokens) {
        return OutputTokensMode::MaxCompletionTokens;
    }
    return OutputTokensMode::Auto;
}

inline ProviderRequestProfile ResolveProviderRequestProfile(const ProviderConfig& provider,
                                                           const ModelConfig& model,
                                                           bool force_max_completion_tokens = false) {
    ProviderRequestProfile profile;
    profile.provider_type = NormalizeProviderType(provider.provider_type);
    profile.auth_mode = provider.auth_mode.empty() ? "api_key" : provider.auth_mode;
    profile.model_catalog_mode = provider.model_catalog_mode.empty() ? "manual" : provider.model_catalog_mode;
    profile.output_tokens_mode = ResolveOutputTokensMode(model, force_max_completion_tokens);
    profile.attach_bearer_authorization = !provider.api_key.empty();
    profile.allow_output_tokens_retry =
        profile.provider_type == "openai_compatible" ||
        profile.provider_type == "openai_codex_oauth" ||
        profile.provider_type == "lmstudio_local";
    profile.require_tls_fingerprint_validation =
        profile.provider_type == "agent_https" &&
        !Trim(provider.tls_certificate_fingerprint).empty();

    if (profile.provider_type == "openai_codex_oauth") {
        profile.model_catalog_mode = provider.model_catalog_mode.empty() ? "bundled" : provider.model_catalog_mode;
    } else if (profile.provider_type == "lmstudio_local") {
        profile.model_catalog_mode = provider.model_catalog_mode.empty() ? "remote_endpoint" : provider.model_catalog_mode;
    } else if (profile.provider_type == "ollama_local") {
        profile.chat_completions_path = "/api/chat";
        profile.model_catalog_mode = provider.model_catalog_mode.empty() ? "manual" : provider.model_catalog_mode;
    }

    return profile;
}
