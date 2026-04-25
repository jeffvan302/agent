#pragma once

#include <filesystem>
#include <string>

enum class RemoteOllamaCommandMode {
    Setup,
    Remote,
};

struct RemoteWorkerCertificateMaterial {
    std::string certificate_pem;
    std::string private_key_pem;
    std::string fingerprint;
};

std::string GenerateRemoteWorkerSharedSecret();
bool GenerateRemoteWorkerSelfSignedCertificateMaterial(
    RemoteWorkerCertificateMaterial* material,
    std::string* error);
bool GenerateRemoteWorkerSelfSignedCert(
    const std::filesystem::path& cert_path,
    const std::filesystem::path& key_path,
    std::string* fingerprint,
    std::string* error);
std::string RemoteWorkerCertificateFingerprint(const std::filesystem::path& cert_path, std::string* error);

int RunRemoteOllamaCommand(RemoteOllamaCommandMode mode, const std::filesystem::path& config_path);
