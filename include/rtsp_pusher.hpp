#ifndef RTSP_PUSHER_HPP
#define RTSP_PUSHER_HPP

#include <stdexcept>
#include <iostream>
#include <future>
#include <functional>
#include <gst/gst.h>
#include <gst/audio/audio.h>

class RtspPusher
{
public:
  RtspPusher(const std::string &rtsp_url, const std::function<int(uint8_t *buffer, int chunk_size, int sample_rate)> &data_provider, GstAudioFormat audio_format, int chunk_size = 1024, int sample_rate = 44100);

  RtspPusher(RtspPusher &&other);

  RtspPusher &operator=(RtspPusher &&other);

  void start();

  void stop();

  ~RtspPusher();

private:
  struct GstreamerData
  {
    GstElement *pipeline, *app_source, *tee, *audio_queue, *audio_convert1,
        *audio_resample, *audio_encode, *audio_parse, *audio_sink;
    guint64 num_samples; // Number of samples generated so far (for timestamp generation)
    guint sourceid;
    GMainLoop *main_loop;
    GstPad *tee_audio_pad, *queue_audio_pad, *parse_src_pad, *rtsp_sink_pad;
    GstAudioInfo info;
    GstCaps *audio_caps;
    GstBus *bus;
    std::string rtsp_url;
    std::function<int(uint8_t *, int, int)> data_provider;
    int chunk_size;
    int sample_rate;
  };

  std::future<void> g_main_loop_future;
  std::unique_ptr<GstreamerData> data_ptr;

  static gboolean push_data(GstreamerData *data);

  static void start_feed(GstElement *source, guint size, GstreamerData *data);

  static void stop_feed(GstElement *source, GstreamerData *data);

  static void error_cb(GstBus *bus, GstMessage *msg, GstreamerData *data);
};
#endif // RTSP_PUSHER_HPP