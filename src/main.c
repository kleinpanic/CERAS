/* src/main.c */
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <getopt.h>
#include "recorder.h"
#include "audio.h"
#include "encoder.h"
#include "gui.h"
#include "version.h"   /* Must define APP_VERSION, e.g. "1.0.0" */
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <unistd.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

/* Global debug flag: if set, extra debug info is printed */
static int g_debug = 0;

/* Macro for debug printing */
#define DEBUG_PRINT(fmt, ...) \
    do { if (g_debug) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

/* Structure and idle callback for updating the preview safely */
typedef struct {
    GtkWidget *image;
    GdkPixbuf *pixbuf;
} PreviewData;

static gboolean update_preview_idle(gpointer data) {
    PreviewData *pd = (PreviewData *) data;
    if (GTK_IS_WIDGET(pd->image)) {
        GtkAllocation alloc;
        gtk_widget_get_allocation(pd->image, &alloc);
        if (alloc.width <= 0 || alloc.height <= 0)
            gtk_image_clear(GTK_IMAGE(pd->image));
        else {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pd->pixbuf, alloc.width, alloc.height, GDK_INTERP_BILINEAR);
            gtk_image_set_from_pixbuf(GTK_IMAGE(pd->image), scaled);
            g_object_unref(scaled);
        }
    }
    g_object_unref(pd->pixbuf);
    free(pd);
    return FALSE;
}

/* Prototype for prompt_for_filename */
static char* prompt_for_filename(GtkWindow *parent, const char *default_name);

/* Global variables */
static GUIComponents *gui;
static int is_recording = 0;
static EncoderContext* enc_ctx = NULL;
static RecorderContext* rec_ctx = NULL;
static AudioContext* audio_ctx = NULL;
static pthread_t record_thread;
static pthread_t audio_thread;
static pthread_t webcam_thread;
static time_t recording_start_time = 0;
static volatile int camera_running = 0;
static volatile int camera_thread_running = 0;

/* Get file size (in bytes) of the output file */
static off_t get_file_size(const char* filename) {
    struct stat st;
    return (stat(filename, &st) == 0) ? st.st_size : -1;
}

/* Timer callback to update elapsed time and file size in the info label */
static gboolean update_info_callback(gpointer data) {
    if (!is_recording || !enc_ctx) return FALSE;
    time_t now = time(NULL);
    int elapsed = (int)difftime(now, recording_start_time);
    off_t fsize = get_file_size(enc_ctx->filename);
    char info[512];
    snprintf(info, sizeof(info), "Elapsed: %d sec | File Size: %ld bytes | Output: %.100s",
             elapsed, (long)(fsize > 0 ? fsize : 0), enc_ctx->filename);
    gui_update_info(gui, info);
    return TRUE;
}

/* Audio capture thread */
void* audio_thread_func(void* arg) {
    int buffer_frames = 1024;
    int bytes_per_frame = audio_ctx->channels * 2; // S16_LE
    int buffer_size = buffer_frames * bytes_per_frame;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) return NULL;
    while (is_recording) {
        int frames = audio_capture(audio_ctx, buffer, buffer_size);
        if (frames > 0) {
            int size = frames * bytes_per_frame;
            encoder_encode_audio_frame(enc_ctx, buffer, size);
        }
        usleep(5000);
    }
    free(buffer);
    return NULL;
}

/* Video recording thread */
void* record_thread_func(void* arg) {
    int fps = gui_get_fps(gui);
    int linesize = 0;
    while (is_recording) {
        if(rec_ctx && rec_ctx->is_window_capture)
            recorder_update_window_geometry(rec_ctx);
        uint8_t* frame_data = recorder_capture_frame(rec_ctx, &linesize);
        if (frame_data) {
            encoder_encode_video_frame(enc_ctx, frame_data);
            free(frame_data);
        }
        usleep(1000000 / fps);
    }
    return NULL;
}

