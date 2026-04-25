#include "web_user_store.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

std::string WebUserStore::BytesToHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

std::vector<unsigned char> WebUserStore::HexToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    if (hex.size() % 2 != 0) return bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int byte = 0;
        std::istringstream ss(hex.substr(i, 2));
        ss >> std::hex >> byte;
        bytes.push_back(static_cast<unsigned char>(byte));
    }
    return bytes;
}

std::string WebUserStore::GenerateUuid() {
    GUID guid = {};
    CoCreateGuid(&guid);
    wchar_t buf[40] = {};
    StringFromGUID2(guid, buf, static_cast<int>(std::size(buf)));
    // buf is like {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
    // Convert to narrow string, strip braces
    std::string result;
    result.reserve(36);
    for (int i = 1; buf[i] && buf[i] != L'}'; ++i)
        result += static_cast<char>(buf[i]);
    return result;
}

std::string WebUserStore::NowIso8601() {
    SYSTEMTIME st = {};
    GetSystemTime(&st);
    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ──────────────────────────────────────────────────────────────────────────────
// Password hashing — PBKDF2-SHA256 via Windows CNG
// Format stored: "pbkdf2:100000:<hex_16_salt>:<hex_32_key>"
// ──────────────────────────────────────────────────────────────────────────────

static constexpr ULONG kPbkdf2Iterations = 100000;
static constexpr size_t kSaltBytes = 16;
static constexpr size_t kKeyBytes  = 32;

std::string WebUserStore::HashPassword(const std::string& plaintext) const {
    // Generate random salt
    unsigned char salt[kSaltBytes] = {};
    BCryptGenRandom(nullptr, salt, static_cast<ULONG>(kSaltBytes),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    // Open HMAC-SHA256 algorithm provider
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                                    nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    unsigned char derived[kKeyBytes] = {};
    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
        static_cast<ULONG>(plaintext.size()),
        salt, static_cast<ULONG>(kSaltBytes),
        kPbkdf2Iterations,
        derived, static_cast<ULONG>(kKeyBytes),
        0);

    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    }

    return "pbkdf2:" + std::to_string(kPbkdf2Iterations) + ":"
         + BytesToHex(salt, kSaltBytes) + ":"
         + BytesToHex(derived, kKeyBytes);
}

bool WebUserStore::CheckPassword(const std::string& stored_hash,
                                 const std::string& plaintext) const {
    // Parse "pbkdf2:<iter>:<hex_salt>:<hex_key>"
    auto part = [&](int n) -> std::string {
        size_t start = 0;
        for (int i = 0; i < n; ++i) {
            start = stored_hash.find(':', start);
            if (start == std::string::npos) return {};
            ++start;
        }
        size_t end = stored_hash.find(':', start);
        return stored_hash.substr(start, end == std::string::npos ? std::string::npos : end - start);
    };

    if (part(0) != "pbkdf2") return false;
    const ULONG iters = static_cast<ULONG>(std::stoul(part(1)));
    const auto salt_bytes = HexToBytes(part(2));
    const auto expected_key = HexToBytes(part(3));
    if (salt_bytes.empty() || expected_key.empty()) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                                    nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return false;
    }

    std::vector<unsigned char> derived(kKeyBytes);
    NTSTATUS status = BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
        static_cast<ULONG>(plaintext.size()),
        const_cast<PUCHAR>(salt_bytes.data()),
        static_cast<ULONG>(salt_bytes.size()),
        iters,
        derived.data(), static_cast<ULONG>(derived.size()),
        0);

    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) return false;

    // Constant-time compare to prevent timing attacks
    if (derived.size() != expected_key.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < derived.size(); ++i)
        diff |= derived[i] ^ expected_key[i];
    return diff == 0;
}

// ──────────────────────────────────────────────────────────────────────────────
// JSON serialization helpers
// ──────────────────────────────────────────────────────────────────────────────

static json UserToJson(const WebUser& u) {
    return json{
        {"id",                    u.id},
        {"username",              u.username},
        {"display_name",          u.display_name},
        {"email",                 u.email},
        {"password_hash",         u.password_hash},
        {"enabled",               u.enabled},
        {"force_password_reset",  u.force_password_reset},
        {"created_at",            u.created_at},
        {"last_login_at",         u.last_login_at},
    };
}

