// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include "obs_whisperbleep/runtime/persistent_process_runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace obs_whisperbleep::runtime {

namespace {

using Clock = std::chrono::steady_clock;

std::chrono::milliseconds remaining_until(const Clock::time_point deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
  return std::max(remaining, std::chrono::milliseconds::zero());
}

}  // namespace

class PersistentWhisperProcessRunner::Impl final {
 public:
  explicit Impl(PersistentWhisperProcessRunnerConfig config)
      : config_(std::move(config)) {
    if (config_.response_timeout <= std::chrono::milliseconds::zero()) {
      config_.response_timeout = std::chrono::milliseconds(30'000);
    }
    if (config_.shutdown_timeout < std::chrono::milliseconds::zero()) {
      config_.shutdown_timeout = std::chrono::milliseconds::zero();
    }
    if (config_.max_response_bytes == 0) {
      config_.max_response_bytes = 4U * 1024U * 1024U;
    }
    if (config_.max_error_output_bytes == 0) {
      config_.max_error_output_bytes = 16U * 1024U;
    }
  }

  ~Impl() { stop(); }

  [[nodiscard]] WhisperProcessResult run(
      const WhisperProcessRequest& request) {
    std::lock_guard<std::mutex> lock(request_mutex_);
    WhisperProcessResult result;

    if (request.command.empty() || request.command.front().empty()) {
      return fail_result(result, "Whisper bridge command is empty");
    }

    if (!started_) {
      if (!start_locked(request.command, result)) {
        return result;
      }
    } else if (command_ != request.command) {
      return fail_result(
          result,
          "Whisper bridge command changed while its persistent process was running");
    }

    return transact_locked(request.input_line);
  }

  void stop() noexcept {
    std::lock_guard<std::mutex> lock(request_mutex_);
    stop_locked();
  }

  [[nodiscard]] bool running() const noexcept {
    std::lock_guard<std::mutex> lock(request_mutex_);
    return is_running_locked();
  }

  [[nodiscard]] std::string last_error() const {
    std::lock_guard<std::mutex> lock(request_mutex_);
    return last_error_;
  }

 private:
  WhisperProcessResult fail_result(WhisperProcessResult result,
                                   const std::string& message) {
    last_error_ = message;
    result.error_output = message;
    const std::string stderr_output = take_stderr_locked();
    if (!stderr_output.empty()) {
      result.error_output += ": ";
      result.error_output += stderr_output;
    }
    return result;
  }

  std::string take_stderr_locked() {
    std::lock_guard<std::mutex> lock(stderr_mutex_);
    std::string output = std::move(stderr_output_);
    stderr_output_.clear();
    return output;
  }

  void append_stderr(const char* data, const std::size_t size) {
    if (data == nullptr || size == 0) {
      return;
    }
    std::lock_guard<std::mutex> lock(stderr_mutex_);
    if (stderr_output_.size() >= config_.max_error_output_bytes) {
      return;
    }
    const std::size_t remaining = config_.max_error_output_bytes - stderr_output_.size();
    stderr_output_.append(data, std::min(size, remaining));
  }

  WhisperProcessResult transact_locked(const std::string& input_line) {
    WhisperProcessResult result;
    result.started = true;
    result.exit_code = -1;

    if (!is_running_locked()) {
      result.exit_code = process_exit_code_locked();
      const WhisperProcessResult failure =
          fail_result(result, "Whisper bridge process is no longer running");
      stop_locked();
      return failure;
    }

    std::string wire = input_line;
    if (wire.empty() || wire.back() != '\n') {
      wire.push_back('\n');
    }
    const auto deadline = Clock::now() + config_.response_timeout;
    if (!write_line_locked(wire, deadline)) {
      result.exit_code = process_exit_code_locked();
      const WhisperProcessResult failure = fail_result(
          result, last_error_.empty() ? "Unable to write to Whisper bridge process"
                                      : last_error_);
      stop_locked();
      return failure;
    }

    std::string response;
    if (!read_line_locked(response, deadline)) {
      result.exit_code = process_exit_code_locked();
      const std::string error = last_error_.empty()
                                    ? "Unable to read from Whisper bridge process"
                                    : last_error_;
      stop_locked();
      result.exit_code = result.exit_code == -1 ? 124 : result.exit_code;
      return fail_result(result, error);
    }

    result.output_lines.push_back(std::move(response));
    result.error_output = take_stderr_locked();
    last_error_.clear();
    return result;
  }

#ifdef _WIN32
  static std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) {
      return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             value.data(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0);
    if (required <= 0) {
      return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            required) <= 0) {
      return {};
    }
    return result;
  }

  static std::wstring quote_windows_argument(const std::wstring& value) {
    if (!value.empty() && value.find_first_of(L" \t\"") == std::wstring::npos) {
      return value;
    }
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
      if (character == L'\\') {
        ++backslashes;
      } else if (character == L'"') {
        result.append(backslashes * 2U + 1U, L'\\');
        result.push_back(L'"');
        backslashes = 0;
      } else {
        result.append(backslashes, L'\\');
        result.push_back(character);
        backslashes = 0;
      }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
  }

  static std::string windows_error(const char* operation) {
    const DWORD code = GetLastError();
    LPWSTR message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring wide = length == 0 ? L"unknown Windows error" :
                                       std::wstring(message, length);
    if (message != nullptr) {
      LocalFree(message);
    }
    std::string narrow;
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                              static_cast<int>(wide.size()),
                                              nullptr, 0, nullptr, nullptr);
    if (required > 0) {
      narrow.resize(static_cast<std::size_t>(required));
      WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                          narrow.data(), required, nullptr, nullptr);
    } else {
      narrow = "unknown Windows error";
    }
    return std::string(operation) + " failed: " + narrow;
  }

  bool start_locked(const std::vector<std::string>& command,
                    WhisperProcessResult& result) {
    std::vector<std::wstring> wide_command;
    wide_command.reserve(command.size());
    for (const std::string& argument : command) {
      const std::wstring converted = utf8_to_wide(argument);
      if (converted.empty() && !argument.empty()) {
        result = fail_result(result, "Whisper bridge command contains invalid UTF-8");
        return false;
      }
      wide_command.push_back(converted);
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    HANDLE child_stderr_write = nullptr;
    if (!CreatePipe(&child_stdin_read, &stdin_write_, &attributes, 0) ||
        !CreatePipe(&stdout_read_, &child_stdout_write, &attributes, 0) ||
        !CreatePipe(&stderr_read_, &child_stderr_write, &attributes, 0)) {
      if (child_stdin_read != nullptr) CloseHandle(child_stdin_read);
      if (child_stdout_write != nullptr) CloseHandle(child_stdout_write);
      if (child_stderr_write != nullptr) CloseHandle(child_stderr_write);
      if (stdin_write_ != nullptr) CloseHandle(stdin_write_);
      if (stdout_read_ != nullptr) CloseHandle(stdout_read_);
      if (stderr_read_ != nullptr) CloseHandle(stderr_read_);
      stdin_write_ = stdout_read_ = stderr_read_ = nullptr;
      const std::string error = windows_error("CreatePipe");
      result = fail_result(result, error);
      return false;
    }
    if (!SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderr_read_, HANDLE_FLAG_INHERIT, 0)) {
      const std::string error = windows_error("SetHandleInformation");
      CloseHandle(child_stdin_read);
      CloseHandle(child_stdout_write);
      CloseHandle(child_stderr_write);
      CloseHandle(stdin_write_);
      CloseHandle(stdout_read_);
      CloseHandle(stderr_read_);
      stdin_write_ = stdout_read_ = stderr_read_ = nullptr;
      result = fail_result(result, error);
      return false;
    }

    std::wstring command_line;
    for (const std::wstring& argument : wide_command) {
      if (!command_line.empty()) command_line.push_back(L' ');
      command_line += quote_windows_argument(argument);
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_stdin_read;
    startup.hStdOutput = child_stdout_write;
    startup.hStdError = child_stderr_write;
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process_info)) {
      const std::string error = windows_error("CreateProcessW");
      CloseHandle(child_stdin_read);
      CloseHandle(child_stdout_write);
      CloseHandle(child_stderr_write);
      CloseHandle(stdin_write_);
      CloseHandle(stdout_read_);
      CloseHandle(stderr_read_);
      stdin_write_ = stdout_read_ = stderr_read_ = nullptr;
      result = fail_result(result, error);
      return false;
    }
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    CloseHandle(child_stderr_write);
    process_handle_ = process_info.hProcess;
    CloseHandle(process_info.hThread);
    command_ = command;
    started_ = true;
    stdout_buffer_.clear();
    const HANDLE stderr_handle = stderr_read_;
    stderr_thread_ = std::thread([this, stderr_handle] {
      drain_stderr(stderr_handle);
    });
    return true;
  }

  bool write_line_locked(const std::string& wire, Clock::time_point) {
    std::size_t offset = 0;
    while (offset < wire.size()) {
      DWORD written = 0;
      if (!WriteFile(stdin_write_, wire.data() + offset,
                     static_cast<DWORD>(std::min<std::size_t>(
                         wire.size() - offset,
                         static_cast<std::size_t>(std::numeric_limits<DWORD>::max()))),
                     &written, nullptr) ||
          written == 0) {
        last_error_ = windows_error("WriteFile");
        return false;
      }
      offset += written;
    }
    return true;
  }

  bool read_line_locked(std::string& response, const Clock::time_point deadline) {
    while (true) {
      const std::size_t newline = stdout_buffer_.find('\n');
      if (newline != std::string::npos) {
        response = stdout_buffer_.substr(0, newline);
        stdout_buffer_.erase(0, newline + 1U);
        return true;
      }
      if (stdout_buffer_.size() > config_.max_response_bytes) {
        last_error_ = "Whisper bridge response exceeded the configured size limit";
        return false;
      }
      DWORD available = 0;
      if (!PeekNamedPipe(stdout_read_, nullptr, 0, nullptr, &available, nullptr)) {
        last_error_ = windows_error("PeekNamedPipe");
        return false;
      }
      if (available != 0) {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        if (!ReadFile(stdout_read_, buffer.data(),
                      static_cast<DWORD>(std::min<std::size_t>(available, buffer.size())),
                      &read, nullptr) ||
            read == 0) {
          last_error_ = windows_error("ReadFile");
          return false;
        }
        stdout_buffer_.append(buffer.data(), read);
        continue;
      }
      if (WaitForSingleObject(process_handle_, 0) == WAIT_OBJECT_0) {
        last_error_ = "Whisper bridge process closed stdout before a response";
        return false;
      }
      const auto remaining = remaining_until(deadline);
      if (remaining <= std::chrono::milliseconds::zero()) {
        last_error_ = "Timed out waiting for Whisper bridge response";
        return false;
      }
      std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(5)));
    }
  }

  void drain_stderr(const HANDLE stderr_handle) noexcept {
    std::array<char, 4096> buffer{};
    while (stderr_handle != nullptr) {
      DWORD read = 0;
      if (!ReadFile(stderr_handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr) ||
          read == 0) {
        break;
      }
      append_stderr(buffer.data(), read);
    }
  }

  bool is_running_locked() const noexcept {
    return started_ && process_handle_ != nullptr &&
           WaitForSingleObject(process_handle_, 0) == WAIT_TIMEOUT;
  }

  int process_exit_code_locked() const noexcept {
    if (process_handle_ == nullptr) return -1;
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(process_handle_, &code) || code == STILL_ACTIVE) {
      return -1;
    }
    return static_cast<int>(code);
  }

  void stop_locked() noexcept {
    if (!started_ && process_handle_ == nullptr) return;
    if (stdin_write_ != nullptr) {
      CloseHandle(stdin_write_);
      stdin_write_ = nullptr;
    }
    if (process_handle_ != nullptr) {
      if (WaitForSingleObject(process_handle_, static_cast<DWORD>(
                                  config_.shutdown_timeout.count())) ==
          WAIT_TIMEOUT) {
        TerminateProcess(process_handle_, 124);
        WaitForSingleObject(process_handle_, INFINITE);
      }
      CloseHandle(process_handle_);
      process_handle_ = nullptr;
    }
    if (stdout_read_ != nullptr) {
      CloseHandle(stdout_read_);
      stdout_read_ = nullptr;
    }
    if (stderr_thread_.joinable()) stderr_thread_.join();
    if (stderr_read_ != nullptr) {
      CloseHandle(stderr_read_);
      stderr_read_ = nullptr;
    }
    started_ = false;
    command_.clear();
    stdout_buffer_.clear();
  }

  PersistentWhisperProcessRunnerConfig config_;
  mutable std::mutex request_mutex_;
  mutable std::mutex stderr_mutex_;
  std::vector<std::string> command_;
  std::string stdout_buffer_;
  std::string stderr_output_;
  std::string last_error_;
  std::thread stderr_thread_;
  bool started_ = false;
  HANDLE process_handle_ = nullptr;
  HANDLE stdin_write_ = nullptr;
  HANDLE stdout_read_ = nullptr;
  HANDLE stderr_read_ = nullptr;
