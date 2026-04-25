struct ToolTracePayload {
    std::string project_id;
    std::string chat_id;
    ToolTraceEntry entry;
};

std::filesystem::path DetermineAppRoot() {
    wchar_t module_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    std::filesystem::path exe_path(module_path);
    std::filesystem::path root = exe_path.parent_path();
    if (root.filename() == L"build") {
        root = root.parent_path();
    }
    return root;
}

std::filesystem::path ResolveAbsolutePath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

RuntimePaths ResolveRuntimePaths(const std::filesystem::path& startup_override,
                                 const std::filesystem::path& config_override,
                                 const std::filesystem::path& data_override,
                                 const std::filesystem::path& log_override) {
    RuntimePaths paths;
    paths.startup_root = startup_override.empty()
        ? DetermineAppRoot()
        : ResolveAbsolutePath(startup_override);
    paths.startup_root = ResolveAbsolutePath(paths.startup_root);
    paths.config_root = config_override.empty()
        ? (paths.startup_root / ".config")
        : ResolveAbsolutePath(config_override);
    paths.data_root = data_override.empty()
        ? (paths.startup_root / ".data")
        : ResolveAbsolutePath(data_override);
    paths.log_root = log_override.empty()
        ? (paths.startup_root / ".log")
        : ResolveAbsolutePath(log_override);
    return paths;
}

void CopyFileIfMissing(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (source.empty() || destination.empty() || source == destination || !std::filesystem::is_regular_file(source) ||
        std::filesystem::exists(destination)) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
}

void CopyDirectoryIfMissing(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (source.empty() || destination.empty() || source == destination || !std::filesystem::is_directory(source) ||
        std::filesystem::exists(destination)) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    ec.clear();
    std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, ec);
}

void MigrateLegacyRuntimeLayout(const RuntimePaths& paths) {
    std::error_code ec;
    std::filesystem::create_directories(paths.config_root, ec);
    std::filesystem::create_directories(paths.data_root, ec);
    std::filesystem::create_directories(paths.log_root, ec);

    const std::filesystem::path migration_marker = paths.config_root / ".runtime_layout_migrated";
    if (std::filesystem::exists(migration_marker)) {
        return;
    }

    const std::filesystem::path legacy_root = paths.startup_root;
    const std::filesystem::path legacy_data_root = legacy_root / "data";

    for (const auto& filename : {
            L"providers.json",
            L"mcp_servers.json",
            L"context_compression_configs.json",
            L"model_tools.json",
            L"users.json",
            L"web_settings.json"}) {
        CopyFileIfMissing(legacy_root / filename, paths.config_root / filename);
    }

    CopyFileIfMissing(legacy_root / "web_audit.log", paths.log_root / "web" / "web_audit.log");
    CopyFileIfMissing(legacy_data_root / "web_remembered_sessions.json", paths.data_root / "web_remembered_sessions.json");
    CopyFileIfMissing(legacy_data_root / "rag_image_ingest_settings.json",
                      paths.config_root / "rag" / "rag_image_ingest_settings.json");
    CopyFileIfMissing(legacy_data_root / "rag_embedding_runtime.log",
                      paths.log_root / "rag" / "rag_embedding_runtime.log");
    CopyFileIfMissing(legacy_data_root / "rag_image_ingest_runtime.log",
                      paths.log_root / "rag" / "rag_image_ingest_runtime.log");
    CopyDirectoryIfMissing(legacy_data_root / "rag_libraries", paths.data_root / "rag_libraries");

    const std::filesystem::path legacy_projects_root = legacy_data_root / "projects";
    if (std::filesystem::is_directory(legacy_projects_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(legacy_projects_root)) {
            if (!entry.is_directory()) {
                continue;
            }

            const std::filesystem::path legacy_project = entry.path();
            const std::filesystem::path config_project = paths.config_root / "projects" / legacy_project.filename();
            const std::filesystem::path data_project = paths.data_root / "projects" / legacy_project.filename();

            for (const auto& filename : {
                    L"project.json",
                    L"mcp_consent.json",
                    L"project_mcp.json",
                    L"context_compression.json",
                    L"project_settings.json",
                    L"project_rag.json"}) {
                CopyFileIfMissing(legacy_project / filename, config_project / filename);
            }

            CopyDirectoryIfMissing(legacy_project / "chats", data_project / "chats");
        }
    }

    std::ofstream marker_output(migration_marker, std::ios::binary | std::ios::trunc);
    if (marker_output.is_open()) {
        marker_output << "runtime_layout_migrated=1\n";
    }