static WebUser UserFromJson(const json& j) {
    WebUser u;
    u.id                   = j.value("id", "");
    u.username             = j.value("username", "");
    u.display_name         = j.value("display_name", "");
    u.email                = j.value("email", "");
    u.password_hash        = j.value("password_hash", "");
    u.enabled              = j.value("enabled", true);
    u.force_password_reset = j.value("force_password_reset", false);
    u.created_at           = j.value("created_at", "");
    u.last_login_at        = j.value("last_login_at", "");
    return u;
}

static json GroupToJson(const WebGroup& g) {
    return json{
        {"id",       g.id},
        {"name",     g.name},
        {"user_ids", g.user_ids},
    };
}

static WebGroup GroupFromJson(const json& j) {
    WebGroup g;
    g.id   = j.value("id", "");
    g.name = j.value("name", "");
    if (j.contains("user_ids") && j["user_ids"].is_array())
        g.user_ids = j["user_ids"].get<std::vector<std::string>>();
    return g;
}

static json BindingToJson(const WebProjectBinding& b) {
    return json{
        {"project_id",                   b.project_id},
        {"group_ids",                    b.group_ids},
        {"user_project_folder_enabled",  b.user_project_folder_enabled},
        {"user_project_folder_root",     b.user_project_folder_root},
    };
}

static WebProjectBinding BindingFromJson(const json& j) {
    WebProjectBinding b;
    b.project_id                  = j.value("project_id", "");
    b.user_project_folder_enabled = j.value("user_project_folder_enabled", false);
    b.user_project_folder_root    = j.value("user_project_folder_root", "");
    if (j.contains("group_ids") && j["group_ids"].is_array())
        b.group_ids = j["group_ids"].get<std::vector<std::string>>();
    return b;
}

// ──────────────────────────────────────────────────────────────────────────────
// Construction / persistence
// ──────────────────────────────────────────────────────────────────────────────

WebUserStore::WebUserStore(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

void WebUserStore::EnsureInitialized() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::filesystem::exists(file_path_)) {
        Load();
        return;
    }
    // Create default file with one test admin user
    // (force_password_reset = true so they must change it on first login)
    WebUser admin;
    admin.id                   = GenerateUuid();
    admin.username             = "admin";
    admin.display_name         = "Administrator";
    admin.email                = "";
    admin.enabled              = true;
    admin.force_password_reset = true;
    admin.created_at           = NowIso8601();
    admin.password_hash        = HashPassword("changeme");
    users_.push_back(admin);
    Save();
}

void WebUserStore::Load() {
    // Caller holds mutex_
    std::ifstream f(file_path_);
    if (!f.is_open()) return;
    json j;
    try { f >> j; } catch (...) { return; }

    users_.clear();
    groups_.clear();
    bindings_.clear();

    if (j.contains("users") && j["users"].is_array())
        for (const auto& u : j["users"]) users_.push_back(UserFromJson(u));
    if (j.contains("groups") && j["groups"].is_array())
        for (const auto& g : j["groups"]) groups_.push_back(GroupFromJson(g));
    if (j.contains("project_bindings") && j["project_bindings"].is_array())
        for (const auto& b : j["project_bindings"]) bindings_.push_back(BindingFromJson(b));
}

void WebUserStore::Save() const {
    // Caller holds mutex_
    json j;
    j["users"] = json::array();
    for (const auto& u : users_) j["users"].push_back(UserToJson(u));
    j["groups"] = json::array();
    for (const auto& g : groups_) j["groups"].push_back(GroupToJson(g));
    j["project_bindings"] = json::array();
    for (const auto& b : bindings_) j["project_bindings"].push_back(BindingToJson(b));

    // Atomic write: write to temp file then rename
    std::filesystem::create_directories(std::filesystem::path(file_path_).parent_path());
    const auto tmp = std::filesystem::path(file_path_).replace_extension(".tmp");
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    std::filesystem::rename(tmp, file_path_);
}

// ──────────────────────────────────────────────────────────────────────────────
// User operations
// ──────────────────────────────────────────────────────────────────────────────

std::vector<WebUser> WebUserStore::GetUsers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_;
}

std::optional<WebUser> WebUserStore::FindUserByUsername(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& u : users_)
        if (u.username == username) return u;
    return std::nullopt;
}

