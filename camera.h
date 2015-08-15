#pragma once

#include <glib.h>

typedef struct _Camera Camera;

Camera *camera_new();
void camera_set_window_id(Camera *camera, gint64 window_id);
void camera_start(Camera *camera);
void camera_stop(Camera *camera);
void camera_destroy(Camera *camera);

gboolean camera_save_snapshot_to_file(Camera *camera, const gchar *filename, guint width, guint height);
