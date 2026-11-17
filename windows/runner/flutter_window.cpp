#include "flutter_window.h"

#include <optional>
#include <utility>
#include <variant>

#include <flutter/standard_method_codec.h>

#include "engine_manager.h"
#include "flutter/generated_plugin_registrant.h"

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
  if (!Win32Window::OnCreate()) return false;
  RECT frame = GetClientArea();
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  if (!flutter_controller_->engine() || !flutter_controller_->view()) return false;

  RegisterPlugins(flutter_controller_->engine());
  channel_ = std::make_unique<flutter::MethodChannel<EncodableValue>>(
      flutter_controller_->engine()->messenger(), kChannelName,
      &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [this](const MethodCall<EncodableValue>& call,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
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
          if (const auto* value = std::get_if<int32_t>(&id_it->second)) id = *value;
          else if (const auto* value = std::get_if<int64_t>(&id_it->second)) id = *value;
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
        EngineEntry* entry = manager_->get(key);
        if (entry) {
          HWND window = entry->window->GetHandle();
          if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
          SetForegroundWindow(window);
        } else if (!manager_->create(key, *type, id)) {
          result->Error("window_failed", "Could not create window");
          return;
        }
        result->Success();
      });

  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  initialized_ = true;
  flutter_controller_->engine()->SetNextFrameCallback([this]() { Show(); });
  flutter_controller_->ForceRedraw();
  return true;
}

void FlutterWindow::OnDestroy() {
  channel_.reset();
  flutter_controller_.reset();
  if (initialized_) {
    initialized_ = false;
    manager_->on_window_closed(this);
  }
  Win32Window::OnDestroy();
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
