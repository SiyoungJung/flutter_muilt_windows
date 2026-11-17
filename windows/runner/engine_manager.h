#ifndef RUNNER_ENGINE_MANAGER_H_
#define RUNNER_ENGINE_MANAGER_H_

#include <flutter/dart_project.h>

#include <map>
#include <memory>
#include <string>

#include "flutter_window.h"

struct EngineEntry {
  std::string id;
  std::unique_ptr<FlutterWindow> window;
};

// Owns secondary windows and their Flutter engines. The main window is borrowed.
class EngineManager {
 public:
  explicit EngineManager(const flutter::DartProject& project);
  ~EngineManager();
  EngineManager(const EngineManager&) = delete;
  EngineManager& operator=(const EngineManager&) = delete;

  void set_main_window(FlutterWindow* window);
  EngineEntry* create(const std::string& id, const std::string& type,
                      int64_t item_id);
  EngineEntry* get(const std::string& id);
  void on_window_closed(FlutterWindow* window);
  void shutdown();

 private:
  flutter::DartProject project_;
  FlutterWindow* main_window_ = nullptr;
  std::map<std::string, std::unique_ptr<EngineEntry>> engines_;
  bool shutting_down_ = false;
};

#endif  // RUNNER_ENGINE_MANAGER_H_
