import pathlib, sys

def modify_types_h():
    p = pathlib.Path('src/types.h')
    text = p.read_text(encoding='utf-16-le')
    old = '''    bool built_in_questionnaire_enabled = false;      // Enable user questionnaire built-in tool
    int questionnaire_max_options = 8;                // Max options the questionnaire tool can offer
    bool questionnaire_restrict_by_mode = false;      // Only allow questionnaire in a specific agentic mode
    std::string questionnaire_allowed_mode_id;          // Agentic mode ID that triggers questionnaire availability
    int model_timeout_seconds = 0;                  // 0 = wait forever (default), otherwise max seconds per model request
};'''
    new = '''    bool built_in_questionnaire_enabled = false;      // Enable user questionnaire built-in tool
    int questionnaire_max_options = 8;                // Max options the questionnaire tool can offer
    bool questionnaire_restrict_by_mode = false;      // Only allow questionnaire in a specific agentic mode
    std::string questionnaire_allowed_mode_id;          // Agentic mode ID that triggers questionnaire availability
    bool built_in_filesystem_enabled = false;         // Enable project_filesystem built-in tool
    bool built_in_filesystem_auto_archive = false;    // Auto-archive file reads/writes into Artifact/Code Memory
    std::string built_in_filesystem_working_directory = "$ProjectFolder$";
    int model_timeout_seconds = 0;                  // 0 = wait forever (default), otherwise max seconds per model request
};'''
    if old not in text:
        print('types.h: old not found', file=sys.stderr)
        return False
    text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')
    print('types.h updated')
    return True

def modify_storage_cpp():
    p = pathlib.Path('src/storage.cpp')
    text = p.read_text(encoding='utf-16-le')
    # ProjectSettingsToJson
    old1 = '''    j["questionnaire_restrict_by_mode"] = settings.questionnaire_restrict_by_mode;
    j["questionnaire_allowed_mode_id"] = settings.questionnaire_allowed_mode_id;
    j["model_timeout_seconds"] = settings.model_timeout_seconds;'''
    new1 = '''    j["questionnaire_restrict_by_mode"] = settings.questionnaire_restrict_by_mode;
    j["questionnaire_allowed_mode_id"] = settings.questionnaire_allowed_mode_id;
    j["built_in_filesystem_enabled"] = settings.built_in_filesystem_enabled;
    j["built_in_filesystem_auto_archive"] = settings.built_in_filesystem_auto_archive;
    j["built_in_filesystem_working_directory"] = settings.built_in_filesystem_working_directory;
    j["model_timeout_seconds"] = settings.model_timeout_seconds;'''
    if old1 not in text:
        print('storage.cpp: old1 not found', file=sys.stderr)
        return False
    text = text.replace(old1, new1)
    # ProjectSettingsFromJson
    old2 = '''    settings.questionnaire_restrict_by_mode = j.value("questionnaire_restrict_by_mode", false);
    settings.questionnaire_allowed_mode_id = j.value("questionnaire_allowed_mode_id", "");
    settings.model_timeout_seconds = j.value("model_timeout_seconds", 0);'''
    new2 = '''    settings.questionnaire_restrict_by_mode = j.value("questionnaire_restrict_by_mode", false);
    settings.questionnaire_allowed_mode_id = j.value("questionnaire_allowed_mode_id", "");
    settings.built_in_filesystem_enabled = j.value("built_in_filesystem_enabled", false);
    settings.built_in_filesystem_auto_archive = j.value("built_in_filesystem_auto_archive", false);
    settings.built_in_filesystem_working_directory = j.value("built_in_filesystem_working_directory", "$ProjectFolder$");
    if (Trim(settings.built_in_filesystem_working_directory).empty()) {
        settings.built_in_filesystem_working_directory = "$ProjectFolder$";
    }
    settings.model_timeout_seconds = j.value("model_timeout_seconds", 0);'''
    if old2 not in text:
        print('storage.cpp: old2 not found', file=sys.stderr)
        return False
    text = text.replace(old2, new2)
    p.write_text(text, encoding='utf-8')
    print('storage.cpp updated')
    return True

def modify_main_cpp():
    p = pathlib.Path('src/main.cpp')
    text = p.read_text(encoding='utf-8')
    old = '''        if (built_in_tools::IsQuestionnaireEnabled(proj_settings, proj_settings.selected_agentic_mode_id)) {
            const std::string q_context = built_in_tools::QuestionnaireSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\\n\\n";
            }
            request.system_prompt += q_context;
            system_prompt_sections.push_back({"User Questionnaire", q_context});
        }
    }'''
    new = '''        if (built_in_tools::IsQuestionnaireEnabled(proj_settings, proj_settings.selected_agentic_mode_id)) {
            const std::string q_context = built_in_tools::QuestionnaireSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\\n\\n";
            }
            request.system_prompt += q_context;
            system_prompt_sections.push_back({"User Questionnaire", q_context});
        }
        if (proj_settings.built_in_filesystem_enabled) {
            const std::string fs_context = built_in_tools::FilesystemSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\\n\\n";
            }
            request.system_prompt += fs_context;
            system_prompt_sections.push_back({"Project Filesystem", fs_context});
        }
    }'''
    if old not in text:
        print('main.cpp: old not found', file=sys.stderr)
        return False
    text = text.replace(old, new)
    # options mapping
    old2 = '''    options.questionnaire_allowed_mode_id = project_settings.questionnaire_allowed_mode_id;
    options.model_timeout_seconds = project_settings.model_timeout_seconds;'''
    new2 = '''    options.questionnaire_allowed_mode_id = project_settings.questionnaire_allowed_mode_id;
    options.built_in_filesystem_enabled = project_settings.built_in_filesystem_enabled;
    options.built_in_filesystem_auto_archive = project_settings.built_in_filesystem_auto_archive;
    options.built_in_filesystem_working_directory = project_settings.built_in_filesystem_working_directory;
    options.model_timeout_seconds = project_settings.model_timeout_seconds;'''
    if old2 not in text:
        print('main.cpp: old2 not found', file=sys.stderr)
        return False
    text = text.replace(old2, new2)
    # saved_settings mapping
    old3 = '''    saved_settings.questionnaire_restrict_by_mode = result->questionnaire_restrict_by_mode;
    saved_settings.questionnaire_allowed_mode_id = result->questionnaire_allowed_mode_id;
    saved_settings.model_timeout_seconds = result->model_timeout_seconds;'''
    new3 = '''    saved_settings.questionnaire_restrict_by_mode = result->questionnaire_restrict_by_mode;
    saved_settings.questionnaire_allowed_mode_id = result->questionnaire_allowed_mode_id;
    saved_settings.built_in_filesystem_enabled = result->built_in_filesystem_enabled;
    saved_settings.built_in_filesystem_auto_archive = result->built_in_filesystem_auto_archive;
    saved_settings.built_in_filesystem_working_directory = result->built_in_filesystem_working_directory;
    saved_settings.model_timeout_seconds = result->model_timeout_seconds;'''
    if old3 not in text:
        print('main.cpp: old3 not found', file=sys.stderr)
        return False
    text = text.replace(old3, new3)
    p.write_text(text, encoding='utf-8')
    print('main.cpp updated')
    return True

