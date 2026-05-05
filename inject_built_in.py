import pathlib

p = pathlib.Path('src/built_in_tools.h')
text = p.read_text(encoding='utf-8')

# 1. Add includes
if '#include <chrono>' not in text:
    text = text.replace('#include <algorithm>', '#include <algorithm>\n#include <chrono>')
if '#include <iomanip>' not in text:
    text = text.replace('#include <fstream>', '#include <fstream>\n#include <iomanip>')

# 2. Insert filesystem helpers before CallTool
insertion_point = 'inline McpToolCallResult CallTool(\n    const std::string& name,\n    const std::string& arguments_json,\n    const ProjectSettings& settings,\n    const std::vector<ProjectMcpVariableValue>& variables,\n    const std::string& current_agentic_mode_id = {},\n    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {}) {'

new_code = '''inline bool IsValidRelativePath(const std::string& path) {
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

'''

if insertion_point in text:
    text = text.replace(insertion_point, new_code + insertion_point)
    print('inserted helpers')
else:
    print('insertion point not found')

# 3. Hook into CallTool dispatcher
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
if old2 in text:
    text = text.replace(old2, new2)
    print('hooked dispatcher')
else:
    print('dispatcher hook not found')

p.write_text(text, encoding='utf-8')
print('built_in_tools.h final write done')
