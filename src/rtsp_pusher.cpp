#include "../include/rtsp_pusher.hpp"

RtspPusher::RtspPusher(const std::string &rtsp_url, const std::function<int(uint8_t *buffer, int chunk_size, int sample_rate)> &data_provider, GstAudioFormat audio_format, int chunk_size, int sample_rate) : data_ptr(std::make_unique<GstreamerData>())
{
  gst_init(nullptr, nullptr);

  data_ptr->rtsp_url = rtsp_url;
  data_ptr->data_provider = data_provider;
  data_ptr->chunk_size = chunk_size;
  data_ptr->sample_rate = sample_rate;
  data_ptr->app_source = gst_element_factory_make("appsrc", "audio_source");
  data_ptr->tee = gst_element_factory_make("tee", "tee");
  data_ptr->audio_queue = gst_element_factory_make("queue", "audio_queue");
  data_ptr->audio_convert1 =
      gst_element_factory_make("audioconvert", "audio_convert1");
  data_ptr->audio_resample =
      gst_element_factory_make("audioresample", "audio_resample");
  data_ptr->audio_encode = gst_element_factory_make("opusenc", "opus-encode"),
  data_ptr->audio_parse = gst_element_factory_make("opusparse", "opus-parse"),
  data_ptr->audio_sink = gst_element_factory_make("rtspclientsink", "rtsp-client"),

  data_ptr->pipeline = gst_pipeline_new("main-pipeline");

  if (!data_ptr->pipeline || !data_ptr->app_source || !data_ptr->tee || !data_ptr->audio_queue || !data_ptr->audio_convert1 || !data_ptr->audio_resample || !data_ptr->audio_encode || !data_ptr->audio_parse || !data_ptr->audio_sink)
  {
    g_printerr("Not all elements could be created.\n");
    throw std::runtime_error("Not all elements could be created");
  }

  g_object_set(data_ptr->audio_sink, "location", data_ptr->rtsp_url.c_str(), nullptr);
  gst_audio_info_set_format(&(data_ptr->info), audio_format, data_ptr->sample_rate, 1, nullptr);
  data_ptr->audio_caps = gst_audio_info_to_caps(&(data_ptr->info));
  g_object_set(data_ptr->app_source, "caps", data_ptr->audio_caps, "format", GST_FORMAT_TIME,
               nullptr);
  // g_object_set(data_ptr->app_source, "is-live", true, nullptr);
  // g_object_set(data_ptr->app_source, "do-timestamp", true, nullptr);
  // g_object_set(data_ptr->app_source, "min-latency", (gint64)0, nullptr);
  g_signal_connect(data_ptr->app_source, "need-data", G_CALLBACK(start_feed),
                   data_ptr.get());
  g_signal_connect(data_ptr->app_source, "enough-data", G_CALLBACK(stop_feed),
                   data_ptr.get());

  gst_caps_unref(data_ptr->audio_caps);

  gst_bin_add_many(GST_BIN(data_ptr->pipeline), data_ptr->app_source, data_ptr->tee,
                   data_ptr->audio_queue, data_ptr->audio_convert1, data_ptr->audio_resample,
                   data_ptr->audio_encode, data_ptr->audio_parse,
                   data_ptr->audio_sink, nullptr);
  if (gst_element_link_many(data_ptr->app_source, data_ptr->tee, nullptr) != true || gst_element_link_many(data_ptr->audio_queue, data_ptr->audio_convert1, data_ptr->audio_resample, data_ptr->audio_encode, data_ptr->audio_parse, nullptr) != true)
  {
    g_printerr("Elements could not be linked.\n");
    gst_object_unref(data_ptr->pipeline);
    throw std::runtime_error("Elements could not be linked");
  }

  data_ptr->tee_audio_pad = gst_element_request_pad_simple(data_ptr->tee, "src_%u");
  data_ptr->queue_audio_pad = gst_element_get_static_pad(data_ptr->audio_queue, "sink");
  data_ptr->parse_src_pad = gst_element_get_static_pad(data_ptr->audio_parse, "src");
  data_ptr->rtsp_sink_pad = gst_element_request_pad_simple(data_ptr->audio_sink, "sink_%u");
  if (gst_pad_link(data_ptr->tee_audio_pad, data_ptr->queue_audio_pad) != GST_PAD_LINK_OK ||
      gst_pad_link(data_ptr->parse_src_pad, data_ptr->rtsp_sink_pad) != GST_PAD_LINK_OK)
  {
    g_printerr("Tee could not be linked\n");
    gst_object_unref(data_ptr->pipeline);
    throw std::runtime_error("Tee could not be linked");
  }
  gst_object_unref(data_ptr->queue_audio_pad);
  gst_object_unref(data_ptr->parse_src_pad);

  data_ptr->bus = gst_element_get_bus(data_ptr->pipeline);
  gst_bus_add_signal_watch(data_ptr->bus);
  g_signal_connect(G_OBJECT(data_ptr->bus), "message::error", (GCallback)error_cb,
                   data_ptr.get());
  gst_object_unref(data_ptr->bus);

  data_ptr->main_loop = g_main_loop_new(nullptr, false);
}

