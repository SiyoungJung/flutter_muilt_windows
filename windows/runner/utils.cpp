#include "utils.h"

#include <flutter_windows.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>

#include <iostream>

namespace {
LONG WINAPI LogUnhandledException(EXCEPTION_POINTERS* exception) {
  char message[256];
  if (exception && exception->ExceptionRecord) {
    const auto* record = exception->ExceptionRecord;
    snprintf(message, sizeof(message), "Unhandled exception: code=0x%08lX address=%p flags=0x%08lX",
             static_cast<unsigned long>(record->ExceptionCode),
             record->ExceptionAddress,
             static_cast<unsigned long>(record->ExceptionFlags));
    DebugLog(message);
  } else {
    DebugLog("Unhandled exception: no exception record");
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void DebugLog(const std::string& message) {
  SYSTEMTIME now;
  GetLocalTime(&now);
  char prefix[128];
  snprintf(prefix, sizeof(prefix),
           "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu ",
           static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
           static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
           static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
           static_cast<unsigned>(now.wMilliseconds),
           static_cast<unsigned long>(GetCurrentProcessId()),
           static_cast<unsigned long>(GetCurrentThreadId()));
  const std::string line = std::string(prefix) + message + "\r\n";
  OutputDebugStringA(line.c_str());
  wchar_t temp_path[MAX_PATH];
  const DWORD length = GetTempPathW(MAX_PATH, temp_path);
  if (length == 0 || length >= MAX_PATH) return;
  const std::wstring path = std::wstring(temp_path) + L"multi_windows_poc_debug.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

void InstallCrashLogger() {
  SetUnhandledExceptionFilter(LogUnhandledException);
  DebugLog("Crash logger installed; log path: %TEMP%\\multi_windows_poc_debug.log");
}

void CreateAndAttachConsole() {
  if (::AllocConsole()) {
    FILE *unused;
    if (freopen_s(&unused, "CONOUT$", "w", stdout)) {
      _dup2(_fileno(stdout), 1);
    }
    if (freopen_s(&unused, "CONOUT$", "w", stderr)) {
      _dup2(_fileno(stdout), 2);
    }
    std::ios::sync_with_stdio();
    FlutterDesktopResyncOutputStreams();
  }
}

std::vector<std::string> GetCommandLineArguments() {
  // Convert the UTF-16 command line arguments to UTF-8 for the Engine to use.
  int argc;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return std::vector<std::string>();
  }

  std::vector<std::string> command_line_arguments;

  // Skip the first argument as it's the binary name.
  for (int i = 1; i < argc; i++) {
    command_line_arguments.push_back(Utf8FromUtf16(argv[i]));
  }

  ::LocalFree(argv);

  return command_line_arguments;
}

std::string Utf8FromUtf16(const wchar_t* utf16_string) {
  if (utf16_string == nullptr) {
    return std::string();
  }
  // First, find the length of the string with a safe upper bound (CWE-126).
  // UNICODE_STRING_MAX_CHARS (32767) is the maximum length of a UNICODE_STRING.
  int input_length = static_cast<int>(wcsnlen(utf16_string, UNICODE_STRING_MAX_CHARS));
  // Now use that bounded length to determine the required buffer size.
  // When an explicit length is passed, WideCharToMultiByte does not include
  // the null terminator in its returned size.
  int target_length = ::WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, utf16_string,
      input_length, nullptr, 0, nullptr, nullptr);
  std::string utf8_string;
  if (target_length == 0 || static_cast<size_t>(target_length) > utf8_string.max_size()) {
    return utf8_string;
  }
  utf8_string.resize(target_length);
  int converted_length = ::WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, utf16_string,
      input_length, utf8_string.data(), target_length, nullptr, nullptr);
  if (converted_length == 0) {
    return std::string();
  }
  return utf8_string;
}
