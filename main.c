#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

enum ENTRIES {
    ENTRY_DIRECTORY,
    ENTRY_BASENAME,
    ENTRY_WIDTH,
    ENTRY_HEIGHT,
    ENTRY_N_SNAPSHOTS,
    ENTRY_INTERVAL,
    N_ENTRIES
};

enum STATUS_LABELS {
    LABEL_RUNNING_TIME,
    LABEL_TIMESTAMP_LAST,
    LABEL_TIMESTAMP_NEXT,
    N_STATUS_LABELS
};

struct {
    GtkWidget *main_window;
    GtkWidget *entries[N_ENTRIES];
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *live_view;
    GtkWidget *last_view;
    GtkWidget *running_area;
    GtkWidget *labels[N_STATUS_LABELS];
} widgets;

GPid timelapse_pid;
GFileMonitor *file_monitor;
gboolean is_running;

time_t start_time;
guint64 running_time;
time_t last_time;
time_t next_time;

guint timer_id;

typedef struct {
    gchar *filename;
    guint width;
    guint height;
    guint count;
    guint interval;
    gboolean valid;
} TimelapseConfig;

TimelapseConfig current_config;

void main_read_config(void)
{
    gchar *status_file_path = g_build_filename(
            g_get_user_config_dir(),
            "timelapse-status.conf",
            NULL);
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, status_file_path, G_KEY_FILE_NONE, NULL)) {
        current_config.filename = g_strdup("frame0000.jpeg");
        current_config.width = 640;
        current_config.height = 480;
        current_config.count = 100;
        current_config.interval = 2;
    }
    else {
        current_config.filename = g_key_file_get_string(kf, "Status", "filename", NULL);
        current_config.width = g_key_file_get_integer(kf, "Status", "width", NULL);
        current_config.height = g_key_file_get_integer(kf, "Status", "height", NULL);
        current_config.count = g_key_file_get_integer(kf, "Status", "count", NULL);
        current_config.interval = g_key_file_get_integer(kf, "Status", "interval", NULL);
    }

    g_free(status_file_path);
    g_key_file_free(kf);
}

void main_write_config(void)
{
    gchar *status_file_path = g_build_filename(
            g_get_user_config_dir(),
            "timelapse-status.conf",
            NULL);
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_string(kf, "Status", "filename", current_config.filename);
    g_key_file_set_integer(kf, "Status", "width", current_config.width);
    g_key_file_set_integer(kf, "Status", "height", current_config.height);
    g_key_file_set_integer(kf, "Status", "count", current_config.count);
    g_key_file_set_integer(kf, "Status", "interval", current_config.interval);

    g_key_file_save_to_file(kf, status_file_path, NULL);

    g_free(status_file_path);
    g_key_file_free(kf);
}

void main_child_stop(void);

void main_cleanup(void)
{
    main_child_stop();
}

const gchar *seconds_to_string(guint32 seconds)
{
    static gchar buffer[64];
    sprintf(buffer, "%02d:%02d:%02d", seconds/3600, (seconds%3600)/60, (seconds%60));
    return buffer;
}

static gboolean update_running_time(gpointer userdata)
{
    static time_t cur_time;
    time(&cur_time);
    gtk_label_set_text(GTK_LABEL(widgets.labels[LABEL_RUNNING_TIME]),
            seconds_to_string((guint32)difftime(cur_time, start_time)));
    return G_SOURCE_CONTINUE;
}

