/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#if defined(__linux__)

#include "Gui.h"
#include "Logger.h"

#include <gtk/gtk.h>

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

extern unsigned long GamePID;

static GtkApplication* s_app          = nullptr;
static GtkWidget*      s_window       = nullptr;
static GtkWidget*      s_status_label = nullptr;
static GtkWidget*      s_progress_bar = nullptr;
static GtkStatusIcon*  s_tray_icon    = nullptr;
static guint           s_pulse_source = 0;
static std::atomic<bool> s_enabled { false };

// ---------------------------------------------------------------------------
// GTK Idle Callbacks
// ---------------------------------------------------------------------------

struct LabelUpdate { std::string text; };
static gboolean cb_set_label(gpointer data) {
    auto* u = static_cast<LabelUpdate*>(data);
    if (s_status_label) gtk_label_set_text(GTK_LABEL(s_status_label), u->text.c_str());
    delete u;
    return G_SOURCE_REMOVE;
}

struct ProgressUpdate { double fraction; };
static gboolean cb_set_progress(gpointer data) {
    auto* u = static_cast<ProgressUpdate*>(data);
    if (!s_progress_bar) { delete u; return G_SOURCE_REMOVE; }

    if (u->fraction >= 0.0) {
        if (s_pulse_source) { g_source_remove(s_pulse_source); s_pulse_source = 0; }
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), u->fraction);
    } else if (!s_pulse_source) {
        s_pulse_source = g_timeout_add(80, +[](gpointer) -> gboolean {
            if (s_progress_bar) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(s_progress_bar));
            return G_SOURCE_CONTINUE;
        }, nullptr);
    }
    delete u;
    return G_SOURCE_REMOVE;
}

static gboolean cb_quit(gpointer) {
    if (s_app) g_application_quit(G_APPLICATION(s_app));
    return G_SOURCE_REMOVE;
}

// ---------------------------------------------------------------------------
// Error Dialog
// ---------------------------------------------------------------------------

struct ErrorDialogData {
    std::string title;
    std::string message;
    std::mutex mtx;
    std::condition_variable cv;
    bool done { false };
};

static gboolean cb_show_error(gpointer data) {
    auto* ed = static_cast<ErrorDialogData*>(data);

    GtkWidget* dlg = gtk_message_dialog_new(
        s_window ? GTK_WINDOW(s_window) : nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s", ed->title.c_str());
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", ed->message.c_str());
    
    g_signal_connect(dlg, "response", G_CALLBACK(+[](GtkDialog* dialog, gint response_id, gpointer d) {
        auto* ed2 = static_cast<ErrorDialogData*>(d);
        std::lock_guard lk(ed2->mtx);
        ed2->done = true;
        ed2->cv.notify_all();
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }), ed);

    gtk_widget_show_all(dlg);
    return G_SOURCE_REMOVE;
}

// ---------------------------------------------------------------------------
// Tray Icon
// ---------------------------------------------------------------------------

static void cb_tray_popup(GtkStatusIcon* /*status_icon*/, guint button, guint activate_time, gpointer) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* quit_item = gtk_menu_item_new_with_label("Quit BeamMP");
    g_signal_connect(quit_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer) {
        if (GamePID > 0) kill(GamePID, SIGTERM);
        if (s_app) g_application_quit(G_APPLICATION(s_app));
        exit(0);
    }), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_widget_show_all(menu);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, gtk_status_icon_position_menu,
                   s_tray_icon, button, activate_time);
#pragma GCC diagnostic pop
}

static gboolean cb_show_running_indicator(gpointer) {
    if (s_window) gtk_widget_hide(s_window);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    s_tray_icon = gtk_status_icon_new_from_icon_name("com.beammp.Launcher");
    gtk_status_icon_set_tooltip_text(s_tray_icon, "BeamMP Launcher");
    g_signal_connect(s_tray_icon, "popup-menu", G_CALLBACK(cb_tray_popup), nullptr);
#pragma GCC diagnostic pop

    return G_SOURCE_REMOVE;
}

