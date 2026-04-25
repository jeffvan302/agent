#pragma once

#include "types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Certificate generation (shared with legacy remote_ollama_worker)
struct RemoteProviderWorkerCertificateMaterial {
    std::string certificate_pem;
    std::string private_key_pem;
    std::string fingerprint;
};

std::string GenerateRemoteProviderWorkerSharedSecret();
bool GenerateRemoteProviderWorkerSelfSignedCertificateMaterial(
    RemoteProviderWorkerCertificateMaterial* material,
    std::string* error);

// An exported provider: the full provider config plus optional auth record.
struct RemoteProviderWorkerExportedProvider {
    ProviderConfig provider;
    std::optional<ProviderAuthRecord> auth_record;
};

// Generic worker configuration (self-contained JSON).
struct RemoteProviderWorkerConfig {
    int version = 2;
    std::string worker_name = "Agent Remote Worker";
    std::string bind_address = "0.0.0.0";
    int https_port = 8765;
    std::string shared_secret;
    std::string certificate_pem;
    std::string private_key_pem;
    std::string certificate_fingerprint;
    std::vector<RemoteProviderWorkerExportedProvider> exported_providers;
    std::filesystem::path source_path;
};

std::optional<RemoteProviderWorkerConfig> LoadRemoteProviderWorkerConfig(
    const std::filesystem::path& path, std::string* error);
bool SaveRemoteProviderWorkerConfig(
    const RemoteProviderWorkerConfig& config, std::string* error);

enum class RemoteProviderWorkerCommandMode {
    Setup,
    Run,
};

// Main entry point for headless operation.
int RunRemoteProviderWorkerCommand(
    RemoteProviderWorkerCommandMode mode,
    const std::filesystem::path& config_path);

// Convenience: legacy Ollama-only config is auto-migrated to the new format
// when loaded.