/* Webcam preview thread */
void* webcam_thread_func(void* arg) {
    camera_thread_running = 1;
    avdevice_register_all();
    AVFormatContext *fmt_ctx = NULL;
    const AVInputFormat *input_fmt = av_find_input_format("v4l2");
    const char* device = "/dev/video0";
    AVDictionary *options = NULL;
    const char* ws = gui_get_webcam_resolution(gui);
    if (ws && strcmp(ws, "640x480") == 0)
        av_dict_set(&options, "video_size", "640x480", 0);
    if (avformat_open_input(&fmt_ctx, device, input_fmt, &options) != 0) {
        fprintf(stderr, "Could not open webcam device\n");
        if (options) av_dict_free(&options);
        camera_thread_running = 0;
        return NULL;
    }
    if (options) av_dict_free(&options);
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not get stream info from webcam\n");
        avformat_close_input(&fmt_ctx);
        camera_thread_running = 0;
        return NULL;
    }
    int video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "Could not find video stream in webcam\n");
        avformat_close_input(&fmt_ctx);
        camera_thread_running = 0;
        return NULL;
    }
    const AVCodec *codec = avcodec_find_decoder(fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Could not find codec for webcam\n");
        avformat_close_input(&fmt_ctx);
        camera_thread_running = 0;
        return NULL;
    }
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open webcam codec\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        camera_thread_running = 0;
        return NULL;
    }
    struct SwsContext* sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                                                codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
                                                SWS_BICUBIC, NULL, NULL, NULL);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    uint8_t *buffer_data = (uint8_t *) av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer_data, AV_PIX_FMT_RGB24,
                           codec_ctx->width, codec_ctx->height, 1);

    int sleep_duration = (ws && strcmp(ws, "640x480") == 0) ? 20000 : 30000;
    while (camera_running) {
        if (av_read_frame(fmt_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                if (avcodec_send_packet(codec_ctx, packet) == 0) {
                    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                        sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0,
                                  codec_ctx->height, rgb_frame->data, rgb_frame->linesize);
                        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(rgb_frame->data[0],
                                                                     GDK_COLORSPACE_RGB,
                                                                     FALSE,
                                                                     8,
                                                                     codec_ctx->width,
                                                                     codec_ctx->height,
                                                                     rgb_frame->linesize[0],
                                                                     NULL, NULL);
                        if (pixbuf) {
                            PreviewData *pd = malloc(sizeof(PreviewData));
                            pd->image = gui->preview_area;
                            pd->pixbuf = g_object_ref(pixbuf);
                            g_idle_add(update_preview_idle, pd);
                            g_object_unref(pixbuf);
                        }
                        av_frame_unref(frame);
                    }
                }
            }
            av_packet_unref(packet);
        }
        if (!camera_running)
            break;
        usleep(sleep_duration);
    }
    av_free(buffer_data);
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    camera_thread_running = 0;
    return NULL;
}

/* Get monitor geometry using XRandR */
int get_monitor_geometry(const char* monitor_name, int *x, int *y, int *width, int *height) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return -1;
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources *res = XRRGetScreenResources(dpy, root);
    if (!res) {
        XCloseDisplay(dpy);
        return -1;
    }
    int found = 0;
    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo *out = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (out && out->connection == RR_Connected && out->name && strcmp(out->name, monitor_name) == 0) {
            if (out->crtc) {
                XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, out->crtc);
                if (crtc) {
                    *x = crtc->x;
                    *y = crtc->y;
                    *width = crtc->width;
                    *height = crtc->height;
                    XRRFreeCrtcInfo(crtc);
                    found = 1;
                    XRRFreeOutputInfo(out);
                    break;
                }
            }
        }
        if (out)
            XRRFreeOutputInfo(out);
    }
    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
    return found ? 0 : -1;
}

/* Prompt for filename using a GTK dialog */
static char* prompt_for_filename(GtkWindow *parent, const char *default_name) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Save Recording",
                                                    parent,
                                                    GTK_DIALOG_MODAL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), default_name);
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    
    char *result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text && strlen(text) > 0)
            result = strdup(text);
    }
    gtk_widget_destroy(dialog);
    return result;
}

