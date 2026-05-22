#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// Data structures for web user / group / project-binding storage.
// Persisted to <config_root>/users.json via WebUserStore.
// ──────────────────────────────────────────────────────────────────────────────

struct WebUser {
    std::string id;
    std::string username;         // unique login name; used as $<UserName> variable
    std::string display_name;
    std::string email;
    std::string password_hash;    // "pbkdf2:100000:<hex_salt>:<hex_key>"
    bool enabled = true;
    bool force_password_reset = false;
    bool allow_folder_browse = false;
    std::string created_at;       // ISO 8601
    std::string last_login_at;    // ISO 8601, may be empty
};

struct WebGroup {
    std::string id;
    std::string name;
    std::vector<std::string> user_ids;
};

struct WebProjectBinding {
    std::string project_id;
    std::vector<std::string> group_ids;
    bool user_project_folder_enabled = false;
    std::string user_project_folder_root;   // base path; per-user path = root/<username>
};

// ──────────────────────────────────────────────────────────────────────────────
// WebUserStore — reads/writes users.json and provides auth helpers.
// Thread-safe: all methods acquire the internal mutex.
// ──────────────────────────────────────────────────────────────────────────────
class WebUserStore {
public:
    explicit WebUserStore(std::filesystem::path file_path);

    // Creates the file with a default test user if it does not exist.
    void EnsureInitialized();

    // ── Users ──────────────────────────────────────────────────────────────
    std::vector<WebUser> GetUsers() const;
    std::optional<WebUser> FindUserByUsername(const std::string& username) const;
    std::optional<WebUser> FindUserById(const std::string& id) const;

    // Hashes plaintext_password and stores it in user.password_hash, then saves.
    // user.id is generated if empty.
    bool AddUser(WebUser& user, const std::string& plaintext_password);

    bool UpdateUser(const WebUser& user);
    bool DeleteUser(const std::string& user_id);
    bool SetPassword(const std::string& user_id, const std::string& plaintext_password);

    // Returns true if plaintext_password matches the stored hash.
    bool VerifyPassword(const WebUser& user, const std::string& plaintext_password) const;

    // Updates last_login_at to now and clears force_password_reset if applicable.
    void RecordLogin(const std::string& user_id);

    // ── Groups ─────────────────────────────────────────────────────────────
    std::vector<WebGroup> GetGroups() const;
    bool AddGroup(WebGroup& group);    // group.id generated if empty
    bool UpdateGroup(const WebGroup& group);
    bool DeleteGroup(const std::string& group_id);

    // ── Project bindings ───────────────────────────────────────────────────
    std::vector<WebProjectBinding> GetBindings() const;
    void SetBinding(const WebProjectBinding& binding);   // upsert by project_id
    void RemoveBinding(const std::string& project_id);

    // Returns all project IDs accessible to the given user (via group membership).
    std::vector<std::string> GetProjectIdsForUser(const std::string& user_id) const;

    // Returns the resolved per-user output folder for a project, or empty if disabled.
    std::string GetUserProjectFolder(const std::string& project_id,
                                     const std::string& username) const;

private:
    void Load();
    void Save() const;
    std::string HashPassword(const std::string& plaintext) const;
    bool CheckPassword(const std::string& stored_hash, const std::string& plaintext) const;
    static std::string GenerateUuid();
    static std::string NowIso8601();
    static std::string BytesToHex(const unsigned char* data, size_t len);
    static std::vector<unsigned char> HexToBytes(const std::string& hex);

    std::filesystem::path file_path_;
    mutable std::mutex mutex_;
    std::vector<WebUser> users_;
    std::vector<WebGroup> groups_;
    std::vector<WebProjectBinding> bindings_;
};
