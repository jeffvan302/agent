#pragma once

#include "types.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace variable_resolver {

inline std::string ToLookupKey(std::string name)
{
    name = Trim(std::move(name));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return name;
}

inline bool IsVariableNameCandidate(const std::string& name)
{
    if (name.empty()) return false;
    for (const unsigned char ch : name) {
        if (std::isspace(ch) || ch == '$' || ch == '<' || ch == '>') {
            return false;
        }
    }
    return true;
}

inline std::string UnderscoreSpaces(std::string value)
{
    for (char& ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return value;
}

inline void UpsertValue(std::vector<ProjectMcpVariableValue>& values,
                        const std::string& name,
                        const std::string& value)
{
    const std::string lookup = ToLookupKey(name);
    if (lookup.empty()) return;
    for (auto& existing : values) {
        if (ToLookupKey(existing.name) == lookup) {
            existing.name = name;
            existing.value = value;
            return;
        }
    }
    values.push_back({name, value});
}

inline void UpsertValue(std::vector<ProjectMcpVariableValue>& values,
                        const ProjectMcpVariableValue& value)
{
    const std::string lookup = ToLookupKey(value.name);
    if (lookup.empty()) return;
    for (auto& existing : values) {
        if (ToLookupKey(existing.name) == lookup) {
            existing = value;
            return;
        }
    }
    values.push_back(value);
}

inline std::optional<std::string> FindValue(
    const std::vector<ProjectMcpVariableValue>& values,
    const std::string& name)
{
    const std::string lookup = ToLookupKey(name);
    if (lookup.empty()) return std::nullopt;
    const auto it = std::find_if(values.begin(), values.end(),
        [&](const ProjectMcpVariableValue& value) {
            return ToLookupKey(value.name) == lookup;
        });
    if (it == values.end()) return std::nullopt;
    return it->value;
}

inline std::vector<ProjectMcpVariableValue> MergeValues(
    const std::vector<ProjectMcpVariableValue>& first,
    const std::vector<ProjectMcpVariableValue>& second)
{
    std::vector<ProjectMcpVariableValue> merged;
    for (const auto& value : first) {
        UpsertValue(merged, value);
    }
    for (const auto& value : second) {
        UpsertValue(merged, value);
    }
    return merged;
}

inline std::string BuildScopeKey(const std::vector<ProjectMcpVariableValue>& values)
{
    if (values.empty()) return {};

    std::vector<std::pair<std::string, std::string>> sorted;
    for (const auto& value : values) {
        const std::string key = ToLookupKey(value.name);
        if (!key.empty()) {
            sorted.push_back({key, value.value});
        }
    }
    if (sorted.empty()) return {};

    std::sort(sorted.begin(), sorted.end());
    std::ostringstream stream;
    for (const auto& item : sorted) {
        stream << item.first << '=' << item.second << '\n';
    }
    return std::to_string(std::hash<std::string>{}(stream.str()));
}

inline std::string ExpandVariables(
    const std::string& text,
    const std::function<std::string(const std::string&)>& get_value)
{
    std::string output;
    output.reserve(text.size());

    for (size_t index = 0; index < text.size();) {
        if (text[index] != '$') {
            output.push_back(text[index++]);
            continue;
        }

        size_t token_end = std::string::npos;
        std::string raw_name;
        bool angle_form = false;

        if (index + 1 < text.size() && text[index + 1] == '<') {
            const size_t close = text.find('>', index + 2);
            if (close != std::string::npos) {
                angle_form = true;
                raw_name = text.substr(index + 2, close - (index + 2));
                token_end = close + 1;
                if (token_end < text.size() && text[token_end] == '$') {
                    ++token_end;
                }
            }
        } else {
            const size_t close = text.find('$', index + 1);
            if (close != std::string::npos) {
                raw_name = text.substr(index + 1, close - (index + 1));
                token_end = close + 1;
            }
        }

        raw_name = Trim(raw_name);
        if (token_end == std::string::npos || !IsVariableNameCandidate(raw_name)) {
            output.push_back(text[index++]);
            continue;
        }

        bool underscore_spaces = false;
        if (!raw_name.empty() && raw_name.back() == '_') {
            underscore_spaces = true;
            raw_name.pop_back();
            raw_name = Trim(raw_name);
        }

        if (!IsVariableNameCandidate(raw_name)) {
            output.append(text.substr(index, token_end - index));
            index = token_end;
            continue;
        }

        std::string replacement = get_value(raw_name);
        if (underscore_spaces) {
            replacement = UnderscoreSpaces(std::move(replacement));
        }
        output += replacement;
        index = token_end;

        (void)angle_form;
    }

    return output;
}

inline std::vector<ProjectMcpVariableValue> ResolveValues(
    const std::vector<ProjectMcpVariableValue>& input_values)
{
    std::unordered_map<std::string, ProjectMcpVariableValue> source;
    for (const auto& value : input_values) {
        const std::string key = ToLookupKey(value.name);
        if (!key.empty()) {
            source[key] = value;
        }
    }

    std::unordered_map<std::string, std::string> cache;
    std::set<std::string> visiting;

    std::function<std::string(const std::string&)> resolve_name =
        [&](const std::string& name) -> std::string {
            const std::string key = ToLookupKey(name);
            if (key.empty()) return {};

            const auto cached = cache.find(key);
            if (cached != cache.end()) return cached->second;

            const auto item = source.find(key);
            if (item == source.end()) {
                cache[key] = "";
                return "";
            }
            if (visiting.find(key) != visiting.end()) {
                return "";
            }

            visiting.insert(key);
            const std::string resolved = ExpandVariables(
                item->second.value,
                [&](const std::string& nested_name) {
                    return resolve_name(nested_name);
                });
            visiting.erase(key);
            cache[key] = resolved;
            return resolved;
        };

    std::vector<ProjectMcpVariableValue> output;
    for (const auto& item : source) {
        ProjectMcpVariableValue value = item.second;
        value.value = resolve_name(item.second.name);
        output.push_back(std::move(value));
    }
    std::sort(output.begin(), output.end(),
              [](const ProjectMcpVariableValue& left, const ProjectMcpVariableValue& right) {
                  return ToLookupKey(left.name) < ToLookupKey(right.name);
              });
    return output;
}

inline std::string ExpandTemplate(
    const std::string& text,
    const std::vector<ProjectMcpVariableValue>& values)
{
    const auto resolved = ResolveValues(values);
    std::unordered_map<std::string, std::string> lookup;
    for (const auto& value : resolved) {
        lookup[ToLookupKey(value.name)] = value.value;
    }
    return ExpandVariables(text, [&](const std::string& name) {
        const auto it = lookup.find(ToLookupKey(name));
        return it == lookup.end() ? std::string() : it->second;
    });
}

inline void EnsureFolderVariables(
    const std::vector<ProjectMcpVariableValue>& values,
    const std::vector<McpServerVariable>& definitions)
{
    const auto resolved = ResolveValues(values);
    std::unordered_map<std::string, std::string> lookup;
    for (const auto& value : resolved) {
        lookup[ToLookupKey(value.name)] = value.value;
    }

    for (const auto& definition : definitions) {
        if (definition.kind != McpVariableKind::Folder) continue;
        const auto it = lookup.find(ToLookupKey(definition.name));
        if (it == lookup.end() || Trim(it->second).empty()) continue;

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(Utf8ToWide(it->second)), ec);
    }
}

inline std::vector<McpServerVariable> MergeDefinitions(
    const std::vector<McpServerVariable>& first,
    const std::vector<McpServerVariable>& second)
{
    std::vector<McpServerVariable> merged;
    auto add = [&](const McpServerVariable& variable) {
        const std::string key = ToLookupKey(variable.name);
        if (key.empty()) return;
        const auto it = std::find_if(merged.begin(), merged.end(),
            [&](const McpServerVariable& existing) {
                return ToLookupKey(existing.name) == key;
            });
        if (it == merged.end()) {
            merged.push_back(variable);
        } else {
            *it = variable;
        }
    };
    for (const auto& variable : first) add(variable);
    for (const auto& variable : second) add(variable);
    return merged;
}

}  // namespace variable_resolver
