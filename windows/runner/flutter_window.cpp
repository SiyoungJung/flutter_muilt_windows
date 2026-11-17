#include "flutter_window.h"

#include <optional>
#include <utility>
#include <variant>

#include <flutter/standard_method_codec.h>

#include "engine_manager.h"
#include "flutter/generated_plugin_registrant.h"
#include "utils.h"

namespace {
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;
constexpr char kChannelName[] = "multi_windows/window";
}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project,
                             EngineManager* manager, std::string type,
                             int64_t id)
    : project_(project), manager_(manager), type_(std::move(type)), id_(id) {}

FlutterWindow::~FlutterWindow() { Destroy(); }

bool FlutterWindow::OnCreate() {
  const std::string label = type_ + ":" + std::to_string(id_);
  DebugLog("FlutterWindow::OnCreate begin " + label);
  if (!Win32Window::OnCreate()) return false;
  RECT frame = GetClientArea();
  DebugLog("Creating FlutterViewController " + label + " size=" +
           std::to_string(frame.right - frame.left) + "x" +
           std::to_string(frame.bottom - frame.top));
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  DebugLog("FlutterViewController constructor returned " + label);
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    DebugLog("FlutterViewController engine or view is null " + label);
    return false;
  }

  DebugLog("RegisterPlugins begin " + label);
  RegisterPlugins(flutter_controller_->engine());
  DebugLog("RegisterPlugins end; creating channel " + label);
  channel_ = std::make_unique<flutter::MethodChannel<EncodableValue>>(
      flutter_controller_->engine()->messenger(), kChannelName,
      &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [this](const MethodCall<EncodableValue>& call,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
        DebugLog("Method call window=" + type_ + ":" + std::to_string(id_) +
                 " method=" + call.method_name());
        if (call.method_name() == "getWindowInfo") {
          EncodableMap info;
          info[EncodableValue("type")] = EncodableValue(type_);
          info[EncodableValue("id")] = EncodableValue(id_);
          result->Success(EncodableValue(info));
          return;
        }
        if (call.method_name() != "openWindow") {
          result->NotImplemented();
          return;
        }
        const auto* args = call.arguments() ? std::get_if<EncodableMap>(call.arguments()) : nullptr;
        if (!args) {
          result->Error("invalid_arguments", "Expected a map");
          return;
        }
        auto type_it = args->find(EncodableValue("type"));
        auto id_it = args->find(EncodableValue("id"));
        const auto* type = type_it == args->end() ? nullptr : std::get_if<std::string>(&type_it->second);
        int64_t id = 0;
        if (id_it != args->end()) {
          if (const auto* value32 = std::get_if<int32_t>(&id_it->second)) {
            id = *value32;
          } else if (const auto* value64 = std::get_if<int64_t>(&id_it->second)) {
            id = *value64;
          }
        }
        if (!type || id <= 0) {
          result->Error("invalid_arguments", "Expected a window type and positive ID");
          return;
        }
        if ((*type != "post" && *type != "user") ||
            (*type == "post" && type_ != "main") ||
            (*type == "user" && type_ != "post")) {
          result->Error("invalid_window", "Unsupported window request");
          return;
        }
        const std::string key = *type + ":" + std::to_string(id);
        DebugLog("openWindow requested key=" + key);
        EngineEntry* entry = manager_->get(key);
        if (entry) {
          DebugLog("openWindow activating existing key=" + key);
          HWND window = entry->window->GetHandle();
          if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
          SetForegroundWindow(window);
        } else if (!manager_->create(key, *type, id)) {
          DebugLog("openWindow creation failed key=" + key);
          result->Error("window_failed", "Could not create window");
          return;
        }
        DebugLog("openWindow returning success key=" + key);
        result->Success();
      });

  DebugLog("SetChildContent begin " + label);
  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  initialized_ = true;
  DebugLog("SetChildContent end; scheduling first frame " + label);
  flutter_controller_->engine()->SetNextFrameCallback([this]() {
    DebugLog("First frame callback " + type_ + ":" + std::to_string(id_));
    Show();
  });
  DebugLog("ForceRedraw begin " + label);
  flutter_controller_->ForceRedraw();
  DebugLog("FlutterWindow::OnCreate end " + label);
  return true;
}

void FlutterWindow::OnDestroy() {
  DebugLog("FlutterWindow::OnDestroy begin " + type_ + ":" + std::to_string(id_) +
           " initialized=" + std::to_string(initialized_));
  channel_.reset();
  DebugLog("FlutterWindow::OnDestroy channel reset " + type_ + ":" + std::to_string(id_));
  flutter_controller_.reset();
  DebugLog("FlutterWindow::OnDestroy controller reset " + type_ + ":" + std::to_string(id_));
  if (initialized_) {
    initialized_ = false;
    manager_->on_window_closed(this);
  }
  Win32Window::OnDestroy();
  DebugLog("FlutterWindow::OnDestroy end " + type_ + ":" + std::to_string(id_));
}

LRESULT FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                                     WPARAM const wparam,
                                     LPARAM const lparam) noexcept {
  if (flutter_controller_) {
    std::optional<LRESULT> result = flutter_controller_->HandleTopLevelWindowProc(
        hwnd, message, wparam, lparam);
    if (result) return *result;
  }
  if (message == WM_FONTCHANGE && flutter_controller_) {
    flutter_controller_->engine()->ReloadSystemFonts();
  }
  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