// ---------------------------------------------------------------------------
// Application Startup
// ---------------------------------------------------------------------------

static void on_activate(GtkApplication* app, gpointer user_data) {
    s_app = app;
    s_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(s_window), "BeamMP Launcher");
    gtk_window_set_resizable(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(s_window), 440, 120);
    gtk_window_set_icon_name(GTK_WINDOW(s_window), "com.beammp.Launcher");
    g_signal_connect(s_window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer) -> gboolean { return TRUE; }), nullptr);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: #1a1a2e; } "
        ".bmp-status { color: #d0d0f0; font-size: 11pt; font-weight: bold; } "
        ".bmp-progress trough { background-color: #2d2d4e; border-radius: 4px; min-height: 8px; } "
        ".bmp-progress progress { background-color: #6d28d9; border-radius: 4px; }", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_margin_start(outer, 20); gtk_widget_set_margin_end(outer, 20);
    gtk_widget_set_margin_top(outer, 18); gtk_widget_set_margin_bottom(outer, 18);
    gtk_container_add(GTK_CONTAINER(s_window), outer);

    GtkWidget* logo = gtk_image_new_from_resource("/com/beammp/Launcher/beamng-logo.svg");
    gtk_widget_set_valign(logo, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), logo, FALSE, FALSE, 0);

    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(right, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), right, TRUE, TRUE, 0);

    s_status_label = gtk_label_new("Starting...");
    gtk_widget_set_halign(s_status_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(s_status_label), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_status_label), "bmp-status");
    gtk_box_pack_start(GTK_BOX(right), s_status_label, FALSE, FALSE, 0);

    s_progress_bar = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(s_progress_bar), "bmp-progress");
    gtk_box_pack_start(GTK_BOX(right), s_progress_bar, FALSE, FALSE, 0);

    s_pulse_source = g_timeout_add(80, +[](gpointer) -> gboolean {
        if (s_progress_bar) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(s_progress_bar));
        return G_SOURCE_CONTINUE;
    }, nullptr);

    gtk_widget_show_all(s_window);

    auto* fn = static_cast<std::function<void()>*>(user_data);
    std::thread([fn]() { (*fn)(); delete fn; }).detach();
}

namespace Gui {
    bool IsEnabled() { return s_enabled.load(std::memory_order_relaxed); }
    void Run(int, const char**, std::function<void()> work) {
        if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) { work(); return; }
        s_enabled.store(true, std::memory_order_relaxed);
        GtkApplication* app = gtk_application_new("com.beammp.Launcher", G_APPLICATION_DEFAULT_FLAGS);
        g_signal_connect(app, "activate", G_CALLBACK(on_activate), new std::function<void()>(std::move(work)));
        g_application_run(G_APPLICATION(app), 0, nullptr);
        g_object_unref(app);
    }
    void SetStatus(const std::string& msg) {
        if (!s_enabled.load(std::memory_order_relaxed)) return;
        g_idle_add(cb_set_label, new LabelUpdate{msg});
        g_idle_add(cb_set_progress, new ProgressUpdate{-1.0});
    }
    void SetProgress(double fraction) {
        if (!s_enabled.load(std::memory_order_relaxed)) return;
        g_idle_add(cb_set_progress, new ProgressUpdate{fraction});
    }
    void ShowError(const std::string& title, const std::string& msg) {
        if (!s_enabled.load(std::memory_order_relaxed)) { error(title + ": " + msg); return; }
        ErrorDialogData ed{title, msg};
        g_idle_add(cb_show_error, &ed);
        std::unique_lock lk(ed.mtx);
        ed.cv.wait(lk, [&] { return ed.done; });
    }
    void ShowRunningIndicator() {
        if (!s_enabled.load(std::memory_order_relaxed)) return;
        g_idle_add(cb_show_running_indicator, nullptr);
    }
    void Shutdown() {
        if (!s_enabled.load(std::memory_order_relaxed)) return;
        g_idle_add(cb_quit, nullptr);
    }
}
#endif // defined(__linux__)
