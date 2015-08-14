#include "camera.h"
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/base/gstbasesink.h>

struct _Camera {
    gint64 window_id;
    GstElement *vsink;
    GstElement *playsink;
    GstElement *pipeline;
    GstElement *source;
    GstState state;

    guint32 initialized : 1;
};

void camera_setup_pipeline(Camera *camera);

Camera *camera_new()
{
    gst_init(NULL, NULL);
    return g_malloc0(sizeof(Camera));
}

void camera_set_window_id(Camera *camera, gint64 window_id)
{
    g_return_if_fail(camera != NULL);

    camera->window_id = window_id;
}

void camera_start(Camera *camera)
{
    g_return_if_fail(camera != NULL);

    if (!camera->initialized)
        camera_setup_pipeline(camera);

    g_print("pipeline: %p\n", camera->pipeline);
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(camera->pipeline), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("State change failure at camera_start()\n");
    }
}

void camera_stop(Camera *camera)
{
    g_return_if_fail(camera != NULL);

    gst_element_set_state(camera->pipeline, GST_STATE_READY);
}

void camera_destroy(Camera *camera)
{
    if (camera == NULL)
        return;

    if (camera->pipeline) {
        gst_element_set_state(camera->pipeline, GST_STATE_NULL);
        gst_object_unref(camera->pipeline);
    }

    g_free(camera);
}

static GstBusSyncReply camera_bus_sync_handler(GstBus *bus, GstMessage *message, Camera *camera)
{
    if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_ELEMENT)
        return GST_BUS_PASS;
    if (!gst_structure_has_name(message->structure, "prepare-xwindow-id"))
        return GST_BUS_PASS;

    if (camera->window_id != 0) {
        g_print("set window id: %lld\n", camera->window_id);
        gst_x_overlay_set_window_handle(GST_X_OVERLAY(GST_MESSAGE_SRC(message)), camera->window_id);
    }
    else {
        g_printerr("camera_bus_sync_handler: should have window id now.\n");
    }

    gst_message_unref(message);

    return GST_BUS_DROP;
}

static void camera_bus_error(GstBus *bus, GstMessage *message, Camera *camera)
{
    GError *err;
    gchar *debug_info;

    gst_message_parse_error(message, &err, &debug_info);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);

    gst_element_set_state(camera->pipeline, GST_STATE_READY);
}

void camera_setup_pipeline(Camera *camera)
{
#if 0
    camera->pipeline = gst_pipeline_new(NULL);
    g_print("setup pipeline: %p\n", camera->pipeline);

    camera->vsink = gst_element_factory_make("xvimagesink", NULL);
    g_object_set(G_OBJECT(camera->vsink), "force-aspect-ratio", TRUE, NULL);

    camera->playsink = gst_element_factory_make("playsink", NULL);
    g_object_set(G_OBJECT(camera->playsink), "video-sink", camera->vsink, NULL);

    camera->source = gst_element_factory_make("v4l2src", NULL);

    /* FIXME: add videoscale */

    gst_bin_add_many(GST_BIN(camera->pipeline), camera->source, camera->playsink, NULL);
    if (!gst_element_link(camera->source, camera->playsink)) {
        g_printerr("Elements could not be linked. (source -> playsink)\n");
    }
#endif
    camera->pipeline = gst_parse_launch("v4l2src ! xvimagesink", NULL);

    GstBus *bus = gst_element_get_bus(GST_ELEMENT(camera->pipeline));
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler)camera_bus_sync_handler, camera);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(G_OBJECT(bus), "message::error",
            G_CALLBACK(camera_bus_error), camera);
    /* FIXME: error handling error, eos, state-changed */
    g_object_unref(bus);

    camera->initialized = 1;
}
