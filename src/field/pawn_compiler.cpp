#include "field/pawn_compiler.h"
#include "formats/amx.h"
#include <atomic>
#include <chrono>
#include <regex>
#include <sstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
extern char **environ;
#endif
namespace studio {
namespace {
struct Scratch {
    std::filesystem::path directory;
    ~Scratch() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
#ifdef _WIN32
std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (auto c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"')
            out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
int run(const std::filesystem::path &executable, const std::vector<std::filesystem::path> &args,
        const std::filesystem::path &log) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    auto output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    require(output != INVALID_HANDLE_VALUE, "Cannot create Pawn compiler log");
    auto input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(output);
        throw std::runtime_error("Cannot open compiler input");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = startup.hStdError = output;
    PROCESS_INFORMATION process{};
    auto command = quote(executable.wstring());
    for (const auto &arg : args)
        command += L" " + quote(arg.wstring());
    auto started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, log.parent_path().c_str(), &startup, &process);
    CloseHandle(input);
    CloseHandle(output);
    require(started != FALSE, "Could not start the configured Pawn compiler");
    auto status = WaitForSingleObject(process.hProcess, 120000);
    if (status != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Pawn compilation timed out or could not be monitored");
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return int(exit_code);
}
#else
int run(const std::filesystem::path &executable, const std::vector<std::filesystem::path> &args,
        const std::filesystem::path &log) {
    std::vector<std::string> owned{executable.string()};
    for (const auto &arg : args)
        owned.push_back(arg.string());
    std::vector<char *> argv;
    for (auto &arg : owned)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    require(posix_spawn_file_actions_init(&actions) == 0, "Cannot initialize compiler process");
    int error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(),
                                                 O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (!error)
        error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    if (!error)
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    pid_t child = 0;
    if (!error)
        error = posix_spawn(&child, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    require(error == 0, "Could not start the configured Pawn compiler");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    int status = 0;
    for (;;) {
        auto result = waitpid(child, &status, WNOHANG);
        if (result == child)
            break;
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("Cannot monitor Pawn compiler process");
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            throw std::runtime_error("Pawn compilation timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif
}
PawnCompileResult compile_conversation(const ConversationPawn &source,
                                       const PawnCompilerSettings &settings) {
    auto executable = std::filesystem::absolute(settings.executable);
    auto includes = std::filesystem::absolute(settings.include_directory);
    require(!settings.executable.empty() && std::filesystem::is_regular_file(executable),
            "Choose a compatible Pawn compiler executable");
    require(!settings.include_directory.empty() &&
                std::filesystem::is_regular_file(includes / "usum.inc"),
            "Choose the Pawn support include directory");
    require(!settings.scratch_directory.empty(),
            "A project scratch directory is required for compilation");
    require(!source.source.empty() && source.source.size() <= 1024 * 1024,
            "Pawn source is empty or too large");
    auto root = std::filesystem::absolute(settings.scratch_directory);
    std::filesystem::create_directories(root);
    Scratch scratch;
    static std::atomic<unsigned> sequence{0};
    for (;;) {
        auto name = "pawn-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(sequence++);
        auto candidate = root / name;
        if (std::filesystem::create_directory(candidate)) {
            scratch.directory = std::move(candidate);
            break;
        }
    }
    auto save = [&](const char *name, const std::string &s) {
        write_new_file(scratch.directory / name,
                       View(reinterpret_cast<const std::uint8_t *>(s.data()), s.size()));
    };
    save("main.pwn", source.source);
    save("messages.inc", source.definitions);
    auto output = scratch.directory / "main.amx";
    auto log = scratch.directory / "compile.log";
    PawnCompileResult result;
    auto prefixed = [](const char *prefix, const std::filesystem::path &path) {
        auto value = std::filesystem::path(prefix);
        value += path.native();
        return value;
    };
    result.exit_code = run(executable,
                           {scratch.directory / "main.pwn", "-d0", prefixed("-o", output),
                            prefixed("-i", scratch.directory), prefixed("-i", includes)},
                           log);
    result.output = text(read_file(log));
    std::istringstream lines(result.output);
    const std::regex diagnostic(
        R"(^(.+)\(([0-9]+)(?: -- [0-9]+)?\)\s*:\s*(fatal error|error|warning)\s+[0-9]+:\s*(.*)$)");
    for (std::string line; std::getline(lines, line);) {
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::smatch match;
        if (!std::regex_match(line, match, diagnostic))
            continue;
        PawnDiagnostic item;
        item.file = std::filesystem::path(match[1].str()).filename().string();
        item.line = unsigned(std::stoul(match[2].str()));
        item.severity = match[3];
        item.message = match[4];
        if (item.file == "main.pwn") {
            auto step = source.line_steps.find(item.line);
            if (step != source.line_steps.end())
                item.step = step->second;
        }
        result.diagnostics.push_back(std::move(item));
    }
    if (result.exit_code == 0) {
        require(std::filesystem::is_regular_file(output), "Pawn compiler produced no program");
        result.program = read_file(output);
        decode_field_amx(result.program);
    }
    return result;
}
}