def modify_web_server_cpp():
    p = pathlib.Path('src/web_server.cpp')
    # detect encoding: first bytes were utf-8-sig earlier
    text = p.read_text(encoding='utf-8-sig')
    old = '''    if (built_in_tools::IsQuestionnaireEnabled(project_settings, mode_id)) {
        const std::string q_context = built_in_tools::QuestionnaireSystemPrompt();
        AppendPromptSection(built.full_prompt, q_context);
        built.sections.push_back({"User Questionnaire", q_context});
    }'''
    new = '''    if (built_in_tools::IsQuestionnaireEnabled(project_settings, mode_id)) {
        const std::string q_context = built_in_tools::QuestionnaireSystemPrompt();
        AppendPromptSection(built.full_prompt, q_context);
        built.sections.push_back({"User Questionnaire", q_context});
    }
    if (project_settings.built_in_filesystem_enabled) {
        const std::string fs_context = built_in_tools::FilesystemSystemPrompt();
        AppendPromptSection(built.full_prompt, fs_context);
        built.sections.push_back({"Project Filesystem", fs_context});
    }'''
    if old not in text:
        print('web_server.cpp: old not found', file=sys.stderr)
        return False
    text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')
    print('web_server.cpp updated')
    return True

def modify_project_settings_dialog_h():
    p = pathlib.Path('src/project_settings_dialog.h')
    text = p.read_text(encoding='utf-8')
    # There are two identical blocks for Options and Result struct; replace all
    old = '''    bool built_in_questionnaire_enabled = false;
    int questionnaire_max_options = 8;
    bool questionnaire_restrict_by_mode = false;
    std::string questionnaire_allowed_mode_id;
    int model_timeout_seconds = 0;                    // 0 = infinite (default), otherwise max seconds per model request
};'''
    new = '''    bool built_in_questionnaire_enabled = false;
    int questionnaire_max_options = 8;
    bool questionnaire_restrict_by_mode = false;
    std::string questionnaire_allowed_mode_id;
    bool built_in_filesystem_enabled = false;
    bool built_in_filesystem_auto_archive = false;
    std::string built_in_filesystem_working_directory = "$ProjectFolder$";
    int model_timeout_seconds = 0;                    // 0 = infinite (default), otherwise max seconds per model request
};'''
    if old not in text:
        print('project_settings_dialog.h: old not found', file=sys.stderr)
        return False
    text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')
    print('project_settings_dialog.h updated')
    return True

