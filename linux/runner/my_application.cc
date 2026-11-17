#include "my_application.h"

#include <flutter_linux/flutter_linux.h>

#include <string>

#include "engine_manager.h"
#include "flutter/generated_plugin_registrant.h"

struct _MyApplication {
  GtkApplication parent_instance;
  char** dart_entrypoint_arguments;
  EngineManager* engine_manager;
};

G_DEFINE_TYPE(MyApplication, my_application, GTK_TYPE_APPLICATION)

namespace {
constexpr char kChannelName[] = "multi_windows/window";

struct WindowContext {
  MyApplication* app;
  std::string type;
  int64_t id;
  FlMethodChannel* channel;
};

void free_window_context(gpointer data) {
  auto* context = static_cast<WindowContext*>(data);
  g_printerr("[window-debug] freeing context type=%s id=%" G_GINT64_FORMAT "\n",
             context->type.c_str(), static_cast<gint64>(context->id));
  fl_method_channel_set_method_call_handler(context->channel, nullptr, nullptr,
                                            nullptr);
  g_object_unref(context->channel);
  delete context;
}

void first_frame_cb(FlView* view, gpointer) {
  g_printerr("[window-debug] first frame view=%p\n", static_cast<void*>(view));
  gtk_widget_show(gtk_widget_get_toplevel(GTK_WIDGET(view)));
}

gboolean close_window_cb(GtkWidget* window, GdkEvent*, gpointer) {
  g_printerr("[window-debug] close requested window=%p\n", static_cast<void*>(window));
  // FlView's default delete handler requests process-wide application exit.
  // Destroy only this GTK window; GtkApplication exits after the last one.
  gtk_widget_destroy(window);
  return TRUE;
}

void setup_window(MyApplication* self, FlView* view, GtkWidget* window,
                  const std::string& type, int64_t id);

void on_method_call(FlMethodChannel*, FlMethodCall* call, gpointer user_data) {
  auto* context = static_cast<WindowContext*>(user_data);
  const gchar* method = fl_method_call_get_name(call);
  g_printerr("[window-debug] method=%s window=%s:%" G_GINT64_FORMAT "\n",
             method, context->type.c_str(), static_cast<gint64>(context->id));
  if (g_strcmp0(method, "getWindowInfo") == 0) {
    g_autoptr(FlValue) result = fl_value_new_map();
    fl_value_set_string_take(result, "type",
                             fl_value_new_string(context->type.c_str()));
    fl_value_set_string_take(result, "id", fl_value_new_int(context->id));
    fl_method_call_respond_success(call, result, nullptr);
    return;
  }
  if (g_strcmp0(method, "openWindow") == 0) {
    FlValue* args = fl_method_call_get_args(call);
    if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
      fl_method_call_respond_error(call, "invalid_arguments", "Expected a map", nullptr, nullptr);
      return;
    }
    FlValue* type_value = fl_value_lookup_string(args, "type");
    FlValue* id_value = fl_value_lookup_string(args, "id");
    if (type_value == nullptr || fl_value_get_type(type_value) != FL_VALUE_TYPE_STRING ||
        id_value == nullptr || fl_value_get_type(id_value) != FL_VALUE_TYPE_INT ||
        fl_value_get_int(id_value) <= 0) {
      fl_method_call_respond_error(call, "invalid_arguments", "Expected a window type and positive ID", nullptr, nullptr);
      return;
    }
    std::string type = fl_value_get_string(type_value);
    int64_t id = fl_value_get_int(id_value);
    if ((type != "post" && type != "user") ||
        (type == "post" && context->type != "main") ||
        (type == "user" && context->type != "post")) {
      fl_method_call_respond_error(call, "invalid_window", "Unsupported window request", nullptr, nullptr);
      return;
    }
    std::string key = type + ":" + std::to_string(id);
    g_printerr("[window-debug] opening key=%s\n", key.c_str());
    EngineEntry* entry = context->app->engine_manager->get(key);
    if (entry != nullptr) {
      gtk_window_present(GTK_WINDOW(entry->window));
    } else {
      entry = context->app->engine_manager->create(key, GTK_APPLICATION(context->app));
      if (entry == nullptr) {
        fl_method_call_respond_error(call, "window_failed", "Could not create window", nullptr, nullptr);
        return;
      }
      gtk_window_set_title(GTK_WINDOW(entry->window), type == "post" ? "Post detail" : "User detail");
      setup_window(context->app, entry->view, entry->window, type, id);
      g_printerr("[window-debug] setup complete key=%s\n", key.c_str());
    }
    fl_method_call_respond_success(call, nullptr, nullptr);
    return;
  }
  fl_method_call_respond_not_implemented(call, nullptr);
}

