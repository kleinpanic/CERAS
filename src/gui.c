/* gui.c */
#include "gui.h"
#include "config.h" 
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>

/* Define default position offsets relative to target monitor */
#define DEFAULT_OFFSET_X 100
#define DEFAULT_OFFSET_Y 100

struct _GUIComponents {
    GtkWidget *window;
    GtkWidget *record_toggle;
    GtkWidget *camera_toggle;
    GtkWidget *audio_toggle;      /* Audio toggle button */
    GtkWidget *source_combo;
    GtkWidget *quality_combo;
    GtkWidget *resolution_combo;
    GtkWidget *audio_codec_combo; /* Audio codec selection */
    GtkWidget *fps_selector;      /* FPS selector */
    GtkWidget *webcam_resolution_combo; /* Webcam resolution selection */
    GtkWidget *info_label;
    GtkWidget *preview_area;
};

/* Generate CSS using values from config.h, and insert newlines between rules */
static char *generate_css(void) {
    char *css_data = malloc(512);
    if (!css_data)
        return NULL;
    snprintf(css_data, 512,
             "window { background-color: %s; }\n"
             "button { background-color: %s; color: %s; border-radius: %s; padding: %s; }\n"
             "label { color: %s; }\n"
             "comboboxtext, spinbutton { background-color: %s; color: %s; }",
             WINDOW_BG_COLOR,
             BUTTON_BG_COLOR, BUTTON_TEXT_COLOR, BUTTON_BORDER_RADIUS, BUTTON_PADDING,
             LABEL_TEXT_COLOR,
             COMBO_BG_COLOR, COMBO_TEXT_COLOR);
    return css_data;
}

GUIComponents* gui_init() {
    GUIComponents* gui = malloc(sizeof(GUIComponents));
    if (!gui)
        return NULL;

    gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui->window), "CtheScreen");
    gtk_window_set_default_size(GTK_WINDOW(gui->window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(gui->window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(gui->window), TRUE);
    /* Set the type hint to DIALOG so that DWM treats the window as floating but still manages it */
    gtk_window_set_type_hint(GTK_WINDOW(gui->window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_keep_above(GTK_WINDOW(gui->window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(gui->window), TRUE);

    /* Apply CSS styling using dynamically generated CSS from config.h */
    char *css_data = generate_css();
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(gui->window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    free(css_data);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_container_add(GTK_CONTAINER(gui->window), grid);

    /* Row 0: Recording toggle, Camera toggle, Audio toggle */
    gui->record_toggle = gtk_toggle_button_new_with_label("Start Recording");
    gtk_widget_set_hexpand(gui->record_toggle, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gui->record_toggle, 0, 0, 1, 1);

    gui->camera_toggle = gtk_toggle_button_new_with_label("Camera On");
    gtk_widget_set_hexpand(gui->camera_toggle, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gui->camera_toggle, 1, 0, 1, 1);

    gui->audio_toggle = gtk_toggle_button_new_with_label("Audio On");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->audio_toggle), TRUE);
    gtk_widget_set_hexpand(gui->audio_toggle, TRUE);
    gtk_grid_attach(GTK_GRID(grid), gui->audio_toggle, 2, 0, 1, 1);

    /* Row 1: Source selection and Quality selection */
    GtkWidget *source_label = gtk_label_new("Capture Source:");
    gtk_grid_attach(GTK_GRID(grid), source_label, 0, 1, 1, 1);
    gui->source_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->source_combo), "All");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->source_combo), "Window");
    gui_populate_source_combo(gui);
    gtk_grid_attach(GTK_GRID(grid), gui->source_combo, 1, 1, 1, 1);

    GtkWidget *quality_label = gtk_label_new("Encoding Quality:");
    gtk_widget_set_tooltip_text(quality_label, "Controls video encoding quality (bitrate, etc.)");
    gtk_grid_attach(GTK_GRID(grid), quality_label, 2, 1, 1, 1);
    gui->quality_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->quality_combo), "Low");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->quality_combo), "Medium");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->quality_combo), "High");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->quality_combo), 1);
    gtk_grid_attach(GTK_GRID(grid), gui->quality_combo, 3, 1, 1, 1);

    /* Row 2: Resolution selection and FPS selector */
    GtkWidget *resolution_label = gtk_label_new("Capture Resolution:");
    gtk_widget_set_tooltip_text(resolution_label, "Sets the output dimensions for recording");
    gtk_grid_attach(GTK_GRID(grid), resolution_label, 0, 2, 1, 1);
    gui->resolution_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->resolution_combo), "Full");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->resolution_combo), "1080p");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->resolution_combo), "720p");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->resolution_combo), "480p");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->resolution_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), gui->resolution_combo, 1, 2, 1, 1);

    GtkWidget *fps_label = gtk_label_new("FPS:");
    gtk_grid_attach(GTK_GRID(grid), fps_label, 2, 2, 1, 1);
    gui->fps_selector = gtk_spin_button_new_with_range(15, 60, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui->fps_selector), 30);
    gtk_grid_attach(GTK_GRID(grid), gui->fps_selector, 3, 2, 1, 1);

    /* Row 3: Audio Codec selection and Webcam resolution selection */
    GtkWidget *audio_codec_label = gtk_label_new("Audio Codec:");
    gtk_grid_attach(GTK_GRID(grid), audio_codec_label, 0, 3, 1, 1);
    gui->audio_codec_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->audio_codec_combo), "AAC");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->audio_codec_combo), "PCM");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->audio_codec_combo), "Opus");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->audio_codec_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), gui->audio_codec_combo, 1, 3, 1, 1);

    GtkWidget *webcam_res_label = gtk_label_new("Webcam Resolution:");
    gtk_grid_attach(GTK_GRID(grid), webcam_res_label, 2, 3, 1, 1);
    gui->webcam_resolution_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->webcam_resolution_combo), "Default");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->webcam_resolution_combo), "640x480");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->webcam_resolution_combo), 0);
    gtk_grid_attach(GTK_GRID(grid), gui->webcam_resolution_combo, 3, 3, 1, 1);

    /* Row 4: Info label */
    gui->info_label = gtk_label_new("Video Info: (Elapsed Time, File Size, etc.)");
    gtk_grid_attach(GTK_GRID(grid), gui->info_label, 0, 4, 4, 1);

    /* Row 5: Webcam preview area */
    gui->preview_area = gtk_image_new();
    gtk_widget_set_hexpand(gui->preview_area, TRUE);
    gtk_widget_set_vexpand(gui->preview_area, TRUE);
    /* Remove any fixed size so that the preview area can shrink freely */
    gtk_grid_attach(GTK_GRID(grid), gui->preview_area, 0, 5, 4, 1);

    gtk_widget_show_all(gui->window);

    /* Attempt to position window on the "eDP-1" monitor if available;
       otherwise, use the primary monitor */
    {
        int x, y, w, h;
        if (get_monitor_geometry("eDP-1", &x, &y, &w, &h) == 0) {
            gtk_window_move(GTK_WINDOW(gui->window), x + DEFAULT_OFFSET_X, y + DEFAULT_OFFSET_Y);
        } else {
            GdkDisplay *display = gdk_display_get_default();
            GdkMonitor *primary = gdk_display_get_primary_monitor(display);
            if (primary) {
                GdkRectangle geom;
                gdk_monitor_get_geometry(primary, &geom);
                gtk_window_move(GTK_WINDOW(gui->window), geom.x + DEFAULT_OFFSET_X, geom.y + DEFAULT_OFFSET_Y);
            } else {
                gtk_window_move(GTK_WINDOW(gui->window), DEFAULT_OFFSET_X, DEFAULT_OFFSET_Y);
            }
        }
    }
    return gui;
}

