#include "engine_manager.h"

#include <windows.h>

#include <utility>

EngineManager::EngineManager(const flutter::DartProject& project)
    : project_(project) {}

EngineManager::~EngineManager() { shutdown(); }

void EngineManager::set_main_window(FlutterWindow* window) {
  main_window_ = window;
}

EngineEntry* EngineManager::get(const std::string& id) {
  auto it = engines_.find(id);
  if (it == engines_.end() || !it->second->window->GetHandle()) {
    return nullptr;
  }
  return it->second.get();
}

EngineEntry* EngineManager::create(const std::string& id,
                                   const std::string& type, int64_t item_id) {
  if (get(id)) return nullptr;
  // Closed entries are removed here, outside their window callback.
  engines_.erase(id);
  auto entry = std::make_unique<EngineEntry>();
  entry->id = id;
  entry->window = std::make_unique<FlutterWindow>(project_, this, type, item_id);
  const wchar_t* title = type == "post" ? L"Post detail" : L"User detail";
  if (!entry->window->Create(title, Win32Window::Point(80, 80),
                             Win32Window::Size(1000, 720))) {
    return nullptr;
  }
  EngineEntry* result = entry.get();
  engines_.emplace(id, std::move(entry));
  return result;
}

void EngineManager::on_window_closed(FlutterWindow* window) {
  if (shutting_down_) return;
  if (main_window_ && main_window_ != window && main_window_->GetHandle()) return;
  for (const auto& entry : engines_) {
    if (entry.second->window.get() != window && entry.second->window->GetHandle()) return;
  }
  PostQuitMessage(0);
}

void EngineManager::shutdown() {
  if (shutting_down_) return;
  shutting_down_ = true;
  engines_.clear();
  main_window_ = nullptr;
}
