#ifndef ENGINE_MANAGER_H_
#define ENGINE_MANAGER_H_

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <string>
#include <vector>

// The engine and view are borrowed from the window's widget hierarchy.
struct EngineEntry {
  std::string id;
  FlEngine* engine;
  FlView* view;
  GtkWidget* window;
};

class EngineManager {
 public:
  EngineManager() = default;
  EngineManager(const EngineManager&) = delete;
  EngineManager& operator=(const EngineManager&) = delete;

  ~EngineManager() {
    // Windows can outlive the manager; don't leave callbacks pointing to it.
    for (const auto& entry : engines_) {
      g_signal_handlers_disconnect_by_data(entry.window, this);
    }
  }

  EngineEntry* create(const std::string& id, GtkApplication* app) {
    g_printerr("[window-debug] engine create begin key=%s\n", id.c_str());
    if (get(id) != nullptr) {
      return nullptr;
    }

    g_autoptr(FlDartProject) project = fl_dart_project_new();
    FlView* view = fl_view_new(project);
    g_printerr("[window-debug] view created key=%s view=%p\n", id.c_str(),
               static_cast<void*>(view));
    FlEngine* engine = fl_view_get_engine(view);
    GtkWidget* window = gtk_application_window_new(app);

    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));

    engines_.push_back({id, engine, view, window});
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), this);

    // The caller can register plugins and channels before realizing the view.
    return &engines_.back();
  }

  EngineEntry* get(const std::string& id) {
    for (auto& entry : engines_) {
      if (entry.id == id) {
        return &entry;
      }
    }
    return nullptr;
  }

  const std::vector<EngineEntry>& all() const { return engines_; }

 private:
  static void on_window_destroy(GtkWidget* window, gpointer user_data) {
    g_printerr("[window-debug] window destroyed window=%p\n", static_cast<void*>(window));
    auto* self = static_cast<EngineManager*>(user_data);
    auto& entries = self->engines_;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [window](const EngineEntry& entry) {
                                   return entry.window == window;
                                 }),
                  entries.end());
  }

  // Pointers returned by create/get are invalidated by later modifications.
  std::vector<EngineEntry> engines_;
};

#endif  // ENGINE_MANAGER_H_