void gui_update_info(GUIComponents* gui, const char* info) {
    if (gui && gui->info_label)
        gtk_label_set_text(GTK_LABEL(gui->info_label), info);
}

void gui_update_preview(GUIComponents* gui, GdkPixbuf* pixbuf) {
    if (gui && gui->preview_area)
        gtk_image_set_from_pixbuf(GTK_IMAGE(gui->preview_area), pixbuf);
}

/* Updated cleanup to only destroy the widget if still valid */
void gui_cleanup(GUIComponents* gui) {
    if (gui) {
        if (gui->window && GTK_IS_WIDGET(gui->window))
            gtk_widget_destroy(gui->window);
        free(gui);
    }
}

RecordSource gui_get_record_source(GUIComponents* gui) {
    if (!gui || !gui->source_combo)
        return RECORD_SOURCE_ALL;
    const char* sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->source_combo));
    if (!sel)
        return RECORD_SOURCE_ALL;
    if (strcmp(sel, "Window") == 0)
        return RECORD_SOURCE_WINDOW;
    if (strcmp(sel, "All") == 0)
        return RECORD_SOURCE_ALL;
    return RECORD_SOURCE_MONITOR;
}

Quality gui_get_quality(GUIComponents* gui) {
    if (!gui || !gui->quality_combo)
        return QUALITY_MEDIUM;
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(gui->quality_combo));
    switch(active) {
        case 0: return QUALITY_LOW;
        case 1: return QUALITY_MEDIUM;
        case 2: return QUALITY_HIGH;
        default: return QUALITY_MEDIUM;
    }
}

const char* gui_get_resolution(GUIComponents* gui) {
    if (!gui || !gui->resolution_combo)
        return "Full";
    return gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->resolution_combo));
}

const char* gui_get_monitor_name(GUIComponents* gui) {
    if (!gui || !gui->source_combo)
        return NULL;
    const char* sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->source_combo));
    if (!sel)
        return NULL;
    if ((strcmp(sel, "All") == 0) || (strcmp(sel, "Window") == 0))
        return NULL;
    return sel;
}

void gui_populate_source_combo(GUIComponents *gui) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return;
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources *res = XRRGetScreenResources(dpy, root);
    if (!res) {
        XCloseDisplay(dpy);
        return;
    }
    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo *info = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (info && info->connection == RR_Connected && info->crtc) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->source_combo), info->name);
        }
        if (info)
            XRRFreeOutputInfo(info);
    }
    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
}

AudioCodec gui_get_audio_codec(GUIComponents* gui) {
    if (!gui || !gui->audio_codec_combo)
        return AUDIO_CODEC_AAC;
    const char *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->audio_codec_combo));
    if (!sel)
        return AUDIO_CODEC_AAC;
    if (strcmp(sel, "PCM") == 0)
        return AUDIO_CODEC_PCM;
    if (strcmp(sel, "Opus") == 0)
        return AUDIO_CODEC_OPUS;
    return AUDIO_CODEC_AAC;
}

int gui_get_fps(GUIComponents* gui) {
    if (!gui || !gui->fps_selector)
        return 30;
    return (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui->fps_selector));
}

const char* gui_get_webcam_resolution(GUIComponents* gui) {
    if (!gui || !gui->webcam_resolution_combo)
        return "Default";
    return gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(gui->webcam_resolution_combo));
}