/* Callback for the recording toggle button */
static void on_record_toggle(GtkToggleButton *toggle_button, gpointer user_data) {
    if (gtk_toggle_button_get_active(toggle_button)) {
        gtk_button_set_label(GTK_BUTTON(toggle_button), "Stop Recording");

        /* Retrieve selections */
        RecordSource source = gui_get_record_source(gui);
        Quality quality = gui_get_quality(gui);
        const char* resolution_choice = gui_get_resolution(gui);
        int capture_width, capture_height;
        int cap_x = 0, cap_y = 0;

        /* Get full desktop dimensions */
        Display *dpy = XOpenDisplay(NULL);
        if (!dpy) {
            gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
            return;
        }
        capture_width = DisplayWidth(dpy, DefaultScreen(dpy));
        capture_height = DisplayHeight(dpy, DefaultScreen(dpy));
        XCloseDisplay(dpy);

        /* Override resolution based on selection */
        if (strcmp(resolution_choice, "1080p") == 0) {
            capture_width = 1920; capture_height = 1080;
        } else if (strcmp(resolution_choice, "720p") == 0) {
            capture_width = 1280; capture_height = 720;
        } else if (strcmp(resolution_choice, "480p") == 0) {
            capture_width = 854; capture_height = 480;
        }

        Window target = 0;
        if (source == RECORD_SOURCE_WINDOW) {
            printf("Please click on the window you wish to record...\n");
            rec_ctx = recorder_init(0);
            if (!rec_ctx) {
                gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
                return;
            }
            recorder_select_window(rec_ctx->display, &target, &rec_ctx->x, &rec_ctx->y, &capture_width, &capture_height);
            recorder_cleanup(rec_ctx);
            rec_ctx = recorder_init(target);
            if (!rec_ctx) {
                gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
                return;
            }
        } else if (source == RECORD_SOURCE_MONITOR) {
            const char* mon = gui_get_monitor_name(gui);
            if (mon && get_monitor_geometry(mon, &cap_x, &cap_y, &capture_width, &capture_height) != 0) {
                /* Handle geometry not found if necessary */
            }
            rec_ctx = recorder_init(0);
            if (!rec_ctx) {
                gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
                return;
            }
            rec_ctx->x = cap_x;
            rec_ctx->y = cap_y;
        } else { /* RECORD_SOURCE_ALL */
            rec_ctx = recorder_init(0);
            if (!rec_ctx) {
                gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
                return;
            }
        }
        if (capture_width % 2 != 0) capture_width--;
        if (capture_height % 2 != 0) capture_height--;
        rec_ctx->width = capture_width;
        rec_ctx->height = capture_height;
        recorder_start(rec_ctx);

        /* Retrieve FPS and audio settings */
        int fps = gui_get_fps(gui);
        AudioCodec audio_codec = gui_get_audio_codec(gui);
        int audio_bitrate = DEFAULT_AUDIO_BIT_RATE;

        enc_ctx = encoder_init(quality, capture_width, capture_height, fps, 44100, 2, audio_codec, audio_bitrate);
        if (!enc_ctx) {
            recorder_cleanup(rec_ctx);
            gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
            return;
        }
        audio_ctx = audio_init();
        audio_start(audio_ctx);
        int audio_toggle_state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gui->audio_toggle));
        audio_set_capture(audio_ctx, audio_toggle_state);

        is_recording = 1;
        recording_start_time = time(NULL);
        pthread_create(&record_thread, NULL, record_thread_func, NULL);
        pthread_create(&audio_thread, NULL, audio_thread_func, NULL);
        g_timeout_add_seconds(1, update_info_callback, NULL);
    } else {
        gtk_button_set_label(GTK_BUTTON(toggle_button), "Start Recording");
        is_recording = 0;
        recorder_stop(rec_ctx);
        audio_stop(audio_ctx);
        pthread_join(record_thread, NULL);
        pthread_join(audio_thread, NULL);
        encoder_finalize(enc_ctx);

        char original_fullpath[2048];
        {
            const char *home = getenv("HOME");
            if (!home) home = ".";
            char dirpath[1024];
            snprintf(dirpath, sizeof(dirpath), "%s/Videos/Screenrecords/", home);
            snprintf(original_fullpath, sizeof(original_fullpath), "%s%s", dirpath, enc_ctx->filename);
        }
        char *new_basename = prompt_for_filename(GTK_WINDOW(gui->window), enc_ctx->filename);
        if (new_basename) {
            char new_fullpath[2048];
            if (new_basename[0] == '/') {
                strncpy(new_fullpath, new_basename, sizeof(new_fullpath)-1);
                new_fullpath[sizeof(new_fullpath)-1] = '\0';
            } else {
                const char *home = getenv("HOME");
                if (!home) home = ".";
                char dirpath[1024];
                snprintf(dirpath, sizeof(dirpath), "%s/Videos/Screenrecords/", home);
                snprintf(new_fullpath, sizeof(new_fullpath), "%s%s", dirpath, new_basename);
            }
            if (strcmp(new_fullpath, original_fullpath) != 0) {
                if (rename(original_fullpath, new_fullpath) != 0) {
                    perror("Error renaming file");
                } else {
                    strncpy(enc_ctx->filename, new_fullpath, sizeof(enc_ctx->filename)-1);
                    enc_ctx->filename[sizeof(enc_ctx->filename)-1] = '\0';
                }
            }
            free(new_basename);
        } else {
            if (remove(original_fullpath) != 0) {
                perror("Error deleting file");
            }
            gui_update_info(gui, "Recording cancelled and file deleted.");
        }
        recorder_cleanup(rec_ctx);
        audio_cleanup(audio_ctx);
        encoder_cleanup(enc_ctx);
        rec_ctx = NULL;
        audio_ctx = NULL;
        enc_ctx = NULL;
    }
}

