#ifndef __AUDIO_HANDLER_H__
#define __AUDIO_HANDLER_H__

#include "constants.h"
#include "gstcefsrc.h"
#include <iostream>
#include <sstream>
#include <stdio.h>

class AudioHandler : public CefAudioHandler {
public:
  AudioHandler(GstCefSrc *element) : mElement(element) {}

  ~AudioHandler() {}

  bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
                          CefAudioParameters &params) override {
    // GST_INFO("GetAudioParameters");
    params.sample_rate = mElement->audio_rate;
    params.channel_layout = (cef_channel_layout_t)mElement->channel_layout;
    switch (mElement->channel_layout) {
    case 1:
      params.channel_layout = CEF_CHANNEL_LAYOUT_MONO;
      break;
    case 2:
      params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
      break;
    default:
      params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
    }
    return true;
  }

  void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
                            const CefAudioParameters &params,
                            int channels) override {
    GstStructure *s = gst_structure_new("cef-audio-stream-start", "channels",
                                        G_TYPE_INT, channels, "rate",
                                        G_TYPE_INT, params.sample_rate, NULL);
    GstEvent *event = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, s);

    GST_INFO("Audio stream started with %d channels and %d Hz", channels,
             params.sample_rate);

    mRate = params.sample_rate;
    mChannels = channels;
    mCurrentTime = GST_CLOCK_TIME_NONE;

    GST_OBJECT_LOCK(mElement);
    mElement->audio_events = g_list_append(mElement->audio_events, event);
    GST_OBJECT_UNLOCK(mElement);
  }

  void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, const float **data,
                           int frames, int64_t pts) override {
    GstBuffer *buf;
    GstMapInfo info;
    gint i, j;

    GST_LOG_OBJECT(mElement, "Handling audio stream packet with %d frames",
                   frames);

    GST_INFO("Handling audio stream packet with %d frames", frames);

    // TODO: I have to do this otherwise the pipeline will deadlock
    if (mElement->is_playing == FALSE) {
      GST_LOG_OBJECT(mElement, "Not playing, dropping audio buffer");
      return;
    }

    buf = gst_buffer_new_allocate(NULL, mChannels * frames * 4, NULL);

    gst_buffer_map(buf, &info, GST_MAP_WRITE);
    for (i = 0; i < mChannels; i++) {
      gfloat *cdata = (gfloat *)data[i];

      for (j = 0; j < frames; j++) {
        memcpy(info.data + j * 4 * mChannels + i * 4, &cdata[j], 4);
      }
    }
    gst_buffer_unmap(buf, &info);

    GST_OBJECT_LOCK(mElement);

    if (!GST_CLOCK_TIME_IS_VALID(mCurrentTime)) {
      mCurrentTime = gst_util_uint64_scale(mElement->n_frames,
                                           mElement->vinfo.fps_d * GST_SECOND,
                                           mElement->vinfo.fps_n);
    }

    GST_BUFFER_PTS(buf) = mCurrentTime;
    GST_BUFFER_DURATION(buf) = gst_util_uint64_scale(frames, GST_SECOND, mRate);
    mCurrentTime += GST_BUFFER_DURATION(buf);

    if (!mElement->audio_buffers) {
      mElement->audio_buffers = gst_buffer_list_new();
    }

    gst_buffer_list_add(mElement->audio_buffers, buf);
    GST_OBJECT_UNLOCK(mElement);

    GST_LOG_OBJECT(mElement, "Handled audio stream packet");
  }

  void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override {}

  void OnAudioStreamError(CefRefPtr<CefBrowser> browser,
                          const CefString &message) override {
    GST_WARNING_OBJECT(mElement, "Audio stream error: %s",
                       message.ToString().c_str());
    g_signal_emit_by_name(mElement, kOnAudioStreamError,
                          message.ToString().c_str());
  }

private:
  GstCefSrc *mElement;
  GstClockTime mCurrentTime;
  gint mRate;
  gint mChannels;
  IMPLEMENT_REFCOUNTING(AudioHandler);
};

#endif // __AUDIO_HANDLER_H__