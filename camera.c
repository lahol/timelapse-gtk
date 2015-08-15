#include "camera.h"
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/base/gstbasesink.h>

#include <Imlib2.h>

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

static void camera_decoder_pad_added(GstElement *src, GstPad *new_pad, Camera *camera)
{
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;
    GstPad *sink_pad = NULL;
    GstPadLinkReturn ret;

    new_pad_caps = gst_pad_get_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    GstElementClass *klass = GST_ELEMENT_GET_CLASS(camera->playsink);
    GstPadTemplate *templ = NULL;

    if (g_str_has_prefix(new_pad_type, "audio"))
        templ = gst_element_class_get_pad_template(klass, "audio_sink");
    else if (g_str_has_prefix(new_pad_type, "video"))
        templ = gst_element_class_get_pad_template(klass, "video_sink");

    if (templ) {
        sink_pad = gst_element_request_pad(camera->playsink, templ, NULL, NULL);

        if (gst_pad_is_linked(sink_pad)) {
            g_print("Already linked\n");
            goto done;
        }

        ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret))
            g_print("Linking failed\n");
    }
    
done:
    if (new_pad_caps)
        gst_caps_unref(new_pad_caps);
    if (sink_pad)
        gst_object_unref(sink_pad);
}

void camera_setup_pipeline(Camera *camera)
{
#if 1
    camera->pipeline = gst_pipeline_new(NULL);
    camera->vsink = gst_element_factory_make("xvimagesink", NULL);
    g_object_set(G_OBJECT(camera->vsink), "force-aspect-ratio", TRUE, NULL);

    camera->playsink = gst_element_factory_make("playsink", NULL);
    g_object_set(G_OBJECT(camera->playsink), "video-sink", camera->vsink, NULL);

    camera->source = gst_element_factory_make("v4l2src", NULL);

    GstElement *decoder = gst_element_factory_make("decodebin2", NULL);
    g_signal_connect(G_OBJECT(decoder), "pad-added",
            G_CALLBACK(camera_decoder_pad_added), camera);

    /* FIXME: add videoscale */

    gst_bin_add_many(GST_BIN(camera->pipeline), camera->source, decoder, camera->playsink, NULL);
    if (!gst_element_link(camera->source, decoder)) {
        g_printerr("Elements could not be linked. (source -> playsink)\n");
    }
#else
    camera->pipeline = gst_parse_launch("v4l2src ! xvimagesink", NULL);
#endif
    GstBus *bus = gst_element_get_bus(GST_ELEMENT(camera->pipeline));
    gst_bus_set_sync_handler(bus, (GstBusSyncHandler)camera_bus_sync_handler, camera);
    gst_bus_add_signal_watch(bus);
    /* FIXME: error handling error, eos, state-changed */
    g_signal_connect(G_OBJECT(bus), "message::error",
            G_CALLBACK(camera_bus_error), camera);
    g_object_unref(bus);

    camera->initialized = 1;
}

gboolean camera_save_snapshot_to_file(Camera *camera, const gchar *filename, guint width, guint height,
        CAMERA_SNAPSHOT_TAKEN_CALLBACK cb, gpointer userdata)
{
    g_return_val_if_fail(camera != NULL, FALSE);

    GstCaps *caps = NULL;
    GstBuffer *buffer = NULL;
    gint w = 0, h = 0;
    GstStructure *s;
    Imlib_Image image = NULL;
    gboolean result = FALSE;

    caps = gst_caps_new_simple("video/x-raw-rgb",
            "format", G_TYPE_STRING, "ARGB",
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
            NULL);
    if (width)
        gst_caps_set_simple(caps,
                "width", G_TYPE_INT, width,
                NULL);
    if (height)
        gst_caps_set_simple(caps,
                "height", G_TYPE_INT, height,
                NULL);

    g_signal_emit_by_name(camera->playsink, "convert-frame", caps, &buffer);
    gst_caps_unref(caps);
    caps = NULL;

    if (!buffer)
        goto done;

    caps = gst_buffer_get_caps(buffer);
    if (caps == NULL)
        goto done;

    s = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(s, "width", &w);
    gst_structure_get_int(s, "height", &h);

    /* we get the color in rgba*/
#define SWAP_BYTES24(c) (c)=(((c) & 0xff00ff00) | (((c) >> 16)&0xff) | (((c) <<16)&0xff0000))
    gsize j;
    guint32 *cur;
    for (j=0, cur = (guint32 *)buffer->data; j < w*h; ++j, ++cur)
        SWAP_BYTES24(*cur);
#undef SWAP_BYTES24

    /* save buffer to file */
    image = imlib_create_image_using_data(w, h, (DATA32 *)buffer->data);
    imlib_context_set_image(image);
/*    imlib_image_set_format("jpeg");*/
    Imlib_Load_Error err = 0;
    imlib_save_image_with_error_return(filename, &err);
    if (err)
        g_print("Error loading image: %d\n", err);
    imlib_free_image();

    if (cb)
        cb(w, h, buffer->data, userdata);

    result = TRUE;

done:
    if (caps)
        gst_caps_unref(caps);
    if (buffer)
        gst_buffer_unref(buffer);
    return result;
}

