#ifndef RUNNER_FLUTTER_WINDOW_H_
#define RUNNER_FLUTTER_WINDOW_H_

#include <flutter/dart_project.h>
#include <flutter/encodable_value.h>
#include <flutter/flutter_view_controller.h>
#include <flutter/method_channel.h>

#include <cstdint>
#include <memory>
#include <string>

#include "win32_window.h"

class EngineManager;

class FlutterWindow : public Win32Window {
 public:
  FlutterWindow(const flutter::DartProject& project, EngineManager* manager,
                std::string type, int64_t id);
  ~FlutterWindow() override;

 protected:
  bool OnCreate() override;
  void OnDestroy() override;
  LRESULT MessageHandler(HWND window, UINT const message, WPARAM const wparam,
                         LPARAM const lparam) noexcept override;

 private:
  flutter::DartProject project_;
  EngineManager* manager_;
  std::string type_;
  int64_t id_;
  bool initialized_ = false;
  std::unique_ptr<flutter::FlutterViewController> flutter_controller_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

#endif  // RUNNER_FLUTTER_WINDOW_H_
