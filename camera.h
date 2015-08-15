#pragma once

#include <glib.h>

typedef struct _Camera Camera;

Camera *camera_new();
void camera_set_window_id(Camera *camera, gint64 window_id);
void camera_start(Camera *camera);
void camera_stop(Camera *camera);
void camera_destroy(Camera *camera);

/* width, height, data, userdata*/
typedef void (*CAMERA_SNAPSHOT_TAKEN_CALLBACK)(guint, guint, guchar *, gpointer);
gboolean camera_save_snapshot_to_file(Camera *camera, const gchar *filename, guint width, guint height,
        CAMERA_SNAPSHOT_TAKEN_CALLBACK cb, gpointer userdata);
