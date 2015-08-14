#pragma once

#include <glib.h>

typedef struct _Camera Camera;

Camera *camera_new();
void camera_set_window_id(Camera *camera, gint64 window_id);
void camera_start(Camera *camera);
void camera_stop(Camera *camera);
void camera_destroy(Camera *camera);