#else
  bool start_locked(const std::vector<std::string>& command,
                    WhisperProcessResult& result) {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
      const int error = errno;
      if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
      if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
      if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
      if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
      if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);
      if (stderr_pipe[1] >= 0) close(stderr_pipe[1]);
      result = fail_result(result, std::string("pipe failed: ") + std::strerror(error));
      return false;
    }
    posix_spawn_file_actions_t actions{};
    int spawn_error = posix_spawn_file_actions_init(&actions);
    const bool actions_initialized = spawn_error == 0;
    if (spawn_error == 0) {
      spawn_error = posix_spawn_file_actions_adddup2(
          &actions, stdin_pipe[0], STDIN_FILENO);
    }
    if (spawn_error == 0) {
      spawn_error = posix_spawn_file_actions_adddup2(
          &actions, stdout_pipe[1], STDOUT_FILENO);
    }
    if (spawn_error == 0) {
      spawn_error = posix_spawn_file_actions_adddup2(
          &actions, stderr_pipe[1], STDERR_FILENO);
    }
    const auto add_close = [&actions, &spawn_error](const int descriptor,
                                                    const int standard_descriptor) {
      if (spawn_error == 0 && descriptor != standard_descriptor) {
        spawn_error = posix_spawn_file_actions_addclose(&actions, descriptor);
      }
    };
    add_close(stdin_pipe[0], STDIN_FILENO);
    add_close(stdin_pipe[1], STDIN_FILENO);
    add_close(stdout_pipe[0], STDOUT_FILENO);
    add_close(stdout_pipe[1], STDOUT_FILENO);
    add_close(stderr_pipe[0], STDERR_FILENO);
    add_close(stderr_pipe[1], STDERR_FILENO);

    std::vector<char*> argv;
    argv.reserve(command.size() + 1U);
    for (const std::string& argument : command) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child = -1;
    if (spawn_error == 0) {
      spawn_error = posix_spawnp(&child, argv.front(), &actions, nullptr,
                                 argv.data(), environ);
    }
    if (actions_initialized) {
      (void)posix_spawn_file_actions_destroy(&actions);
    }
    if (spawn_error != 0) {
      close(stdin_pipe[0]); close(stdin_pipe[1]);
      close(stdout_pipe[0]); close(stdout_pipe[1]);
      close(stderr_pipe[0]); close(stderr_pipe[1]);
      result = fail_result(result, std::string("posix_spawnp failed: ") +
                                     std::strerror(spawn_error));
      return false;
    }
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    command_ = command;
    pid_ = child;
    started_ = true;
    stdout_buffer_.clear();
    const int stderr_descriptor = stderr_fd_;
    stderr_thread_ = std::thread([this, stderr_descriptor] {
      drain_stderr(stderr_descriptor);
    });
    return true;
  }

  bool write_line_locked(const std::string& wire, const Clock::time_point) {
    std::size_t offset = 0;
    while (offset < wire.size()) {
      sigset_t blocked{};
      sigemptyset(&blocked);
      sigaddset(&blocked, SIGPIPE);
      sigset_t previous{};
      (void)pthread_sigmask(SIG_BLOCK, &blocked, &previous);
      const ssize_t written = write(stdin_fd_, wire.data() + offset,
                                    wire.size() - offset);
      const int error = errno;
      if (written < 0 && error == EPIPE) {
        timespec no_wait{};
        (void)sigtimedwait(&blocked, nullptr, &no_wait);
      }
      (void)pthread_sigmask(SIG_SETMASK, &previous, nullptr);
      if (written <= 0) {
        last_error_ = std::string("write to Whisper bridge failed: ") +
                      std::strerror(error);
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }

  bool read_line_locked(std::string& response, const Clock::time_point deadline) {
    while (true) {
      const std::size_t newline = stdout_buffer_.find('\n');
      if (newline != std::string::npos) {
        response = stdout_buffer_.substr(0, newline);
        stdout_buffer_.erase(0, newline + 1U);
        return true;
      }
      if (stdout_buffer_.size() > config_.max_response_bytes) {
        last_error_ = "Whisper bridge response exceeded the configured size limit";
        return false;
      }
      const auto remaining = remaining_until(deadline);
      if (remaining <= std::chrono::milliseconds::zero()) {
        last_error_ = "Timed out waiting for Whisper bridge response";
        return false;
      }
      pollfd descriptor{stdout_fd_, POLLIN | POLLHUP | POLLERR, 0};
      const int wait_ms = static_cast<int>(std::min<std::int64_t>(
          remaining.count(), static_cast<std::int64_t>(INT_MAX)));
      const int ready = poll(&descriptor, 1, wait_ms);
      if (ready < 0) {
        if (errno == EINTR) continue;
        last_error_ = std::string("poll on Whisper bridge failed: ") +
                      std::strerror(errno);
        return false;
      }
      if (ready == 0) {
        last_error_ = "Timed out waiting for Whisper bridge response";
        return false;
      }
      std::array<char, 4096> buffer{};
      const ssize_t read_count = read(stdout_fd_, buffer.data(), buffer.size());
      if (read_count <= 0) {
        last_error_ = read_count == 0
                          ? "Whisper bridge process closed stdout before a response"
                          : std::string("read from Whisper bridge failed: ") +
                                std::strerror(errno);
        return false;
      }
      stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(read_count));
    }
  }

  void drain_stderr(const int stderr_descriptor) noexcept {
    std::array<char, 4096> buffer{};
    while (stderr_descriptor >= 0) {
      const ssize_t read_count =
          read(stderr_descriptor, buffer.data(), buffer.size());
      if (read_count <= 0) break;
      append_stderr(buffer.data(), static_cast<std::size_t>(read_count));
    }
  }

  bool is_running_locked() const noexcept {
    if (!started_ || pid_ <= 0) return false;
    if (process_exit_code_ >= 0) return false;
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == pid_) {
      process_exit_code_ = decode_wait_status(status);
      return false;
    }
    return result == 0;
  }

  int process_exit_code_locked() const noexcept {
    if (pid_ <= 0) return -1;
    if (process_exit_code_ >= 0) return process_exit_code_;
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) return -1;
    if (result < 0 && errno == ECHILD) return -1;
    process_exit_code_ = decode_wait_status(status);
    return process_exit_code_;
  }

  static int decode_wait_status(const int status) noexcept {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
  }

  void stop_locked() noexcept {
    if (!started_ && pid_ <= 0) return;
    if (stdin_fd_ >= 0) {
      close(stdin_fd_);
      stdin_fd_ = -1;
    }
    if (pid_ > 0) {
      const auto deadline = Clock::now() + config_.shutdown_timeout;
      int status = 0;
      while (process_exit_code_ < 0 && waitpid(pid_, &status, WNOHANG) == 0 &&
             Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (process_exit_code_ < 0 && waitpid(pid_, &status, WNOHANG) == 0) {
        (void)kill(pid_, SIGTERM);
        const auto term_deadline = Clock::now() + config_.shutdown_timeout;
        while (process_exit_code_ < 0 && waitpid(pid_, &status, WNOHANG) == 0 &&
               Clock::now() < term_deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (process_exit_code_ < 0 && waitpid(pid_, &status, WNOHANG) == 0) {
          (void)kill(pid_, SIGKILL);
          (void)waitpid(pid_, &status, 0);
        }
      }
      if (process_exit_code_ < 0) {
        process_exit_code_ = decode_wait_status(status);
      }
      pid_ = -1;
    }
    if (stdout_fd_ >= 0) {
      close(stdout_fd_);
      stdout_fd_ = -1;
    }
    if (stderr_thread_.joinable()) stderr_thread_.join();
    if (stderr_fd_ >= 0) {
      close(stderr_fd_);
      stderr_fd_ = -1;
    }
    started_ = false;
    process_exit_code_ = -1;
    command_.clear();
    stdout_buffer_.clear();
  }

  PersistentWhisperProcessRunnerConfig config_;
  mutable std::mutex request_mutex_;
  mutable std::mutex stderr_mutex_;
  std::vector<std::string> command_;
  std::string stdout_buffer_;
  std::string stderr_output_;
  std::string last_error_;
  std::thread stderr_thread_;
  bool started_ = false;
  pid_t pid_ = -1;
  mutable int process_exit_code_ = -1;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int stderr_fd_ = -1;
#endif
};

PersistentWhisperProcessRunner::PersistentWhisperProcessRunner(
    PersistentWhisperProcessRunnerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

PersistentWhisperProcessRunner::~PersistentWhisperProcessRunner() = default;

PersistentWhisperProcessRunner::PersistentWhisperProcessRunner(
    PersistentWhisperProcessRunner&&) noexcept = default;

PersistentWhisperProcessRunner& PersistentWhisperProcessRunner::operator=(
    PersistentWhisperProcessRunner&&) noexcept = default;

WhisperProcessResult PersistentWhisperProcessRunner::operator()(
    const WhisperProcessRequest& request) {
  return impl_ == nullptr ? WhisperProcessResult{}
                          : impl_->run(request);
}

void PersistentWhisperProcessRunner::stop() noexcept {
  if (impl_ != nullptr) impl_->stop();
}

bool PersistentWhisperProcessRunner::running() const noexcept {
  return impl_ != nullptr && impl_->running();
}

std::string PersistentWhisperProcessRunner::last_error() const {
  return impl_ == nullptr ? std::string{} : impl_->last_error();
}

WhisperProcessRunner make_persistent_whisper_process_runner(
    PersistentWhisperProcessRunnerConfig config) {
  const auto runner = std::make_shared<PersistentWhisperProcessRunner>(
      std::move(config));
  return [runner](const WhisperProcessRequest& request) {
    return (*runner)(request);
  };
}

}  // namespace obs_whisperbleep::runtime