/* Callback for the camera toggle button */
static void on_camera_toggle(GtkToggleButton *toggle_button, gpointer user_data) {
    if (gtk_toggle_button_get_active(toggle_button)) {
        gtk_button_set_label(GTK_BUTTON(toggle_button), "Camera Off");
        camera_running = 1;
        if (pthread_create(&webcam_thread, NULL, webcam_thread_func, NULL) != 0) {
            g_print("Error starting webcam preview thread\n");
        }
    } else {
        gtk_button_set_label(GTK_BUTTON(toggle_button), "Camera On");
        camera_running = 0;
        while (camera_thread_running)
            usleep(5000);
        pthread_join(webcam_thread, NULL);
        gtk_image_clear(GTK_IMAGE(gui->preview_area));
        gtk_widget_set_size_request(gui->preview_area, 0, 0);
    }
}

/* Callback for the audio toggle button */
static void on_audio_toggle(GtkToggleButton *toggle_button, gpointer user_data) {
    int state = gtk_toggle_button_get_active(toggle_button);
    if (audio_ctx)
        audio_set_capture(audio_ctx, state);
    gtk_button_set_label(GTK_BUTTON(toggle_button), state ? "Audio On" : "Audio Off");
}

/* Print help message */
static void print_help(const char *progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  --help           Display this help message and exit\n");
    printf("  --version        Output version information and exit\n");
    printf("  --debug          Enable additional debug output\n");
}

/* Parse command-line options using getopt_long */
static void parse_options(int argc, char **argv) {
    static struct option long_options[] = {
        {"help",    no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"debug",   no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "hvd", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                exit(0);
            case 'v':
                printf("%s version %s\n", argv[0], APP_VERSION);
                exit(0);
            case 'd':
                g_debug = 1;
                fprintf(stderr, "[DEBUG] Debug mode enabled\n");
                break;
            default:
                print_help(argv[0]);
                exit(1);
        }
    }
}

int main(int argc, char **argv) {
    parse_options(argc, argv);
    gtk_init(&argc, &argv);
    gui = gui_init();
    if (!gui)
        return 1;
    g_signal_connect(gui->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(gui->record_toggle, "toggled", G_CALLBACK(on_record_toggle), NULL);
    g_signal_connect(gui->camera_toggle, "toggled", G_CALLBACK(on_camera_toggle), NULL);
    g_signal_connect(gui->audio_toggle, "toggled", G_CALLBACK(on_audio_toggle), NULL);
    
    if (g_debug)
        fprintf(stderr, "[DEBUG] Entering main loop\n");
    gtk_main();
    if (g_debug)
        fprintf(stderr, "[DEBUG] Exiting main loop\n");
    gui_cleanup(gui);
    return 0;
}