def modify_built_in_tools_h():
    p = pathlib.Path('src/built_in_tools.h')
    text = p.read_text(encoding='utf-8')
    # Add constant
    old = '''inline constexpr const char* kDefaultPlannerStorageFolder = "$ProjectFolder$\\.agent";
inline constexpr const char* kPlannerFileName = "planner.json";'''
    new = '''inline constexpr const char* kDefaultPlannerStorageFolder = "$ProjectFolder$\\.agent";
inline constexpr const char* kPlannerFileName = "planner.json";
inline constexpr const char* kFilesystemToolName = "project_filesystem";'''
    if old not in text:
        print('built_in_tools.h: old const not found', file=sys.stderr)
        return False
    text = text.replace(old, new)
    # Update IsBuiltInToolName
    old = '''inline bool IsBuiltInToolName(const std::string& name) {
    return name == kPowerShellToolName || name == kQuestionnaireToolName ||
           name == kPlannerToolName || name == kCompletionDriverToolName;
}'''
    new = '''inline bool IsBuiltInToolName(const std::string& name) {
    return name == kPowerShellToolName || name == kQuestionnaireToolName ||
           name == kPlannerToolName || name == kCompletionDriverToolName ||
           name == kFilesystemToolName;
}'''
    text = text.replace(old, new)
    # Update TraceTitleForBuiltInTool
    old = '''inline std::string TraceTitleForBuiltInTool(const std::string& name) {
    if (name == kPowerShellToolName) return "Built-in / PowerShell";
    if (name == kQuestionnaireToolName) return "Built-in / User Questionnaire";
    if (name == kPlannerToolName) return "Built-in / Planner";
    if (name == kCompletionDriverToolName) return "Built-in / Completion Driver";
    return "Built-in / " + name;
}'''
    new = '''inline std::string TraceTitleForBuiltInTool(const std::string& name) {
    if (name == kPowerShellToolName) return "Built-in / PowerShell";
    if (name == kQuestionnaireToolName) return "Built-in / User Questionnaire";
    if (name == kPlannerToolName) return "Built-in / Planner";
    if (name == kCompletionDriverToolName) return "Built-in / Completion Driver";
    if (name == kFilesystemToolName) return "Built-in / Project Filesystem";
    return "Built-in / " + name;
}'''
    text = text.replace(old, new)
    # Add FilesystemSystemPrompt before CompletionDriverSystemPrompt
    old = '''inline std::string CompletionDriverSystemPrompt() {'''
    new = '''inline std::string FilesystemSystemPrompt() {
    return (
        "Project Filesystem Instructions:\n"
        "- Use the project_filesystem tool for all file read, write, directory listing, and edit operations.\n"
        "- Always specify paths relative to the configured working directory (default $ProjectFolder$). Templates like $ProjectFolder$ are expanded automatically.\n"
        "- Actions: read, write, edit, list_directory, create_directory.\n"
        "- read: Returns file content. Optionally pass start_line / end_line (1-based, inclusive) or start_offset / length (bytes).\n"
        "- write: Overwrites a file. Pass create_backup=true to snapshot the existing file into .agent/backups/<timestamp>/<path> before overwriting.\n"
        "- edit: Applies JSON diff edits. Each edit object uses either:\n"
        "  1) old_lines + new_lines — exact match-and-replace by contiguous lines, or\n"
        "  2) direct JSON-patch style: {\\"op\\": \\"replace\\", \\"path\\": \\"...\\"} (not yet supported; use old_lines/new_lines).\n"
        "- list_directory: Returns files and subdirectories for a path.\n"
        "- create_directory: Creates a directory (including parents).\n"
        "- Backups: When create_backup=true, the tool copies the original file or directory tree into $ProjectFolder$/.agent/backups/<timestamp>/<original_relative_path>.\n"
        "- The host auto-expands path templates. Do not escape backslashes unnecessarily; forward slashes are acceptable.\n"
        "- If a path is invalid, permission is denied, or a file is missing, the tool returns a clear error — do not retry blindly.\n"
        "Examples:\n"
        "  Read whole file: {\\"action\\":\\"read\\",\\"path\\":\\"src/main.cpp\\"}\n"
        "  Read lines 10-20: {\\"action\\":\\"read\\",\\"path\\":\\"src/main.cpp\\",\\"start_line\\":10,\\"end_line\\":20}\n"
        "  Write with backup: {\\"action\\":\\"write\\",\\"path\\":\\"README.md\\",\\"content\\":\\"...\\",\\"create_backup\\":true}\n"
        "  Edit by lines: {\\"action\\":\\"edit\\",\\"path\\":\\"src/main.cpp\\",\\"edits\\":[{\\"old_lines\\":[\\"old line 1\\",\\"old line 2\\"],\\"new_lines\\":[\\"new line 1\\"]}]}\n"
        "  List dir: {\\"action\\":\\"list_directory\\",\\"path\\":\\"src\\"}\n"
        "  Create dir: {\\"action\\":\\"create_directory\\",\\"path\\":\\"src/utils\\"}");
}

inline std::string CompletionDriverSystemPrompt() {'''
    text = text.replace(old, new)
    # Add filesystem definition before return definitions;
    old = '''    if (IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        ChatToolDefinition q;
        q.name = kQuestionnaireToolName;
        q.description =
            "Present a multiple-choice question to the user and wait for their selection before continuing. "
            "Use this for clarifying requirements, collecting preferences, confirming trade-offs, or gathering planning decisions. "
            "The question appears as a bubble with clickable options in the web UI. The model should not assume the answer; "
            "it must wait until the user selects one of the provided choices and the tool result is injected back into context. "
            "Do not ask rhetorical questions without this tool when a decision is needed.";
        q.parameters_json = R"({"type":"object","properties":{"question":{"type":"string","description":"The question or prompt text shown to the user."},"options":{"type":"array","items":{"type":"string"},"description":"A list of distinct clickable answer choices."},"allow_multiple":{"type":"boolean","description":"If true, the user may select more than one option. Default false."}},"required":["question","options"]})";
        definitions.push_back(std::move(q));
    }
    return definitions;
}'''
    new = '''    if (IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        ChatToolDefinition q;
        q.name = kQuestionnaireToolName;
        q.description =
            "Present a multiple-choice question to the user and wait for their selection before continuing. "
            "Use this for clarifying requirements, collecting preferences, confirming trade-offs, or gathering planning decisions. "
            "The question appears as a bubble with clickable options in the web UI. The model should not assume the answer; "
            "it must wait until the user selects one of the provided choices and the tool result is injected back into context. "
            "Do not ask rhetorical questions without this tool when a decision is needed.";
        q.parameters_json = R"({"type":"object","properties":{"question":{"type":"string","description":"The question or prompt text shown to the user."},"options":{"type":"array","items":{"type":"string"},"description":"A list of distinct clickable answer choices."},"allow_multiple":{"type":"boolean","description":"If true, the user may select more than one option. Default false."}},"required":["question","options"]})";
        definitions.push_back(std::move(q));
    }
    if (settings.built_in_filesystem_enabled) {
        ChatToolDefinition fs;
        fs.name = kFilesystemToolName;
        fs.description =
            "Project Filesystem tool. Read, write, edit, list, and create files/directories under the configured working directory. "
            "All paths are relative to the working directory (default $ProjectFolder$) and templates are auto-expanded.\n\n"
            "Actions:\n"
            "- read — Return the full content of a file, or a portion if start_line/end_line or start_offset/length are provided.\n"
            "- write — Overwrite a file with new content. Set create_backup=true to snapshot the original into .agent/backups/<timestamp>.\n"
            "- edit — Apply diff edits using old_lines/new_lines replacements. Each edit must match contiguous existing lines exactly.\n"
            "- list_directory — Return files and subdirectories for the given path.\n"
            "- create_directory — Create a new directory (including intermediate parents).\n\n"
            "Errors are explicit: file_not_found, permission_denied, invalid_path, or backup_failed. Do not retry blindly.";
        fs.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["read", "write", "edit", "list_directory", "create_directory"], "description": "Filesystem operation to perform."},
    "path": {"type": "string", "description": "Relative path within the working directory. Supports $ProjectFolder$ templates."},
    "content": {"type": "string", "description": "Full file content for write action."},
    "start_line": {"type": "integer", "description": "Optional 1-based start line for read."},
    "end_line": {"type": "integer", "description": "Optional 1-based end line for read (inclusive)."},
    "start_offset": {"type": "integer", "description": "Optional byte start offset for read."},
    "length": {"type": "integer", "description": "Optional byte length for read."},
    "create_backup": {"type": "boolean", "description": "If true, backup the original file or directory before write or edit."},
    "edits": {"type": "array", "description": "For edit action: list of {old_lines:[...], new_lines:[...]} objects. old_lines must match contiguous lines exactly."}
  },
  "required": ["action", "path"]
})";
        definitions.push_back(std::move(fs));
    }
    return definitions;
}'''
    text = text.replace(old, new)
    # Add helper functions (and CallFilesystem) before CallTool
    old = '''inline McpToolCallResult CallTool(
    const std::string& name,
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables,
    const std::string& current_agentic_mode_id = {},
    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {}) {'''
    new = '''inline bool IsValidRelativePath(const std::string& path) {
    if (path.empty()) return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.find(':') != std::string::npos) return false;
    return true;
}

inline std::filesystem::path ResolveFilesystemPath(
    const std::string& path_template,
    const std::string& working_directory_template,
    const std::vector<ProjectMcpVariableValue>& variables) {
    std::string expanded = variable_resolver::ExpandTemplate(
        working_directory_template.empty() ? "$ProjectFolder$" : working_directory_template,
        variables);
    expanded = Trim(expanded);
    if (expanded.empty()) expanded = "$ProjectFolder$";
    std::filesystem::path root(Utf8ToWide(expanded));
    std::string rel = Trim(path_template);
    rel = variable_resolver::ExpandTemplate(rel, variables);
    rel = Trim(rel);
    std::string root_utf8 = WideToUtf8(root.wstring());
    if (!root_utf8.empty() && rel.rfind(root_utf8, 0) == 0) {
        rel = rel.substr(root_utf8.size());
        while (!rel.empty() && (rel.front() == '\\\\' || rel.front() == '/')) rel = rel.substr(1);
    }
    if (rel.empty()) return root;
    std::filesystem::path target = root / Utf8ToWide(rel);
    return target;
}

inline bool ReadWholeFileUtf8(const std::filesystem::path& path, std::string* out, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error) *error = "Could not open file for reading: " + WideToUtf8(path.wstring());
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    *out = stream.str();
    return true;
}

inline bool WriteWholeFileUtf8(const std::filesystem::path& path, const std::string& content, std::string* error) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error) *error = "Could not open file for writing: " + WideToUtf8(path.wstring());
            return false;
        }
        output << content;
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Could not write file: ") + ex.what();
        return false;
    }
}

inline bool CreateFilesystemBackup(
    const std::filesystem::path& target,
    const std::filesystem::path& project_root,
    std::string* backup_rel_path_out,
    std::string* error) {
    try {
        std::error_code ec;
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        gmtime_s(&utc, &time_t_now);
        std::ostringstream ts;
        ts << std::put_time(&utc, "%Y%m%d_%H%M%S");
        const std::string timestamp = ts.str();
        std::filesystem::path backup_root = project_root / ".agent" / "backups" / Utf8ToWide(timestamp);
        std::filesystem::path rel;
        if (target.wstring().rfind(project_root.wstring(), 0) == 0) {
            rel = std::filesystem::relative(target, project_root, ec);
        }
        if (ec || rel.empty()) {
            rel = target.filename();
        }
        std::filesystem::path backup_dest = backup_root / rel;
        std::filesystem::create_directories(backup_dest.parent_path(), ec);
        if (ec) {
            if (error) *error = "Failed to create backup directory: " + ec.message();
            return false;
        }
        if (std::filesystem::is_directory(target, ec)) {
            std::filesystem::copy(target, backup_dest,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error) *error = "Failed to backup directory: " + ec.message();
                return false;
            }
        } else {
            std::filesystem::copy_file(target, backup_dest,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error) *error = "Failed to backup file: " + ec.message();
                return false;
            }
        }
        if (backup_rel_path_out) {
            *backup_rel_path_out = WideToUtf8((std::filesystem::path(".agent") / "backups" / Utf8ToWide(timestamp) / rel).wstring());
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Backup error: ") + ex.what();
        return false;
    }
}

inline McpToolCallResult CallFilesystem(
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid filesystem tool arguments: ") + ex.what());
    }
    const std::string action = Trim(args.value("action", ""));
    const std::string path_template = Trim(args.value("path", ""));
    if (action.empty()) return ErrorResult("Filesystem tool requires an action.");
    if (path_template.empty()) return ErrorResult("Filesystem tool requires a path.");
    if (!IsValidRelativePath(path_template)) return ErrorResult("Filesystem path contains invalid characters or parent-directory references (..).");
    std::filesystem::path target = ResolveFilesystemPath(path_template, settings.built_in_filesystem_working_directory, variables);
    std::error_code ec;
    const bool target_exists = std::filesystem::exists(target, ec);
    const bool target_is_dir = target_exists && std::filesystem::is_directory(target, ec);
    const bool create_backup = args.value("create_backup", false);
    std::string expanded_root = variable_resolver::ExpandTemplate(
        settings.built_in_filesystem_working_directory.empty() ? "$ProjectFolder$" : settings.built_in_filesystem_working_directory,
        variables);
    expanded_root = Trim(expanded_root);
    if (expanded_root.empty()) expanded_root = "$ProjectFolder$";
    std::filesystem::path project_root = std::filesystem::weakly_canonical(std::filesystem::path(Utf8ToWide(expanded_root)), ec);
    if (action == "read") {
        if (!target_exists) return ErrorResult("File not found: " + path_template);
        if (target_is_dir) return ErrorResult("Cannot read a directory as a file. Use list_directory instead.");
        std::string content;
        std::string read_error;
        if (!ReadWholeFileUtf8(target, &content, &read_error)) return ErrorResult(read_error);
        int start_line = args.value("start_line", 0);
        int end_line = args.value("end_line", 0);
        int start_offset = args.value("start_offset", -1);
        int length = args.value("length", -1);
        if (start_line > 0 || end_line > 0) {
            std::vector<std::string> lines;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) lines.push_back(line);
            if (start_line < 1) start_line = 1;
            if (end_line < 1 || end_line > static_cast<int>(lines.size())) end_line = static_cast<int>(lines.size());
            std::ostringstream out;
            for (int i = start_line - 1; i < end_line; ++i) {
                if (i > start_line - 1) out << "\\n";
                out << lines[i];
            }
            content = out.str();
        } else if (start_offset >= 0 && length >= 0) {
            if (start_offset < static_cast<int>(content.size())) {
                content = content.substr(start_offset, static_cast<size_t>(length));
            } else { content.clear(); }
        }
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "read"}, {"path", path_template}, {"content", content} }).dump(2);
        result.content_text = "Read file: " + path_template + "\\n\\n" + content;
        return result;
    }
    if (action == "list_directory") {
        if (!target_exists) return ErrorResult("Directory not found: " + path_template);
        if (!target_is_dir) return ErrorResult("Path is not a directory: " + path_template);
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& entry : std::filesystem::directory_iterator(target, ec)) {
            if (ec) continue;
            entries.push_back(nlohmann::json({ {"name", WideToUtf8(entry.path().filename().wstring())}, {"is_directory", entry.is_directory(ec)} }));
        }
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "list_directory"}, {"path", path_template}, {"entries", entries} }).dump(2);
        result.content_text = "Directory listing: " + path_template + "\\n" + entries.dump(2);
        return result;
    }
    if (action == "create_directory") {
        std::filesystem::create_directories(target, ec);
        if (ec) return ErrorResult("Failed to create directory: " + path_template + " — " + ec.message());
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "create_directory"}, {"path", path_template} }).dump(2);
        result.content_text = "Created directory: " + path_template;
        return result;
    }
    std::string backup_rel;
    if (create_backup && target_exists) {
        std::string backup_error;
        if (!CreateFilesystemBackup(target, project_root, &backup_rel, &backup_error)) return ErrorResult("Backup failed: " + backup_error);
    }
    if (action == "write") {
        const std::string content = args.value("content", "");
        std::string write_error;
        if (!WriteWholeFileUtf8(target, content, &write_error)) return ErrorResult(write_error);
        McpToolCallResult result;
        result.success = true;
        nlohmann::json payload = { {"tool", kFilesystemToolName}, {"success", true}, {"action", "write"}, {"path", path_template}, {"bytes_written", static_cast<int>(content.size())} };
        if (!backup_rel.empty()) payload["backup_path"] = backup_rel;
        result.raw_result_json = payload.dump(2);
        result.content_text = "Wrote file: " + path_template;
        if (!backup_rel.empty()) result.content_text += " (backup: " + backup_rel + ")";
        return result;
    }
    if (action == "edit") {
        if (!target_exists || target_is_dir) return ErrorResult("Edit target must be an existing file: " + path_template);
        std::string original;
        std::string read_error;
        if (!ReadWholeFileUtf8(target, &original, &read_error)) return ErrorResult(read_error);
        const auto edits = args.value("edits", nlohmann::json::array());
        if (!edits.is_array() || edits.empty()) return ErrorResult("Edit action requires a non-empty 'edits' array.");
        std::vector<std::string> lines;
        { std::istringstream stream(original); std::string line; while (std::getline(stream, line)) lines.push_back(line); }
        for (const auto& edit : edits) {
            if (!edit.is_object()) continue;
            const auto old_j = edit.value("old_lines", nlohmann::json::array());
            const auto new_j = edit.value("new_lines", nlohmann::json::array());
            if (!old_j.is_array() || old_j.empty()) continue;
            std::vector<std::string> old_lines; for (const auto& item : old_j) { if (item.is_string()) old_lines.push_back(item.get<std::string>()); }
            std::vector<std::string> new_lines; for (const auto& item : new_j) { if (item.is_string()) new_lines.push_back(item.get<std::string>()); }
            if (old_lines.empty()) continue;
            bool replaced = false;
            for (size_t i = 0; i + old_lines.size() <= lines.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < old_lines.size(); ++j) { if (lines[i + j] != old_lines[j]) { match = false; break; } }
                if (match) {
                    lines.erase(lines.begin() + i, lines.begin() + i + old_lines.size());
                    lines.insert(lines.begin() + i, new_lines.begin(), new_lines.end());
                    replaced = true; break;
                }
            }
            if (!replaced) return ErrorResult("Edit failed: old_lines not found in file. Ensure the lines match exactly (including whitespace).");
        }
        std::ostringstream out;
        for (size_t i = 0; i < lines.size(); ++i) { if (i > 0) out << "\\n"; out << lines[i]; }
        std::string write_error;
        if (!WriteWholeFileUtf8(target, out.str(), &write_error)) return ErrorResult(write_error);
        McpToolCallResult result;
        result.success = true;
        nlohmann::json payload = { {"tool", kFilesystemToolName}, {"success", true}, {"action", "edit"}, {"path", path_template} };
        if (!backup_rel.empty()) payload["backup_path"] = backup_rel;
        result.raw_result_json = payload.dump(2);
        result.content_text = "Edited file: " + path_template;
        if (!backup_rel.empty()) result.content_text += " (backup: " + backup_rel + ")";
        return result;
    }
    return ErrorResult("Unknown filesystem action: " + action);
}

inline McpToolCallResult CallTool(
    const std::string& name,
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables,
    const std::string& current_agentic_mode_id = {},
    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {}) {'''
    text = text.replace(old, new)
    # Hook filesystem into CallTool
    old2 = '''    if (name == kQuestionnaireToolName && IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        return CallQuestionnaire(arguments_json, questionnaire_wait);
    }
    return ErrorResult("Built-in tool is not enabled for this project: " + name);
}'''
    new2 = '''    if (name == kQuestionnaireToolName && IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        return CallQuestionnaire(arguments_json, questionnaire_wait);
    }
    if (name == kFilesystemToolName && settings.built_in_filesystem_enabled) {
        return CallFilesystem(arguments_json, settings, variables);
    }
    return ErrorResult("Built-in tool is not enabled for this project: " + name);
}'''
    text = text.replace(old2, new2)
    # Ensure <chrono> and <iomanip> includes
    if '#include <chrono>' not in text:
        text = text.replace('#include <algorithm>', '#include <algorithm>\n#include <chrono>')
    if '#include <iomanip>' not in text:
        text = text.replace('#include <fstream>', '#include <fstream>\n#include <iomanip>')
    p.write_text(text, encoding='utf-8')
    print('built_in_tools.h updated')
    return True