void setup_window(MyApplication* self, FlView* view, GtkWidget* window,
                  const std::string& type, int64_t id) {
  g_printerr("[window-debug] setup begin window=%s:%" G_GINT64_FORMAT " view=%p\n",
             type.c_str(), static_cast<gint64>(id), static_cast<void*>(view));
  // Connect before realizing the view, which installs Flutter's delete handler.
  g_signal_connect(window, "delete-event", G_CALLBACK(close_window_cb), nullptr);
  FlEngine* engine = fl_view_get_engine(view);
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  FlMethodChannel* channel = fl_method_channel_new(
      fl_engine_get_binary_messenger(engine), kChannelName, FL_METHOD_CODEC(codec));
  auto* context = new WindowContext{self, type, id, channel};
  fl_method_channel_set_method_call_handler(channel, on_method_call, context, nullptr);
  g_object_set_data_full(G_OBJECT(window), "window-context", context, free_window_context);
  fl_register_plugins(FL_PLUGIN_REGISTRY(view));
  gtk_widget_show(GTK_WIDGET(view));
  g_signal_connect(view, "first-frame", G_CALLBACK(first_frame_cb), nullptr);
  gtk_widget_realize(GTK_WIDGET(view));
  g_printerr("[window-debug] view realized window=%s:%" G_GINT64_FORMAT "\n",
             type.c_str(), static_cast<gint64>(id));
}
}  // namespace

static void my_application_activate(GApplication* application) {
  g_printerr("[window-debug] application activated\n");
  MyApplication* self = MY_APPLICATION(application);
  GtkWidget* window = gtk_application_window_new(GTK_APPLICATION(application));
  gtk_window_set_default_size(GTK_WINDOW(window), 1000, 720);
  gtk_window_set_title(GTK_WINDOW(window), "Posts");
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_dart_entrypoint_arguments(project, self->dart_entrypoint_arguments);
  FlView* view = fl_view_new(project);
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
  setup_window(self, view, window, "main", 0);
  gtk_widget_grab_focus(GTK_WIDGET(view));
}

static gboolean my_application_local_command_line(GApplication* application,
                                                   gchar*** arguments,
                                                   int* exit_status) {
  MyApplication* self = MY_APPLICATION(application);
  self->dart_entrypoint_arguments = g_strdupv(*arguments + 1);
  g_autoptr(GError) error = nullptr;
  if (!g_application_register(application, nullptr, &error)) {
    g_warning("Failed to register: %s", error->message);
    *exit_status = 1;
    return TRUE;
  }
  g_application_activate(application);
  *exit_status = 0;
  return TRUE;
}

static void my_application_dispose(GObject* object) {
  g_printerr("[window-debug] application disposing\n");
  MyApplication* self = MY_APPLICATION(object);
  g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
  delete self->engine_manager;
  self->engine_manager = nullptr;
  G_OBJECT_CLASS(my_application_parent_class)->dispose(object);
}

static void my_application_class_init(MyApplicationClass* klass) {
  G_APPLICATION_CLASS(klass)->activate = my_application_activate;
  G_APPLICATION_CLASS(klass)->local_command_line = my_application_local_command_line;
  G_OBJECT_CLASS(klass)->dispose = my_application_dispose;
}

static void my_application_init(MyApplication* self) {
  self->engine_manager = new EngineManager();
}

MyApplication* my_application_new() {
  g_set_prgname(APPLICATION_ID);
  return MY_APPLICATION(g_object_new(my_application_get_type(),
                                     "application-id", APPLICATION_ID, "flags",
                                     G_APPLICATION_NON_UNIQUE, nullptr));
}
