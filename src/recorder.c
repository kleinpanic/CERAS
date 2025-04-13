/* src/recorder.c */
#include "recorder.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/cursorfont.h>

/* 
 * Implements interactive window selection.
 * Grabs the pointer, sets the cursor to a crosshair, waits for a button press,
 * obtains the window under the pointer, and retrieves its geometry.
 */
void recorder_select_window(Display *display, Window *target, int *x, int *y, int *width, int *height) {
    XEvent event;
    Window ret_root, ret_child;
    unsigned int uwidth, uheight;
    unsigned int dummy_depth;

    /* Set crosshair cursor */
    Cursor cross_cursor = XCreateFontCursor(display, XC_crosshair);
    XDefineCursor(display, DefaultRootWindow(display), cross_cursor);

    if (XGrabPointer(display, DefaultRootWindow(display), False,
                     ButtonPressMask, GrabModeSync, GrabModeAsync,
                     None, cross_cursor, CurrentTime) != GrabSuccess) {
        fprintf(stderr, "Could not grab pointer for window selection\n");
        XFreeCursor(display, cross_cursor);
        return;
    }
    
    XAllowEvents(display, SyncPointer, CurrentTime);
    XWindowEvent(display, DefaultRootWindow(display), ButtonPressMask, &event);
    
    ret_child = event.xbutton.subwindow;
    if(ret_child == None)
        ret_child = DefaultRootWindow(display);
    
    if (!XGetGeometry(display, ret_child, &ret_root, x, y, &uwidth, &uheight, &dummy_depth, &dummy_depth)) {
        fprintf(stderr, "Failed to get geometry of the selected window\n");
        XUngrabPointer(display, CurrentTime);
        XFreeCursor(display, cross_cursor);
        return;
    }
    *width = (int)uwidth;
    *height = (int)uheight;
    *target = ret_child;
    
    XUngrabPointer(display, CurrentTime);
    /* Restore default cursor */
    XUndefineCursor(display, DefaultRootWindow(display));
    XFreeCursor(display, cross_cursor);
}

/*
 * Initialize the recorder.
 * If a nonzero target is provided, that window’s geometry is used;
 * otherwise, the full screen (root) is captured.
 */
RecorderContext* recorder_init(Window target) {
    RecorderContext *ctx = malloc(sizeof(RecorderContext));
    if (!ctx) return NULL;
    ctx->display = XOpenDisplay(NULL);
    if (!ctx->display) {
        fprintf(stderr, "Could not open X display\n");
        free(ctx);
        return NULL;
    }
    ctx->screen = DefaultScreen(ctx->display);
    ctx->root = RootWindow(ctx->display, ctx->screen);
    if (target) {
        /* Capture a specific window */
        ctx->target = target;
        ctx->is_window_capture = 1;
        XWindowAttributes attr;
        if (XGetWindowAttributes(ctx->display, target, &attr) == 0) {
            fprintf(stderr, "Failed to get attributes for selected window\n");
            XCloseDisplay(ctx->display);
            free(ctx);
            return NULL;
        }
        ctx->x = attr.x;   // For window capture, these values are relative to the window
        ctx->y = attr.y;
        ctx->width = attr.width;
        ctx->height = attr.height;
    } else {
        /* Full screen capture */
        ctx->target = 0;
        ctx->is_window_capture = 0;
        ctx->x = 0;
        ctx->y = 0;
        ctx->width = DisplayWidth(ctx->display, ctx->screen);
        ctx->height = DisplayHeight(ctx->display, ctx->screen);
    }
    ctx->is_capturing = 0;
    return ctx;
}

int recorder_start(RecorderContext* ctx) {
    if (!ctx) return -1;
    ctx->is_capturing = 1;
    return 0;
}

int recorder_stop(RecorderContext* ctx) {
    if (!ctx) return -1;
    ctx->is_capturing = 0;
    return 0;
}

void recorder_cleanup(RecorderContext* ctx) {
    if (ctx) {
        if (ctx->display)
            XCloseDisplay(ctx->display);
        free(ctx);
    }
}

/* 
 * Capture one frame from the screen (or target window) and convert it to 24-bit RGB.
 * When capturing full screen (including monitor mode), uses ctx->x and ctx->y as the offset.
 * For window capture, it captures starting at (0,0) as the window’s image.
 */
uint8_t* recorder_capture_frame(RecorderContext* ctx, int *linesize) {
    if (!ctx || !ctx->is_capturing)
        return NULL;
    Window capture_win = ctx->is_window_capture ? ctx->target : ctx->root;
    int x = ctx->is_window_capture ? 0 : ctx->x;
    int y = ctx->is_window_capture ? 0 : ctx->y;
    XImage *img = XGetImage(ctx->display, capture_win, x, y, ctx->width, ctx->height, AllPlanes, ZPixmap);
    if (!img) {
        fprintf(stderr, "Failed to capture screen image\n");
        return NULL;
    }
    int size = ctx->width * ctx->height * 3;
    uint8_t *buffer = malloc(size);
    if (!buffer) {
        XDestroyImage(img);
        return NULL;
    }
    for (int j = 0; j < ctx->height; j++) {
        for (int i = 0; i < ctx->width; i++) {
            unsigned long pixel = XGetPixel(img, i, j);
            int index = (j * ctx->width + i) * 3;
            buffer[index]     = (pixel >> 16) & 0xff;  // Red
            buffer[index + 1] = (pixel >> 8) & 0xff;   // Green
            buffer[index + 2] = pixel & 0xff;          // Blue
        }
    }
    if (linesize)
        *linesize = ctx->width * 3;
    XDestroyImage(img);
    return buffer;
}

/* 
 * Dynamically updates the window geometry for window capture.
 */
int recorder_update_window_geometry(RecorderContext *ctx) {
    if (!ctx || !ctx->is_window_capture)
        return -1;
    XWindowAttributes attr;
    if (!XGetWindowAttributes(ctx->display, ctx->target, &attr)) {
        fprintf(stderr, "Failed to update window attributes for dynamic tracking\n");
        return -1;
    }
    ctx->x = attr.x;
    ctx->y = attr.y;
    ctx->width = attr.width;
    ctx->height = attr.height;
    return 0;
}