def modify_project_settings_dialog_cpp():
    p = pathlib.Path('src/project_settings_dialog.cpp')
    text = p.read_text(encoding='utf-8')
    # 1. Add control IDs
    old = '    kQuestionnaireModeCombo = 6485,\n\n    // RAG services section'
    new = '    kQuestionnaireModeCombo = 6485,\n    kFilesystemEnabledCheck = 6494,\n    kFilesystemAutoArchiveCheck = 6495,\n    kFilesystemWorkDirLabel = 6496,\n    kFilesystemWorkDirEdit = 6497,\n    kFilesystemNoteLabel = 6498,\n\n    // RAG services section'
    text = text.replace(old, new)
    # 2. Add member variables after questionnaire_mode_combo_
    old = '    HWND questionnaire_mode_combo_ = nullptr;\n\n      bool internal_powershell_enabled_ = false;'
    new = '    HWND questionnaire_mode_combo_ = nullptr;\n    HWND filesystem_enabled_check_ = nullptr;\n    HWND filesystem_auto_archive_check_ = nullptr;\n    HWND filesystem_workdir_label_ = nullptr;\n    HWND filesystem_workdir_edit_ = nullptr;\n    HWND filesystem_note_label_ = nullptr;\n\n    bool filesystem_enabled_ = false;\n    bool filesystem_auto_archive_ = false;\n    std::string filesystem_workdir_ = "$ProjectFolder$";\n\n      bool internal_powershell_enabled_ = false;'
    text = text.replace(old, new)
    # 3. Add control creation after questionnaire_mode_combo_ creation
    old = '        questionnaire_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,\n            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,\n            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireModeCombo), nullptr, nullptr);\n\n        // RAG services section'
    new = '''        questionnaire_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireModeCombo), nullptr, nullptr);

        filesystem_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Project Filesystem",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemEnabledCheck), nullptr, nullptr);
        filesystem_auto_archive_check_ = CreateWindowExW(0, L"BUTTON", L"Auto-archive reads/writes into Artifact/Code Memory",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemAutoArchiveCheck), nullptr, nullptr);
        filesystem_workdir_label_ = CreateWindowExW(0, L"STATIC", L"Working directory:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkDirLabel), nullptr, nullptr);
        filesystem_workdir_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"$ProjectFolder$",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkDirEdit), nullptr, nullptr);
        filesystem_note_label_ = CreateWindowExW(0, L"STATIC",
            L"Built-in filesystem replaces the MCP file server for this project. Backups go into .agent/backups.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemNoteLabel), nullptr, nullptr);

        // RAG services section'''
    text = text.replace(old, new)
    # 4. Add to font list
    old = '            questionnaire_restrict_mode_check_, questionnaire_mode_label_, questionnaire_mode_combo_,\n            rag_services_header_, rag_services_list_, rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,'
    new = '            questionnaire_restrict_mode_check_, questionnaire_mode_label_, questionnaire_mode_combo_,\n            filesystem_enabled_check_, filesystem_auto_archive_check_, filesystem_workdir_label_, filesystem_workdir_edit_, filesystem_note_label_,\n            rag_services_header_, rag_services_list_, rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,'
    text = text.replace(old, new)
    # 5. InitializeControls workdir init
    old = '''        planner_storage_folder_ = Trim(options_.built_in_planner_storage_folder).empty()
            ? std::string("$ProjectFolder$\\.agent")
            : options_.built_in_planner_storage_folder;

        questionnaire_enabled_ = options_.built_in_questionnaire_enabled;'''
    new = '''        planner_storage_folder_ = Trim(options_.built_in_planner_storage_folder).empty()
            ? std::string("$ProjectFolder$\\.agent")
            : options_.built_in_planner_storage_folder;
        filesystem_workdir_ = Trim(options_.built_in_filesystem_working_directory).empty()
            ? std::string("$ProjectFolder$")
            : options_.built_in_filesystem_working_directory;

        questionnaire_enabled_ = options_.built_in_questionnaire_enabled;'''
    text = text.replace(old, new)
    # 6. InitializeControls checkbox state
    old = '''        PopulateQuestionnaireModeCombo(options_.questionnaire_allowed_mode_id);
        RefreshQuestionnaireControls();
        RefreshInternalToolsList(false);'''
    new = '''        PopulateQuestionnaireModeCombo(options_.questionnaire_allowed_mode_id);
        RefreshQuestionnaireControls();
        filesystem_enabled_ = options_.built_in_filesystem_enabled;
        filesystem_auto_archive_ = options_.built_in_filesystem_auto_archive;
        filesystem_workdir_ = Trim(options_.built_in_filesystem_working_directory).empty()
            ? std::string("$ProjectFolder$")
            : options_.built_in_filesystem_working_directory;
        if (filesystem_enabled_) {
            CheckDlgButton(scroll_content_host_, kFilesystemEnabledCheck, BST_CHECKED);
        }
        if (filesystem_auto_archive_) {
            CheckDlgButton(scroll_content_host_, kFilesystemAutoArchiveCheck, BST_CHECKED);
        }
        SetWindowTextW(filesystem_workdir_edit_, Utf8ToWide(filesystem_workdir_).c_str());
        RefreshInternalToolsList(false);'''
    text = text.replace(old, new)
    # 7. LayoutControls move windows
    old = '''        MoveWindow(questionnaire_mode_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 75), questionnaire_mode_label_w, label_height, TRUE);
        MoveWindow(questionnaire_mode_combo_, internal_settings_x + panel_pad + questionnaire_mode_label_w + gutter, tool_y + Scale(hwnd_, 72), questionnaire_mode_combo_w, Scale(hwnd_, 180), TRUE);
        SendMessageW(questionnaire_mode_combo_, CB_SETDROPPEDWIDTH, questionnaire_mode_combo_w, 0);

        // RAG services section'''
    new = '''        MoveWindow(questionnaire_mode_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 75), questionnaire_mode_label_w, label_height, TRUE);
        MoveWindow(questionnaire_mode_combo_, internal_settings_x + panel_pad + questionnaire_mode_label_w + gutter, tool_y + Scale(hwnd_, 72), questionnaire_mode_combo_w, Scale(hwnd_, 180), TRUE);
        SendMessageW(questionnaire_mode_combo_, CB_SETDROPPEDWIDTH, questionnaire_mode_combo_w, 0);

        MoveWindow(filesystem_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(filesystem_auto_archive_check_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(filesystem_workdir_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 50), Scale(hwnd_, 110), label_height, TRUE);
        MoveWindow(filesystem_workdir_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 112), tool_y + Scale(hwnd_, 47), internal_settings_w - panel_pad * 2 - Scale(hwnd_, 112), Scale(hwnd_, 22), TRUE);
        MoveWindow(filesystem_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 72), internal_settings_w - panel_pad * 2, label_height, TRUE);

        // RAG services section'''
    text = text.replace(old, new)
    # 8. OnCommand handlers
    old = '''        case kQuestionnaireModeCombo:
            break;'''
    new = '''        case kQuestionnaireModeCombo:
            break;
        case kFilesystemEnabledCheck:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                filesystem_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kFilesystemEnabledCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kFilesystemAutoArchiveCheck:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                filesystem_auto_archive_ = (IsDlgButtonChecked(scroll_content_host_, kFilesystemAutoArchiveCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;'''
    text = text.replace(old, new)
    # 9. CollectCurrentSettings
    old = '            result.questionnaire_allowed_mode_id = std::move(q_mode_id);\n          }\n          const std::wstring timeout_text = TrimWide(GetWindowTextString(model_timeout_edit_));'
    new = '''            result.questionnaire_allowed_mode_id = std::move(q_mode_id);
          }
          result.built_in_filesystem_enabled = filesystem_enabled_;
          result.built_in_filesystem_auto_archive = filesystem_auto_archive_;
          result.built_in_filesystem_working_directory = filesystem_workdir_;
          const std::wstring timeout_text = TrimWide(GetWindowTextString(model_timeout_edit_));'''
    text = text.replace(old, new)
    # 10. SelectInternalTool hide
    old = '''        ShowWindow(questionnaire_enabled_check_, SW_HIDE);
        ShowWindow(questionnaire_max_options_label_, SW_HIDE);
        ShowWindow(questionnaire_max_options_edit_, SW_HIDE);
        ShowWindow(questionnaire_restrict_mode_check_, SW_HIDE);
        ShowWindow(questionnaire_mode_label_, SW_HIDE);
        ShowWindow(questionnaire_mode_combo_, SW_HIDE);
    }'''
    new = '''        ShowWindow(questionnaire_enabled_check_, SW_HIDE);
        ShowWindow(questionnaire_max_options_label_, SW_HIDE);
        ShowWindow(questionnaire_max_options_edit_, SW_HIDE);
        ShowWindow(questionnaire_restrict_mode_check_, SW_HIDE);
        ShowWindow(questionnaire_mode_label_, SW_HIDE);
        ShowWindow(questionnaire_mode_combo_, SW_HIDE);
        ShowWindow(filesystem_enabled_check_, SW_HIDE);
        ShowWindow(filesystem_auto_archive_check_, SW_HIDE);
        ShowWindow(filesystem_workdir_label_, SW_HIDE);
        ShowWindow(filesystem_workdir_edit_, SW_HIDE);
        ShowWindow(filesystem_note_label_, SW_HIDE);
    }'''
    text = text.replace(old, new)
    # 11. RefreshInternalToolsList add item and bound
    old = '''        ListBox_AddString(internal_tools_list_,
            (std::wstring(completion_driver_enabled_ ? L"[✓] " : L"[ ] ") + L"Completion Driver").c_str());

        int sel = selected_internal_tool_index_;
        if (sel < 0 || sel > 4) sel = 0;'''
    new = '''        ListBox_AddString(internal_tools_list_,
            (std::wstring(completion_driver_enabled_ ? L"[✓] " : L"[ ] ") + L"Completion Driver").c_str());

        ListBox_AddString(internal_tools_list_,
            (std::wstring(filesystem_enabled_ ? L"[✓] " : L"[ ] ") + L"Project Filesystem").c_str());

        int sel = selected_internal_tool_index_;
        if (sel < 0 || sel > 5) sel = 0;'''
    text = text.replace(old, new)
    # 12. SelectInternalTool bound
    old = '    void SelectInternalTool(int index) {\n        if (index < 0 || index > 4) return;\n        SaveCurrentInternalToolSettings();'
    new = '    void SelectInternalTool(int index) {\n        if (index < 0 || index > 5) return;\n        SaveCurrentInternalToolSettings();'
    text = text.replace(old, new)
    # 13. SaveCurrentInternalToolSettings add filesystem
    old = '''        if (selected_internal_tool_index_ == 3 && questionnaire_enabled_check_) {
            questionnaire_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);
        }
    }'''
    new = '''        if (selected_internal_tool_index_ == 3 && questionnaire_enabled_check_) {
            questionnaire_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);
        }
        if (selected_internal_tool_index_ == 5 && filesystem_enabled_check_) {
            filesystem_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kFilesystemEnabledCheck) == BST_CHECKED);
            filesystem_auto_archive_ =
                (IsDlgButtonChecked(scroll_content_host_, kFilesystemAutoArchiveCheck) == BST_CHECKED);
            filesystem_workdir_ = Trim(WideToUtf8(GetWindowTextString(filesystem_workdir_edit_)));
            if (filesystem_workdir_.empty()) {
                filesystem_workdir_ = "$ProjectFolder$";
            }
        }
    }'''
    text = text.replace(old, new)
    # 14. ShowInternalToolSettings add index 5 case before panel_has_content block and title switch
    old = '''        } else if (index == 4) {
            panel_has_content = true;
            ShowWindow(completion_driver_enabled_check_, SW_SHOW);
            ShowWindow(completion_driver_modes_label_, SW_SHOW);
            ShowWindow(completion_driver_modes_list_, SW_SHOW);
            ShowWindow(completion_driver_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kCompletionDriverEnabledCheck,
                completion_driver_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            RefreshCompletionDriverModesList();
            EnableWindow(completion_driver_modes_label_, completion_driver_enabled_ ? TRUE : FALSE);
            EnableWindow(completion_driver_modes_list_, completion_driver_enabled_ ? TRUE : FALSE);
        }
        if (panel_has_content) {
            std::wstring title;
            switch (index) {
            case 0: title = L"PowerShell Settings"; break;
            case 1: title = L"Artifact/Code Memory Settings"; break;
            case 2: title = L"Planner Settings"; break;
            case 3: title = L"Questionnaire Settings"; break;
            case 4: title = L"Completion Driver Settings"; break;
            default: title = L"Tool Settings"; break;
            }
            SetWindowTextW(internal_tool_settings_panel_, title.c_str());
        }'''
    new = '''        } else if (index == 4) {
            panel_has_content = true;
            ShowWindow(completion_driver_enabled_check_, SW_SHOW);
            ShowWindow(completion_driver_modes_label_, SW_SHOW);
            ShowWindow(completion_driver_modes_list_, SW_SHOW);
            ShowWindow(completion_driver_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kCompletionDriverEnabledCheck,
                completion_driver_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            RefreshCompletionDriverModesList();
            EnableWindow(completion_driver_modes_label_, completion_driver_enabled_ ? TRUE : FALSE);
            EnableWindow(completion_driver_modes_list_, completion_driver_enabled_ ? TRUE : FALSE);
        } else if (index == 5) {
            panel_has_content = true;
            ShowWindow(filesystem_enabled_check_, SW_SHOW);
            ShowWindow(filesystem_auto_archive_check_, SW_SHOW);
            ShowWindow(filesystem_workdir_label_, SW_SHOW);
            ShowWindow(filesystem_workdir_edit_, SW_SHOW);
            ShowWindow(filesystem_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kFilesystemEnabledCheck,
                filesystem_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(scroll_content_host_, kFilesystemAutoArchiveCheck,
                filesystem_auto_archive_ ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(filesystem_workdir_edit_, Utf8ToWide(filesystem_workdir_).c_str());
        }
        if (panel_has_content) {
            std::wstring title;
            switch (index) {
            case 0: title = L"PowerShell Settings"; break;
            case 1: title = L"Artifact/Code Memory Settings"; break;
            case 2: title = L"Planner Settings"; break;
            case 3: title = L"Questionnaire Settings"; break;
            case 4: title = L"Completion Driver Settings"; break;
            case 5: title = L"Project Filesystem Settings"; break;
            default: title = L"Tool Settings"; break;
            }
            SetWindowTextW(internal_tool_settings_panel_, title.c_str());
        }'''
    text = text.replace(old, new)
    # 15. ToggleInternalToolEnabled bounds and case 5
    old = '''    void ToggleInternalToolEnabled(int index) {
        if (index < 0 || index > 4) return;
        SaveCurrentInternalToolSettings();
        selected_internal_tool_index_ = index;
        if (index == 0) {
            internal_powershell_enabled_ = !internal_powershell_enabled_;
        } else if (index == 1) {
            if (!ArtifactMemoryForcedByLayer0()) {
                internal_artifact_memory_enabled_ = !internal_artifact_memory_enabled_;
            }
        } else if (index == 2) {
            planner_enabled_ = !planner_enabled_;
        } else if (index == 3) {
            questionnaire_enabled_ = !questionnaire_enabled_;
        } else if (index == 4) {
            completion_driver_enabled_ = !completion_driver_enabled_;
        }
        RefreshInternalToolsList(false);
    }'''
    new = '''    void ToggleInternalToolEnabled(int index) {
        if (index < 0 || index > 5) return;
        SaveCurrentInternalToolSettings();
        selected_internal_tool_index_ = index;
        if (index == 0) {
            internal_powershell_enabled_ = !internal_powershell_enabled_;
        } else if (index == 1) {
            if (!ArtifactMemoryForcedByLayer0()) {
                internal_artifact_memory_enabled_ = !internal_artifact_memory_enabled_;
            }
        } else if (index == 2) {
            planner_enabled_ = !planner_enabled_;
        } else if (index == 3) {
            questionnaire_enabled_ = !questionnaire_enabled_;
        } else if (index == 4) {
            completion_driver_enabled_ = !completion_driver_enabled_;
        } else if (index == 5) {
            filesystem_enabled_ = !filesystem_enabled_;
        }
        RefreshInternalToolsList(false);
    }'''
    text = text.replace(old, new)
    p.write_text(text, encoding='utf-8')
    print('project_settings_dialog.cpp updated')
    return True

if __name__ == '__main__':
    ok = True
    ok &= modify_types_h()
    ok &= modify_storage_cpp()
    ok &= modify_main_cpp()
    ok &= modify_web_server_cpp()
    ok &= modify_project_settings_dialog_h()
    ok &= modify_built_in_tools_h()
    ok &= modify_project_settings_dialog_cpp()
    if not ok:
        sys.exit(1)
