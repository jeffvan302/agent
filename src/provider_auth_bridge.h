#pragma once

#include "storage.h"
#include "types.h"

#include <filesystem>
#include <optional>
#include <string>

std::filesystem::path ProviderCodexBridgeHome(const AppStorage& storage, const std::string& provider_id);
std::filesystem::path ProviderCodexBridgeAuthPath(const AppStorage& storage, const std::string& provider_id);
std::filesystem::path ProviderCodexBridgeLoginLogPath(const AppStorage& storage, const std::string& provider_id);

bool RunProviderCodexLoginFlow(const AppStorage& storage, const ProviderConfig& provider, std::string* error);
std::optional<ProviderAuthRecord> ImportProviderAuthFromCodexHome(
    const std::filesystem::path& codex_home,
    const std::string& provider_id,
    const std::string& credential_id,
    std::string* error);
bool ClearProviderCodexBridge(const AppStorage& storage, const std::string& provider_id, std::string* error);