gboolean main_filename_matches_pattern(gchar *name1, gchar *name2)
{
    if (!name1 || !name2)
        return FALSE;
    gssize j = strlen(name1);
    if (strlen(name2) != j) {
        return FALSE;
    }
    for ( ; j >= 0; --j) {
        if (!g_ascii_isdigit(name1[j])) {
            if (name1[j] != name2[j]) {
                return FALSE;
            }
        }
        else {
            break;
        }
    }
    for ( ; j >= 0; --j) {
        if (g_ascii_isdigit(name1[j]) && g_ascii_isdigit(name2[j])) {
            continue;
        }
        break;
    }
    for ( ; j >= 0; --j) {
        if (name1[j] != name2[j]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void main_directory_changed_cb(GFileMonitor *monitor, GFile *file,
        GFile *other_file, GFileMonitorEvent event_type, gpointer userdata)
{
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) {
        gchar *bn1 = g_path_get_basename(current_config.filename);
        gchar *bn2 = g_file_get_basename(file);
        gboolean match = main_filename_matches_pattern(bn1, bn2);
        g_free(bn1);
        g_free(bn2);
        
        if (match) {
            gchar *path = g_file_get_path(file);
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_size(path, 320, 240, NULL);
            gtk_image_set_from_pixbuf(GTK_IMAGE(widgets.last_view), pixbuf);
            g_object_unref(G_OBJECT(pixbuf));
            struct stat st;
            struct tm *tm;
            gchar tbuf[256];
            gchar *text;
            time_t tlast;
            if (stat(path, &st) == 0) {
                tlast = st.st_mtim.tv_sec;
                tm = localtime(&tlast);
                strftime(tbuf, 255, "%x %T", tm);
                text = g_strdup_printf("%s (%s)", tbuf, path);
                gtk_label_set_text(GTK_LABEL(widgets.labels[LABEL_TIMESTAMP_LAST]), text);
                g_free(text);

                tlast += current_config.interval;
                tm = localtime(&tlast);
                strftime(tbuf, 255, "%x %T", tm);
                gtk_label_set_text(GTK_LABEL(widgets.labels[LABEL_TIMESTAMP_NEXT]), tbuf);
            }

            g_free(path);
        }
    }
}

static gboolean main_running_area_draw(GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    GtkAllocation alloc;
    if (is_running) {
        gtk_widget_get_allocation(widget, &alloc);

        cairo_translate(cr, alloc.width/2, alloc.height/2);
        double r = alloc.width > alloc.height ? alloc.height/2 : alloc.width/2;
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_arc(cr, 0, 0, r, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    return TRUE;
}

static gboolean main_live_view_draw(GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_rectangle(cr, 0.0, 0.0, alloc.width, alloc.height);
    cairo_fill(cr);

    return TRUE;
}

static void main_child_watch_cb(GPid pid, gint status, gpointer userdata)
{
    g_spawn_close_pid(timelapse_pid);
    timelapse_pid = 0;

    gtk_widget_set_sensitive(widgets.start_button, TRUE);
    gtk_widget_set_sensitive(widgets.stop_button, FALSE);

    main_child_stop();
}

gboolean main_child_start(const TimelapseConfig *config)
{
    gboolean ret;
    gchar *sizename = g_strdup_printf("%dx%d", config->width, config->height);
    gchar *rate = g_strdup_printf("%.6f", 1.0f/config->interval);
    gchar *count = g_strdup_printf("%d", config->count);
    gchar *argv[] = { "streamer",
        "-o", config->filename,
        "-s", sizename,
        "-j", "100",
        "-t", count,
        "-r", rate,
        NULL };
    ret = g_spawn_async(NULL, argv, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL,
            &timelapse_pid, NULL);
    if (!ret)
        goto done;

    g_child_watch_add(timelapse_pid, (GChildWatchFunc)main_child_watch_cb, NULL);

done:
    g_free(sizename);
    g_free(rate);

    return ret;
}

void main_child_stop(void)
{
    if (timelapse_pid > 0)
        kill(timelapse_pid, SIGKILL);

    if (timer_id) {
        g_source_remove(timer_id);
        timer_id = 0;
    }

    current_config.valid = FALSE;
    is_running = FALSE;
    if (GTK_IS_WIDGET(widgets.running_area))
        gtk_widget_queue_draw(widgets.running_area);
}

static void main_start_button_clicked(GtkButton *button, gpointer userdata)
{
    gchar *directory, *tmp;
    const gchar *filename, *width, *height, *count, *interval;

    directory = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(widgets.entries[ENTRY_DIRECTORY]));
    filename = gtk_entry_get_text(GTK_ENTRY(widgets.entries[ENTRY_BASENAME]));
    width = gtk_entry_get_text(GTK_ENTRY(widgets.entries[ENTRY_WIDTH]));
    height = gtk_entry_get_text(GTK_ENTRY(widgets.entries[ENTRY_HEIGHT]));
    count = gtk_entry_get_text(GTK_ENTRY(widgets.entries[ENTRY_N_SNAPSHOTS]));
    interval = gtk_entry_get_text(GTK_ENTRY(widgets.entries[ENTRY_INTERVAL]));

    current_config.valid = FALSE;
    g_free(current_config.filename);
    current_config.filename = NULL;

    if (directory) {
        tmp = g_build_filename(
                directory,
                filename,
                NULL);
        current_config.filename = g_filename_from_uri(tmp, NULL, NULL);
        g_free(tmp);
    }
    else {
        current_config.filename = g_strdup(filename);
    }
    gchar *endptr;
    GString *error_msg = g_string_new(NULL);

    current_config.width = (guint)strtoul(width, &endptr, 10);
    if (*endptr || endptr == width)
        g_string_append(error_msg, "Width must be a number.\n");

    current_config.height = (guint)strtoul(height, &endptr, 10);
    if (*endptr || endptr == height)
        g_string_append(error_msg, "Height must be a number.\n");

    current_config.count = (guint)strtoul(count, &endptr, 10);
    if (*endptr || endptr == count)
        g_string_append(error_msg, "Count must be a number.\n");

    current_config.interval = (guint)strtoul(interval, &endptr, 10);
    if (*endptr || endptr == interval)
        g_string_append(error_msg, "Interval must be a number.\n");

    gchar *msg = g_string_free(error_msg, FALSE);
    GtkWidget *dialog;
    if (msg[0]) {
        dialog = gtk_message_dialog_new(
                GTK_WINDOW(widgets.main_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                msg);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        goto done;
    }

    if (!main_child_start(&current_config)) {
        dialog = gtk_message_dialog_new(
                GTK_WINDOW(widgets.main_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_OK,
                "Could not spawn process. Maybe the program `streamer' is missing.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        goto done;
    }

    GFile *dir = NULL;
    if (!directory) {
        directory = g_get_current_dir();
    }
    gchar *scheme = g_uri_parse_scheme(directory);
    if (scheme)
        dir = g_file_new_for_uri(directory);
    else
        dir = g_file_new_for_path(directory);
    g_free(scheme);

    if (file_monitor) {
        g_file_monitor_cancel(G_FILE_MONITOR(file_monitor));
        g_object_unref(G_OBJECT(file_monitor));
    }
    file_monitor = g_file_monitor_directory(dir,
            G_FILE_MONITOR_NONE,
            NULL, NULL);
    g_signal_connect(G_OBJECT(file_monitor), "changed",
            G_CALLBACK(main_directory_changed_cb), NULL);
    g_object_unref(G_OBJECT(dir));

    timer_id = g_timeout_add_seconds(1, (GSourceFunc)update_running_time, NULL);
    time(&start_time);

    gtk_widget_set_sensitive(widgets.start_button, FALSE);
    gtk_widget_set_sensitive(widgets.stop_button, TRUE);

    is_running = TRUE;
    gtk_widget_queue_draw(widgets.running_area);

done:
    g_free(msg);
    g_free(directory);
}

static void main_stop_button_clicked(GtkButton *button, gpointer userdata)
{
    gtk_widget_set_sensitive(widgets.start_button, TRUE);
    gtk_widget_set_sensitive(widgets.stop_button, FALSE);

    main_child_stop();
}

void main_create_window(void)
{
    widgets.main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(widgets.main_window), "destroy",
            G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *grid = gtk_grid_new();
    GtkWidget *label;
    GtkWidget *hbox;
    GdkPixbuf *pixbuf;
    GtkWidget *label_grid = gtk_grid_new();
    gchar tbuf[256];
    gchar nbuf[256];
    struct tm *tm;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 3);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 3);

    gtk_grid_set_row_spacing(GTK_GRID(label_grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(label_grid), 3);
    gtk_container_set_border_width(GTK_CONTAINER(label_grid), 3);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);

    widgets.live_view = gtk_drawing_area_new();
    gtk_widget_set_size_request(widgets.live_view, 320, 240);
    g_signal_connect(G_OBJECT(widgets.live_view), "draw",
            G_CALLBACK(main_live_view_draw), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), widgets.live_view, TRUE, TRUE, 3);

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 320, 240);
    gdk_pixbuf_fill(pixbuf, 0x000000ff);

    widgets.last_view = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(G_OBJECT(pixbuf));

    gtk_box_pack_start(GTK_BOX(hbox), widgets.last_view, TRUE, TRUE, 3);

    gtk_grid_attach(GTK_GRID(grid), hbox, 0, 0, 3, 1);

    /* Clock */
    label = gtk_label_new("Running time:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(label_grid), label, 0, 0, 1, 1);
    widgets.labels[LABEL_RUNNING_TIME] = gtk_label_new(seconds_to_string(running_time));
    gtk_widget_set_halign(widgets.labels[LABEL_RUNNING_TIME], GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(label_grid), widgets.labels[LABEL_RUNNING_TIME], 1, 0, 1, 1);

    tm = localtime(&last_time);
    strftime(tbuf, 255, "%x %T", tm);
    label = gtk_label_new("Last image:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(label_grid), label, 0, 1, 1, 1);
    widgets.labels[LABEL_TIMESTAMP_LAST] = gtk_label_new(tbuf);
    gtk_widget_set_halign(widgets.labels[LABEL_TIMESTAMP_LAST], GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(label_grid), widgets.labels[LABEL_TIMESTAMP_LAST], 1, 1, 1, 1);

    tm = localtime(&next_time);
    strftime(tbuf, 255, "%x %T", tm);
    label = gtk_label_new("Next image:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(label_grid), label, 0, 2, 1, 1);
    widgets.labels[LABEL_TIMESTAMP_NEXT] = gtk_label_new(tbuf);
    gtk_widget_set_halign(widgets.labels[LABEL_TIMESTAMP_NEXT], GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(label_grid), widgets.labels[LABEL_TIMESTAMP_NEXT], 1, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), label_grid, 0, 1, 3, 1);

    /* Settings */

    gchar *cfg_dir = g_path_get_dirname(current_config.filename);
    gchar *cfg_file = g_path_get_basename(current_config.filename);

    label = gtk_label_new("Directory:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
    widgets.entries[ENTRY_DIRECTORY] = gtk_file_chooser_button_new("Choose directory",
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widgets.entries[ENTRY_DIRECTORY]),
            cfg_dir);
    gtk_grid_attach(GTK_GRID(grid), widgets.entries[ENTRY_DIRECTORY], 1, 2, 1, 1);

    label = gtk_label_new("Name:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);
    widgets.entries[ENTRY_BASENAME] = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(widgets.entries[ENTRY_BASENAME]), cfg_file);
    gtk_grid_attach(GTK_GRID(grid), widgets.entries[ENTRY_BASENAME], 1, 3, 1, 1);

    g_free(cfg_dir);
    g_free(cfg_file);

    label = gtk_label_new("Size:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 4, 1, 1);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    widgets.entries[ENTRY_WIDTH] = gtk_entry_new();
    snprintf(nbuf, 255, "%u", current_config.width);
    gtk_entry_set_text(GTK_ENTRY(widgets.entries[ENTRY_WIDTH]), nbuf);
    label = gtk_label_new(" x ");
    widgets.entries[ENTRY_HEIGHT] = gtk_entry_new();
    snprintf(nbuf, 255, "%u", current_config.height);
    gtk_entry_set_text(GTK_ENTRY(widgets.entries[ENTRY_HEIGHT]), nbuf);
    gtk_box_pack_start(GTK_BOX(hbox), widgets.entries[ENTRY_WIDTH], TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(hbox), widgets.entries[ENTRY_HEIGHT], TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(grid), hbox, 1, 4, 1, 1);

    label = gtk_label_new("Image count:");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 5, 1, 1);
    widgets.entries[ENTRY_N_SNAPSHOTS] = gtk_entry_new();
    snprintf(nbuf, 255, "%u", current_config.count);
    gtk_entry_set_text(GTK_ENTRY(widgets.entries[ENTRY_N_SNAPSHOTS]), nbuf);
    gtk_grid_attach(GTK_GRID(grid), widgets.entries[ENTRY_N_SNAPSHOTS], 1, 5, 1, 1);

    label = gtk_label_new("Intervall (seconds):");
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 6, 1, 1);
    widgets.entries[ENTRY_INTERVAL] = gtk_entry_new();
    snprintf(nbuf, 255, "%u", current_config.interval);
    gtk_entry_set_text(GTK_ENTRY(widgets.entries[ENTRY_INTERVAL]), nbuf);
    gtk_grid_attach(GTK_GRID(grid), widgets.entries[ENTRY_INTERVAL], 1, 6, 1, 1);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    widgets.start_button = gtk_button_new_with_label("Start");
    g_signal_connect(G_OBJECT(widgets.start_button), "clicked",
            G_CALLBACK(main_start_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), widgets.start_button, FALSE, FALSE, 3);

    widgets.stop_button = gtk_button_new_with_label("Stop");
    g_signal_connect(G_OBJECT(widgets.stop_button), "clicked",
            G_CALLBACK(main_stop_button_clicked), NULL);
    gtk_widget_set_sensitive(widgets.stop_button, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), widgets.stop_button, FALSE, FALSE, 3);

    gtk_grid_attach(GTK_GRID(grid), hbox, 0, 7, 3, 1);

    widgets.running_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(widgets.running_area, 100, 100);
    g_signal_connect(G_OBJECT(widgets.running_area), "draw",
            G_CALLBACK(main_running_area_draw), NULL);
    gtk_grid_attach(GTK_GRID(grid), widgets.running_area, 2, 2, 1, 5);

    gtk_container_add(GTK_CONTAINER(widgets.main_window), grid);

    gtk_widget_show_all(widgets.main_window);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    setlocale(LC_NUMERIC, "C");

    start_time = time(NULL);
    next_time = start_time;
    last_time = start_time;
    running_time = 0;

    main_read_config();
    main_create_window();

    gtk_main();

    main_write_config();
    main_cleanup();

    if (file_monitor) {
        g_file_monitor_cancel(G_FILE_MONITOR(file_monitor));
        g_object_unref(G_OBJECT(file_monitor));
    }

    return 0;
}
