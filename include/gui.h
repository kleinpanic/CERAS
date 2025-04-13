#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>
#include "encoder.h"  /* For Quality and AudioCodec */

/* Available recording sources */
typedef enum {
    RECORD_SOURCE_ALL,       /* record union of all monitors */
    RECORD_SOURCE_WINDOW,    /* Record a specific window */
    RECORD_SOURCE_MONITOR    /* Record a single monitor */
} RecordSource;

/* GUIComponents structure, extended with additional controls */
typedef struct {
    GtkWidget *window;
    GtkWidget *record_toggle;     /* Button to start/stop recording */
    GtkWidget *camera_toggle;     /* Button to toggle webcam preview */
    GtkWidget *audio_toggle;      /* New: Toggle button for audio recording */
    GtkWidget *source_combo;      /* Combo box: "All", "Window", plus individual monitor names */
    GtkWidget *quality_combo;     /* Combo box: "Low", "Medium", "High" */
    GtkWidget *resolution_combo;  /* Combo box: "Full", "1080p", "720p", "480p" */
    GtkWidget *audio_codec_combo; /* New: Combo box for Audio Codec (AAC, PCM, Opus) */
    GtkWidget *fps_selector;      /* New: Selector for FPS (e.g., SpinButton) */
    GtkWidget *webcam_resolution_combo; /* New: Combo for webcam resolution (Default, 640x480) */
    GtkWidget *info_label;        /* Displays recording info */
    GtkWidget *preview_area;      /* Webcam preview area */
} GUIComponents;

/* Initialize the GUI and return main components */
GUIComponents* gui_init();

/* Update the info label with a string */
void gui_update_info(GUIComponents* gui, const char* info);

/* Update the webcam preview area with a GdkPixbuf */
void gui_update_preview(GUIComponents* gui, GdkPixbuf* pixbuf);

/* Free GUI resources */
void gui_cleanup(GUIComponents* gui);

/* Get the currently selected recording source.
   Returns:
    - RECORD_SOURCE_WINDOW if the selected entry equals "Window"
    - RECORD_SOURCE_ALL if the selected entry equals "All"
    - RECORD_SOURCE_MONITOR otherwise (i.e. if it matches a monitor name)
*/
RecordSource gui_get_record_source(GUIComponents* gui);

/* Get the quality setting (Low/Medium/High) */
Quality gui_get_quality(GUIComponents* gui);

/* Get the resolution selection string (e.g., "Full", "1080p", etc.) */
const char* gui_get_resolution(GUIComponents* gui);

/* Get the selected monitor name from the source combo.
   Returns NULL if the user selected "All" or "Window".
*/
const char* gui_get_monitor_name(GUIComponents* gui);

/* Populate the source combo with available monitors in addition to "All" and "Window". */
void gui_populate_source_combo(GUIComponents *gui);

/* Get the selected audio codec from the GUI.
   Returns one of AUDIO_CODEC_AAC, AUDIO_CODEC_PCM, AUDIO_CODEC_OPUS.
*/
AudioCodec gui_get_audio_codec(GUIComponents* gui);

/* Get the selected FPS value */
int gui_get_fps(GUIComponents* gui);

/* Get the selected webcam resolution option (e.g., "Default" or "640x480") */
const char* gui_get_webcam_resolution(GUIComponents* gui);

int get_monitor_geometry(const char *monitor_name, int *x, int *y, int *width, int *height);


#endif // GUI_H

