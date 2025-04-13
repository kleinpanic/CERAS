#ifndef RECORDER_H
#define RECORDER_H

#include <X11/Xlib.h>
#include <stdint.h>
#include <X11/extensions/XShm.h>

/* Recorder context now supports both full-screen and window-based capture */
typedef struct {
    Display *display;
    Window root;
    Window target;       // Target window for capture (if any)
    int screen;
    int x, y;            // Capture region origin
    int width;
    int height;
    int is_capturing;
    int is_window_capture;  // Flag: if 1, capture only the target window
    int use_shm;           // Flag: 1 if XShm is used
    XShmSegmentInfo shm_info; // For XShm
} RecorderContext;

/* 
 * Initializes recorder. If target is 0, captures the full screen;
 * otherwise, uses the specified target window’s geometry.
 */
RecorderContext* recorder_init(Window target);

/* 
 * Grabs the pointer and lets the user click on a window to capture.
 * This function blocks until a window is selected.
 * On return, *target, *x, *y, *width, *height are filled out.
 */
void recorder_select_window(Display *display, Window *target, int *x, int *y, int *width, int *height);

/* Begin capturing (sets a flag) */
int recorder_start(RecorderContext* ctx);

/* Stop capturing */
int recorder_stop(RecorderContext* ctx);

/* Free allocated recorder context */
void recorder_cleanup(RecorderContext* ctx);

/* Capture one frame from the screen or target window.
   Returns a buffer in RGB24 format (caller must free).
   'linesize' returns the number of bytes per row.
*/
uint8_t* recorder_capture_frame(RecorderContext* ctx, int *linesize);

/* Update window geometry dynamically for window capture.
   Re-fetches attributes of the target window.
*/
int recorder_update_window_geometry(RecorderContext *ctx);

#endif // RECORDER_H