std::optional<WebUser> WebUserStore::FindUserById(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& u : users_)
        if (u.id == id) return u;
    return std::nullopt;
}

bool WebUserStore::AddUser(WebUser& user, const std::string& plaintext_password) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check for duplicate username
    for (const auto& u : users_)
        if (u.username == user.username) return false;
    if (user.id.empty()) user.id = GenerateUuid();
    if (user.created_at.empty()) user.created_at = NowIso8601();
    user.password_hash = HashPassword(plaintext_password);
    users_.push_back(user);
    Save();
    return true;
}

bool WebUserStore::UpdateUser(const WebUser& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : users_) {
        if (u.id == user.id) {
            u = user;
            Save();
            return true;
        }
    }
    return false;
}

bool WebUserStore::DeleteUser(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](const WebUser& u) { return u.id == user_id; });
    if (it == users_.end()) return false;
    users_.erase(it);
    // Also remove from all groups
    for (auto& g : groups_) {
        auto uid_it = std::find(g.user_ids.begin(), g.user_ids.end(), user_id);
        if (uid_it != g.user_ids.end()) g.user_ids.erase(uid_it);
    }
    Save();
    return true;
}

bool WebUserStore::SetPassword(const std::string& user_id,
                               const std::string& plaintext_password) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : users_) {
        if (u.id == user_id) {
            u.password_hash = HashPassword(plaintext_password);
            u.force_password_reset = false;
            Save();
            return true;
        }
    }
    return false;
}

bool WebUserStore::VerifyPassword(const WebUser& user,
                                  const std::string& plaintext_password) const {
    return CheckPassword(user.password_hash, plaintext_password);
}

void WebUserStore::RecordLogin(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& u : users_) {
        if (u.id == user_id) {
            u.last_login_at = NowIso8601();
            Save();
            return;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Group operations
// ──────────────────────────────────────────────────────────────────────────────

std::vector<WebGroup> WebUserStore::GetGroups() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return groups_;
}

bool WebUserStore::AddGroup(WebGroup& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& g : groups_)
        if (g.name == group.name) return false;
    if (group.id.empty()) group.id = GenerateUuid();
    groups_.push_back(group);
    Save();
    return true;
}

bool WebUserStore::UpdateGroup(const WebGroup& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& g : groups_) {
        if (g.id == group.id) {
            g = group;
            Save();
            return true;
        }
    }
    return false;
}

bool WebUserStore::DeleteGroup(const std::string& group_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](const WebGroup& g) { return g.id == group_id; });
    if (it == groups_.end()) return false;
    groups_.erase(it);
    // Remove from bindings
    for (auto& b : bindings_) {
        auto gid_it = std::find(b.group_ids.begin(), b.group_ids.end(), group_id);
        if (gid_it != b.group_ids.end()) b.group_ids.erase(gid_it);
    }
    Save();
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Project binding operations
// ──────────────────────────────────────────────────────────────────────────────

std::vector<WebProjectBinding> WebUserStore::GetBindings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bindings_;
}

void WebUserStore::SetBinding(const WebProjectBinding& binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& b : bindings_) {
        if (b.project_id == binding.project_id) {
            b = binding;
            Save();
            return;
        }
    }
    bindings_.push_back(binding);
    Save();
}

void WebUserStore::RemoveBinding(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [&](const WebProjectBinding& b) { return b.project_id == project_id; });
    if (it != bindings_.end()) {
        bindings_.erase(it);
        Save();
    }
}

std::vector<std::string> WebUserStore::GetProjectIdsForUser(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& binding : bindings_) {
        bool accessible = false;
        for (const auto& gid : binding.group_ids) {
            for (const auto& group : groups_) {
                if (group.id == gid) {
                    for (const auto& uid : group.user_ids) {
                        if (uid == user_id) { accessible = true; break; }
                    }
                }
                if (accessible) break;
            }
            if (accessible) break;
        }
        if (accessible) result.push_back(binding.project_id);
    }
    return result;
}

std::string WebUserStore::GetUserProjectFolder(const std::string& project_id,
                                               const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& b : bindings_) {
        if (b.project_id == project_id && b.user_project_folder_enabled
                && !b.user_project_folder_root.empty()) {
            return (std::filesystem::path(b.user_project_folder_root) / username).string();
        }
    }
    return {};
}
