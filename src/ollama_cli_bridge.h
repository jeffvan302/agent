#pragma once

#include <optional>
#include <string>
#include <vector>

struct OllamaCliLaunchOptions {
    int num_threads = 0;
    bool no_gpu = false;
    int gpu_layers = 0;
    int context_length = 0;
};

struct OllamaCliLaunchSpec {
    std::wstring executable_path;
    std::wstring command_line;
    std::vector<wchar_t> environment_block;
};

struct OllamaCliCommandOutput {
    unsigned long exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

class OllamaTerminalSanitizer {
public:
    void Append(const char* data, size_t length);
    std::string VisibleText() const;

private:
    bool in_escape_ = false;
    std::string completed_lines_;
    std::string current_line_;
};

std::optional<std::wstring> FindOllamaCliPath();
bool BuildOllamaCliLaunchSpec(const std::vector<std::wstring>& args,
                              const OllamaCliLaunchOptions& options,
                              OllamaCliLaunchSpec* spec,
                              std::string* error);
bool RunOllamaCliCommand(const std::vector<std::wstring>& args,
                         const std::string& stdin_text,
                         const OllamaCliLaunchOptions& options,
                         OllamaCliCommandOutput* output,
                         std::string* error);
std::string SanitizeOllamaTerminalOutput(const std::string& raw_text);