RtspPusher::RtspPusher(RtspPusher &&other) : data_ptr(std::move(other.data_ptr)), g_main_loop_future(std::move(other.g_main_loop_future))
{
}

RtspPusher &RtspPusher::operator=(RtspPusher &&other)
{
  data_ptr = std::move(other.data_ptr);
  g_main_loop_future = std::move(other.g_main_loop_future);
  return *this;
}

// TODO: fix resume
void RtspPusher::start()
{
  g_main_loop_future = std::async(std::launch::async, [this]()
                                  { g_main_loop_run(data_ptr->main_loop); });
  GstStateChangeReturn st = gst_element_set_state(data_ptr->pipeline, GST_STATE_PLAYING);
  if (st == GST_STATE_CHANGE_FAILURE)
  {
    g_printerr("Unable to set the pipeline to the playing state.\n");
    gst_object_unref(data_ptr->pipeline);
    throw std::runtime_error("Unable to set the pipeline to the playing state");
  }
}

void RtspPusher::stop()
{
  GstStateChangeReturn st = gst_element_set_state(data_ptr->pipeline, GST_STATE_READY);
  if (st == GST_STATE_CHANGE_FAILURE)
  {
    g_printerr("Unable to set the pipeline to the null state.\n");
    gst_object_unref(data_ptr->pipeline);
    throw std::runtime_error("Unable to set the pipeline to the null state");
  }
}

RtspPusher::~RtspPusher()
{
  if (data_ptr != nullptr)
  {
    GstStateChangeReturn st = gst_element_set_state(data_ptr->pipeline, GST_STATE_NULL);
    if (st == GST_STATE_CHANGE_FAILURE)
    {
      g_printerr("Unable to set the pipeline to the null state.\n");
      gst_object_unref(data_ptr->pipeline);
    }
    gst_element_release_request_pad(data_ptr->tee, data_ptr->tee_audio_pad);
    gst_element_release_request_pad(data_ptr->audio_sink, data_ptr->rtsp_sink_pad);
    gst_object_unref(data_ptr->tee_audio_pad);
    gst_object_unref(data_ptr->rtsp_sink_pad);

    gst_object_unref(data_ptr->pipeline);
    g_main_loop_quit(data_ptr->main_loop);
    g_main_loop_future.wait();
  }
}

gboolean RtspPusher::push_data(GstreamerData *data)
{
  GstBuffer *buffer = gst_buffer_new_and_alloc(data->chunk_size);

  GstMapInfo map;
  gst_buffer_map(buffer, &map, GST_MAP_WRITE);

  const auto num_samples = data->data_provider(map.data, data->chunk_size, data->sample_rate);

  gst_buffer_unmap(buffer, &map);
  data->num_samples += num_samples;

  GST_BUFFER_TIMESTAMP(buffer) =
      gst_util_uint64_scale(data->num_samples, GST_SECOND, data->sample_rate);
  GST_BUFFER_DURATION(buffer) =
      gst_util_uint64_scale(num_samples, GST_SECOND, data->sample_rate);

  GstFlowReturn ret;
  g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);

  gst_buffer_unref(buffer);

  if (ret != GST_FLOW_OK)
  {
    return false;
  }

  return true;
}

void RtspPusher::start_feed(GstElement *source, guint size, GstreamerData *data)
{
  if (data->sourceid == 0)
  {
    data->sourceid = g_idle_add((GSourceFunc)push_data, data);
  }
}

void RtspPusher::stop_feed(GstElement *source, GstreamerData *data)
{
  if (data->sourceid != 0)
  {
    g_source_remove(data->sourceid);
    data->sourceid = 0;
  }
}

void RtspPusher::error_cb(GstBus *bus, GstMessage *msg, GstreamerData *data)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error(msg, &err, &debug_info);
  g_printerr("Error received from element %s: %s\n",
             GST_OBJECT_NAME(msg->src), err->message);
  g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error(&err);
  g_free(debug_info);

  g_main_loop_quit(data->main_loop);
}