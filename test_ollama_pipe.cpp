#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

std::string ReadHandleToString(HANDLE handle, DWORD timeout_ms = 5000) {
    std::string output;
    char buffer[4096];
    DWORD read = 0;
    DWORD total_wait = 0;
    const DWORD poll_interval = 50;
    while (total_wait < timeout_ms) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        if (available > 0) {
            DWORD to_read = (available > sizeof(buffer)) ? sizeof(buffer) : available;
            if (ReadFile(handle, buffer, to_read, &read, nullptr) && read > 0) {
                output.append(buffer, buffer + read);
            }
        } else {
            Sleep(poll_interval);
            total_wait += poll_interval;
        }
    }
    return output;
}

int main() {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdin_r = nullptr, stdin_w = nullptr;
    HANDLE stdout_r = nullptr, stdout_w = nullptr;
    HANDLE stderr_r = nullptr, stderr_w = nullptr;

    CreatePipe(&stdin_r, &stdin_w, &sa, 0);
    SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&stdout_r, &stdout_w, &sa, 0);
    SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&stderr_r, &stderr_w, &sa, 0);
    SetHandleInformation(stderr_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = stdin_r;
    si.hStdOutput = stdout_w;
    si.hStdError = stderr_w;
    si.wShowWindow = SW_HIDE;

    std::wstring cmd = L"\"C:\\Users\\TheunisvanNiekerk\\AppData\\Local\\Programs\\Ollama\\ollama.exe\" run phi4 --nowordwrap";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
        return 1;
    }

    CloseHandle(stdin_r);
    CloseHandle(stdout_w);
    CloseHandle(stderr_w);
    CloseHandle(pi.hThread);

    std::cout << "Process started. Waiting 8s for startup output...\n";
    Sleep(8000);

    std::string out = ReadHandleToString(stdout_r, 2000);
    std::string err = ReadHandleToString(stderr_r, 2000);

    std::cout << "--- STDOUT (len=" << out.size() << ") ---\n";
    for (char c : out) {
        if (c == '\n') std::cout << "\\n\n";
        else if (c == '\r') std::cout << "\\r";
        else if (c == '\x1b') std::cout << "\\x1b";
        else if (c == ' ') std::cout << '_';
        else std::cout << c;
    }
    std::cout << "\n--- STDERR ---\n" << err << "\n";

    std::cout << "Sending prompt...\n";
    std::string prompt = "\"\"\"\nhello\n\"\"\"\n";
    DWORD written = 0;
    WriteFile(stdin_w, prompt.data(), static_cast<DWORD>(prompt.size()), &written, nullptr);

    std::cout << "Waiting 8s for response...\n";
    Sleep(8000);

    out = ReadHandleToString(stdout_r, 5000);
    err = ReadHandleToString(stderr_r, 5000);

    std::cout << "--- STDOUT (len=" << out.size() << ") ---\n";
    for (char c : out) {
        if (c == '\n') std::cout << "\\n\n";
        else if (c == '\r') std::cout << "\\r";
        else if (c == '\x1b') std::cout << "\\x1b";
        else if (c == ' ') std::cout << '_';
        else std::cout << c;
    }
    std::cout << "\n--- STDERR ---\n" << err << "\n";

    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(stdin_w);
    CloseHandle(stdout_r);
    CloseHandle(stderr_r);
    return 0;
}
