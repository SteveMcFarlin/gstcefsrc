#include <iostream>
#include <sstream>
#include <stdio.h>

#include <include/base/cef_bind.h>
#include <include/base/cef_callback_helpers.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/wrapper/cef_helpers.h>
#include <include/wrapper/cef_message_router.h>
#include <string>

#include "audiohandler.h"
#include "browserclient.h"
#include "constants.h"
#include "gstcefaudiometa.h"
#include "gstcefsrc.h"
#include "messagehandler.h"
#include "renderhandler.h"
#include "requesthandler.h"
#include "util.h"

GST_DEBUG_CATEGORY_STATIC(cef_src_debug);
#define GST_CAT_DEFAULT cef_src_debug

GST_DEBUG_CATEGORY_STATIC(cef_console_debug);

#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1
#define DEFAULT_URL "https://www.google.com"
#define DEFAULT_GPU FALSE
#define DEFAULT_CHROMIUM_DEBUG_PORT -1
#define DEFAULT_LOG_SEVERITY LOGSEVERITY_DISABLE
#define DEFAULT_SANDBOX FALSE
#define DEFAULT_AUDIO_RATE 48000
#define DEFAULT_AUDIO_CHANNELS 2
#define DEFAULT_AUDIO_FRAMES_PER_BUFFER 1024

static gboolean cef_inited = FALSE;
static gboolean init_result = FALSE;
static GMutex init_lock;
static GCond init_cond;

#define GST_TYPE_CEF_LOG_SEVERITY_MODE (gst_cef_log_severity_mode_get_type())

static GType gst_cef_log_severity_mode_get_type(void) {
  static GType type = 0;
  static const GEnumValue values[] = {
      {LOGSEVERITY_DEBUG, "debug / verbose cef log severity", "debug"},
      {LOGSEVERITY_INFO, "info cef log severity", "info"},
      {LOGSEVERITY_WARNING, "warning cef log severity", "warning"},
      {LOGSEVERITY_ERROR, "error cef log severity", "error"},
      {LOGSEVERITY_FATAL, "fatal cef log severity", "fatal"},
      {LOGSEVERITY_DISABLE, "disable cef log severity", "disable"},
      {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static("GstCefLogSeverityMode", values);
  }
  return type;
}

enum {
  PROP_0,
  PROP_URL,
  PROP_GPU,
  PROP_CHROMIUM_DEBUG_PORT,
  PROP_CHROME_EXTRA_FLAGS,
  PROP_SANDBOX,
  PROP_JS_FLAGS,
  PROP_LOG_SEVERITY,
  PROP_CEF_CACHE_LOCATION,
  PROP_AUDIO_LAYOUT,
  PROP_AUDIO_RATE,
  PROP_AUDIO_FRAMES_PER_BUFFER,
};

enum {
  /* Signals*/
  SIGNAL_ON_CONSOLE_MESSAGE = 0,
  // Uncaught JS exception
  SIGNAL_ON_UNCAUGHT_EXCEPTION,
  // Called when a JS function in the browser is called
  SIGNAL_ON_CALLED_FUNCTION,
  // Called after a new browser is created.
  SIGNAL_ON_AFTER_CREATED,
  // Called before a browser is destroyed.
  SIGNAL_ON_BEFORE_CLOSE,
  // Called on loading state change
  SIGNAL_ON_LOADING_STATE_CHANGE,
  // Called on load start
  SIGNAL_ON_LOAD_START,
  // Called on load end
  SIGNAL_ON_LOAD_END,
  // Called on load error
  SIGNAL_ON_LOAD_ERROR,
  // Called when the overall page loading progress has changed.
  // Ranges from 0.0 to 1.0.
  SIGNAL_ON_LOAD_PROGRESS,
  // Called on an audio error
  SIGNAL_ON_AUDIO_ERROR,
  // Called to query Upstage
  SIGNAL_UPSTAGE_QUERY,
  // Called to cancel a query
  SIGNAL_CANCEL_QUERY,
  // Called to indicate audio error
  SIGNAL_ON_AUDIO_STREAM_ERROR,
  // Called when the render process crashes
  SIGNAL_ON_RENDER_PROCESS_TERMINATED,

  /**
   * SIGNALS for upstage to resolve promises/async calls.
   * These are triggered from the render process.
   * executing the joinRoom() JS function. See OnProcessMessageReceived()
   * In CefClient/CefBrowserClient and
   * RenderProcessHandler::OnProcessMessageReceived().
   */
  SIGNAL_RESOLVE_CALL_FUNCTION,
  SIGNAL_RESOLVE_EXPOSE_FUNCTION,
  SIGNAL_RESOLVE_EXECUTE_JS,

  /* Actions */
  SIGNAL_ACTION_CALL_FUNCTION,
  SIGNAL_ACTION_EXPOSE_FUNCTION,
  SIGNAL_ACTION_EXECUTE_JS,

  SIGNAL_LAST
};

// We really don't need to store the signal ID's as we use emit_by_name.
static guint cefsrc_signals[SIGNAL_LAST] = {0};

// struct SignalMap {
//   static std::map<int, std::string> create_map() {
//     std::map<int, std::string> m;
//     m[1] = "2";
//     m[3] = "4";
//     m[5] = "6";
//     return m;
//   }
//   static const std::map<int, std::string> myMap;
// };

// const std::map<int, std::string> SignalMap::myMap = SignalMap::create_map();

#define gst_cef_src_parent_class parent_class
G_DEFINE_TYPE(GstCefSrc, gst_cef_src, GST_TYPE_PUSH_SRC);

#define CEF_VIDEO_CAPS                                                         \
  "video/x-raw, format=BGRA, width=[1, 2147483647], height=[1, 2147483647], "  \
  "framerate=[1/1, 60/1], pixel-aspect-ratio=1/1"
#define CEF_AUDIO_CAPS                                                         \
  "audio/x-raw, format=F32LE, rate=[1, 2147483647], channels=[1, "             \
  "2147483647], layout=interleaved"

static GstStaticPadTemplate gst_cef_src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(CEF_VIDEO_CAPS));

gchar *get_plugin_base_path() {
  GstPlugin *plugin = gst_registry_find_plugin(gst_registry_get(), "cef");
  gchar *base_path = g_path_get_dirname(gst_plugin_get_filename(plugin));
  gst_object_unref(plugin);
  return base_path;
}

class DisplayHandler : public CefDisplayHandler {
public:
  DisplayHandler(GstCefSrc *element) : mElement(element) {}

  ~DisplayHandler() = default;

  virtual void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                                       double progress) override {
    GST_INFO("CEF loading progress: %f \n", progress);
    g_signal_emit_by_name(mElement, kOnLoadProgress, progress);
    return;
  }

  // virtual void OnStatusMessage(CefRefPtr<CefBrowser> browser,
  //                              const CefString &value) override {
  //   GST_INFO("CEF status message: %s \n", value.ToString().c_str());
  // }

  virtual bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                cef_log_severity_t level,
                                const CefString &message,
                                const CefString &source, int line) override {
    GstDebugLevel gst_level = GST_LEVEL_NONE;
    std::string severity = "";

    switch (level) {
    case LOGSEVERITY_DEFAULT:
    case LOGSEVERITY_INFO:
      gst_level = GST_LEVEL_INFO;
      severity = "info";
      break;
    case LOGSEVERITY_DEBUG:
      gst_level = GST_LEVEL_DEBUG;
      severity = "debug";
      break;
    case LOGSEVERITY_WARNING:
      gst_level = GST_LEVEL_WARNING;
      severity = "warn";
      break;
    case LOGSEVERITY_ERROR:
      gst_level = GST_LEVEL_ERROR;
      severity = "error";
      break;
    case LOGSEVERITY_FATAL:
      gst_level = GST_LEVEL_ERROR;
      severity = "fatal";
      break;
    case LOGSEVERITY_DISABLE:
      gst_level = GST_LEVEL_NONE;
      break;
    };
    GST_CAT_LEVEL_LOG(cef_console_debug, gst_level, mElement, "%s:%d %s",
                      source.ToString().c_str(), line,
                      message.ToString().c_str());

    g_signal_emit_by_name(mElement, kOnConsoleMessage, severity.c_str(),
                          message.ToString().c_str(), source.ToString().c_str(),
                          line);

    if (mElement->log_severity < level) {
      return true;
    }

    // GCP Logging. Take a look at packages/logger/src/logger.ts:logTrace
    // for how it is done in SY. This is currently a very simply
    // implementation.
    std::string json = "{\"severity\":\"" + severity +
                       "\",\"message\":" + message.ToString() + "}";
    switch (level) {
    case LOGSEVERITY_DEFAULT:
    case LOGSEVERITY_INFO:
      fprintf(stdout, "%s\n", json.c_str());
      break;
    case LOGSEVERITY_DEBUG:
      fprintf(stderr, "%s\n", json.c_str());
      break;
    case LOGSEVERITY_WARNING:
      fprintf(stderr, "%s\n", json.c_str());
      break;
    case LOGSEVERITY_ERROR:
    case LOGSEVERITY_FATAL:
      fprintf(stderr, "%s\n", json.c_str());
      break;
    case LOGSEVERITY_DISABLE:
      return true;
    };

    return true;
  }

private:
  GstCefSrc *mElement;
  IMPLEMENT_REFCOUNTING(DisplayHandler);
};

class App : public CefApp, public CefBrowserProcessHandler {
public:
  App(GstCefSrc *src) : src(src) {}

  virtual void OnBeforeCommandLineProcessing(
      const CefString &process_type,
      CefRefPtr<CefCommandLine> command_line) override {
    command_line->AppendSwitchWithValue("autoplay-policy",
                                        "no-user-gesture-required");
    command_line->AppendSwitch("enable-media-stream");
    command_line->AppendSwitch(
        "disable-dev-shm-usage"); /* https://github.com/GoogleChrome/puppeteer/issues/1834
                                   */
    command_line->AppendSwitch(
        "enable-begin-frame-scheduling"); /* https://bitbucket.org/chromiumembedded/cef/issues/1368
                                           */

    if (!src->gpu) {
      // Optimize for no gpu usage
      command_line->AppendSwitch("disable-gpu");
      command_line->AppendSwitch("disable-gpu-compositing");
    }

    if (src->chromium_debug_port >= 0) {
      command_line->AppendSwitchWithValue(
          "remote-debugging-port",
          g_strdup_printf("%i", src->chromium_debug_port));
    }

    if (src->chrome_extra_flags) {
      gchar **flags_list =
          g_strsplit((const gchar *)src->chrome_extra_flags, ",", -1);
      guint i;

      for (i = 0; i < g_strv_length(flags_list); i++) {
        gchar **switch_value =
            g_strsplit((const gchar *)flags_list[i], "=", -1);

        if (g_strv_length(switch_value) > 1) {
          GST_INFO_OBJECT(src, "Adding switch with value %s=%s",
                          switch_value[0], switch_value[1]);
          command_line->AppendSwitchWithValue(switch_value[0], switch_value[1]);
        } else {
          GST_INFO_OBJECT(src, "Adding flag %s", flags_list[i]);
          command_line->AppendSwitch(flags_list[i]);
        }

        g_strfreev(switch_value);
      }

      g_strfreev(flags_list);
    }
  }

private:
  IMPLEMENT_REFCOUNTING(App);
  GstCefSrc *src;
};

static GstFlowReturn gst_cef_src_create(GstPushSrc *push_src, GstBuffer **buf) {
  GstCefSrc *src = GST_CEF_SRC(push_src);
  GList *tmp;

  GST_OBJECT_LOCK(src);

  if (src->audio_events) {
    for (tmp = src->audio_events; tmp; tmp = tmp->next) {
      gst_pad_push_event(GST_BASE_SRC_PAD(src), (GstEvent *)tmp->data);
    }

    g_list_free(src->audio_events);
    src->audio_events = NULL;
  }

  g_assert(src->current_buffer);
  *buf = gst_buffer_copy(src->current_buffer);

  if (src->audio_buffers) {
    gst_buffer_add_cef_audio_meta(*buf, src->audio_buffers);
    src->audio_buffers = NULL;
  }

  GST_BUFFER_PTS(*buf) = gst_util_uint64_scale(
      src->n_frames, src->vinfo.fps_d * GST_SECOND, src->vinfo.fps_n);
  GST_BUFFER_DURATION(*buf) =
      gst_util_uint64_scale(GST_SECOND, src->vinfo.fps_d, src->vinfo.fps_n);
  src->n_frames++;
  GST_OBJECT_UNLOCK(src);

  return GST_FLOW_OK;
}

/* Once we have started a first cefsrc for this process, we start
 * a UI thread and never shut it down. We could probably refine this
 * to stop and restart the thread as needed, but this updated approach
 * now no longer requires a main loop to be running, doesn't crash
 * when one is running either with CEF 86+, and allows for multiple
 * concurrent cefsrc instances.
 */
static gpointer run_cef(GstCefSrc *src) {
#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(0, NULL);
#endif

  CefSettings settings;
  CefRefPtr<App> app;
  CefWindowInfo window_info;

  settings.no_sandbox = !src->sandbox;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = src->log_severity;

  GST_INFO("Initializing CEF");

  gchar *base_path = get_plugin_base_path();

  // If not absolute path append to current_dir
  if (!g_path_is_absolute(base_path)) {
    gchar *current_dir = g_get_current_dir();

    gchar *old_base_path = base_path;
    base_path = g_build_filename(current_dir, base_path, NULL);

    g_free(current_dir);
    g_free(old_base_path);
  }

  gchar *browser_subprocess_path =
      g_build_filename(base_path, "gstcefsubprocess", NULL);
  if (const gchar *custom_subprocess_path =
          g_getenv("GST_CEF_SUBPROCESS_PATH")) {
    g_setenv("CEF_SUBPROCESS_PATH", browser_subprocess_path, TRUE);
    g_free(browser_subprocess_path);
    browser_subprocess_path = g_strdup(custom_subprocess_path);
  }

  CefString(&settings.browser_subprocess_path)
      .FromASCII(browser_subprocess_path);
  g_free(browser_subprocess_path);

  gchar *locales_dir_path = g_build_filename(base_path, "locales", NULL);
  CefString(&settings.locales_dir_path).FromASCII(locales_dir_path);

  if (src->js_flags != NULL) {
    CefString(&settings.javascript_flags).FromASCII(src->js_flags);
  }

  if (src->cef_cache_location != NULL) {
    CefString(&settings.cache_path).FromASCII(src->cef_cache_location);
  }

  g_free(base_path);
  g_free(locales_dir_path);

  app = new App(src);

  if (!CefInitialize(args, settings, app, nullptr)) {
    GST_ERROR("Failed to initialize CEF");

    /* unblock start () */
    g_mutex_lock(&init_lock);
    cef_inited = TRUE;
    g_cond_signal(&init_cond);
    g_mutex_unlock(&init_lock);

    goto done;
  }

  g_mutex_lock(&init_lock);
  cef_inited = TRUE;
  init_result = TRUE;
  g_cond_signal(&init_cond);
  g_mutex_unlock(&init_lock);

  CefRunMessageLoop();

  CefShutdown();

  g_mutex_lock(&init_lock);
  cef_inited = FALSE;
  g_cond_signal(&init_cond);
  g_mutex_unlock(&init_lock);

done:
  return NULL;
}

void quit_message_loop(int arg) { CefQuitMessageLoop(); }

class ShutdownEnforcer {
public:
  ~ShutdownEnforcer() {
    if (!cef_inited)
      return;

    CefPostTask(TID_UI, base::BindOnce(&quit_message_loop, 0));

    g_mutex_lock(&init_lock);
    while (cef_inited)
      g_cond_wait(&init_cond, &init_lock);
    g_mutex_unlock(&init_lock);
  }
} shutdown_enforcer;

static gpointer init_cef(gpointer src) {
  g_mutex_init(&init_lock);
  g_cond_init(&init_cond);

  g_thread_new("cef-ui-thread", (GThreadFunc)run_cef, src);

  return NULL;
}

static gboolean gst_cef_src_start(GstBaseSrc *base_src) {
  static GOnce init_once = G_ONCE_INIT;
  gboolean ret = FALSE;
  GstCefSrc *src = GST_CEF_SRC(base_src);
  CefRefPtr<BrowserClient> browserClient;
  CefRefPtr<RenderHandler> renderHandler = new RenderHandler(src);
  CefRefPtr<AudioHandler> audioHandler = new AudioHandler(src);
  CefRefPtr<RequestHandler> requestHandler = new RequestHandler(src);
  CefRefPtr<DisplayHandler> displayHandler = new DisplayHandler(src);

  /* Initialize global variables */
  g_once(&init_once, init_cef, src);

  /* Make sure CEF is initialized before posting a task */
  g_mutex_lock(&init_lock);
  while (!cef_inited)
    g_cond_wait(&init_cond, &init_lock);
  g_mutex_unlock(&init_lock);

  if (!init_result)
    goto done;

  GST_OBJECT_LOCK(src);
  src->n_frames = 0;
  GST_OBJECT_UNLOCK(src);

  browserClient = new BrowserClient(renderHandler, audioHandler, requestHandler,
                                    displayHandler, src);
  CefPostTask(TID_UI, base::BindOnce(&BrowserClient::MakeBrowser,
                                     browserClient.get(), 0));

  /* And wait for this src's browser to have been created */
  g_mutex_lock(&src->state_lock);
  while (!src->started)
    g_cond_wait(&src->state_cond, &src->state_lock);
  g_mutex_unlock(&src->state_lock);

  ret = src->browser != NULL;

done:
  return ret;
}

static gboolean gst_cef_src_stop(GstBaseSrc *base_src) {
  GstCefSrc *src = GST_CEF_SRC(base_src);

  GST_INFO_OBJECT(src, "Stopping");

  if (src->browser) {
    src->browser->GetHost()->CloseBrowser(true);

    /* And wait for this src's browser to have been closed */
    g_mutex_lock(&src->state_lock);
    while (src->started)
      g_cond_wait(&src->state_cond, &src->state_lock);
    g_mutex_unlock(&src->state_lock);
  }

  return TRUE;
}

static void gst_cef_src_get_times(GstBaseSrc *base_src, GstBuffer *buffer,
                                  GstClockTime *start, GstClockTime *end) {
  GstClockTime timestamp = GST_BUFFER_PTS(buffer);
  GstClockTime duration = GST_BUFFER_DURATION(buffer);

  *end = timestamp + duration;
  *start = timestamp;

  GST_LOG_OBJECT(base_src,
                 "Got times start: %" GST_TIME_FORMAT " end: %" GST_TIME_FORMAT,
                 GST_TIME_ARGS(*start), GST_TIME_ARGS(*end));
}

static gboolean gst_cef_src_query(GstBaseSrc *base_src, GstQuery *query) {
  gboolean res = FALSE;
  GstCefSrc *src = GST_CEF_SRC(base_src);

  switch (GST_QUERY_TYPE(query)) {
  case GST_QUERY_LATENCY: {
    GstClockTime latency;

    if (src->vinfo.fps_n) {
      latency =
          gst_util_uint64_scale(GST_SECOND, src->vinfo.fps_d, src->vinfo.fps_n);
      GST_DEBUG_OBJECT(src, "Reporting latency: %" GST_TIME_FORMAT,
                       GST_TIME_ARGS(latency));
      gst_query_set_latency(query, TRUE, latency, GST_CLOCK_TIME_NONE);
    }
    res = TRUE;
    break;
  }
  default:
    res = GST_BASE_SRC_CLASS(parent_class)->query(base_src, query);
    break;
  }

  return res;
}

static GstCaps *gst_cef_src_fixate(GstBaseSrc *base_src, GstCaps *caps) {
  GstStructure *structure;

  caps = gst_caps_make_writable(caps);
  structure = gst_caps_get_structure(caps, 0);

  gst_structure_fixate_field_nearest_int(structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int(structure, "height", DEFAULT_HEIGHT);

  if (gst_structure_has_field(structure, "framerate"))
    gst_structure_fixate_field_nearest_fraction(structure, "framerate",
                                                DEFAULT_FPS_N, DEFAULT_FPS_D);
  else
    gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N,
                      DEFAULT_FPS_D, NULL);

  caps = GST_BASE_SRC_CLASS(parent_class)->fixate(base_src, caps);

  GST_INFO_OBJECT(base_src, "Fixated caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean gst_cef_src_set_caps(GstBaseSrc *base_src, GstCaps *caps) {
  GstCefSrc *src = GST_CEF_SRC(base_src);
  gboolean ret = TRUE;
  GstBuffer *new_buffer;

  GST_INFO_OBJECT(base_src, "Caps set to %" GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK(src);
  gst_video_info_from_caps(&src->vinfo, caps);
  new_buffer = gst_buffer_new_allocate(
      NULL, src->vinfo.width * src->vinfo.height * 4, NULL);
  gst_buffer_replace(&(src->current_buffer), new_buffer);
  gst_buffer_unref(new_buffer);
  src->browser->GetHost()->SetWindowlessFrameRate(
      gst_util_uint64_scale(1, src->vinfo.fps_n, src->vinfo.fps_d));
  src->browser->GetHost()->WasResized();
  GST_OBJECT_UNLOCK(src);

  return ret;
}

static void gst_cef_src_set_property(GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec) {
  GstCefSrc *src = GST_CEF_SRC(object);

  switch (prop_id) {
  case PROP_URL: {
    const gchar *url;

    url = g_value_get_string(value);
    g_free(src->url);
    src->url = g_strdup(url);

    g_mutex_lock(&src->state_lock);
    if (src->started) {
      src->browser->GetMainFrame()->LoadURL(src->url);
    }
    g_mutex_unlock(&src->state_lock);

    break;
  }
  case PROP_CHROME_EXTRA_FLAGS: {
    g_free(src->chrome_extra_flags);
    src->chrome_extra_flags = g_value_dup_string(value);
    break;
  }
  case PROP_GPU: {
    src->gpu = g_value_get_boolean(value);
    break;
  }
  case PROP_CHROMIUM_DEBUG_PORT: {
    src->chromium_debug_port = g_value_get_int(value);
    break;
  }
  case PROP_SANDBOX: {
    src->sandbox = g_value_get_boolean(value);
    break;
  }
  case PROP_JS_FLAGS: {
    g_free(src->js_flags);
    src->js_flags = g_value_dup_string(value);
    break;
  }
  case PROP_LOG_SEVERITY: {
    src->log_severity = (cef_log_severity_t)g_value_get_enum(value);
    break;
  }
  case PROP_CEF_CACHE_LOCATION: {
    g_free(src->cef_cache_location);
    src->cef_cache_location = g_value_dup_string(value);
    break;
  }
  case PROP_AUDIO_LAYOUT: {
    src->channel_layout = g_value_get_uint(value);
    break;
  }
  case PROP_AUDIO_RATE: {
    src->audio_rate = g_value_get_uint(value);
    break;
  }
  case PROP_AUDIO_FRAMES_PER_BUFFER: {
    src->frames_per_buffer = g_value_get_uint(value);
    break;
  }
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_cef_src_get_property(GObject *object, guint prop_id,
                                     GValue *value, GParamSpec *pspec) {
  GstCefSrc *src = GST_CEF_SRC(object);

  switch (prop_id) {
  case PROP_URL:
    g_value_set_string(value, src->url);
    break;
  case PROP_CHROME_EXTRA_FLAGS:
    g_value_set_string(value, src->chrome_extra_flags);
    break;
  case PROP_GPU:
    g_value_set_boolean(value, src->gpu);
    break;
  case PROP_CHROMIUM_DEBUG_PORT:
    g_value_set_int(value, src->chromium_debug_port);
    break;
  case PROP_SANDBOX:
    g_value_set_boolean(value, src->sandbox);
    break;
  case PROP_JS_FLAGS:
    g_value_set_string(value, src->js_flags);
    break;
  case PROP_LOG_SEVERITY:
    g_value_set_enum(value, src->log_severity);
    break;
  case PROP_CEF_CACHE_LOCATION:
    g_value_set_string(value, src->cef_cache_location);
    break;
  case PROP_AUDIO_LAYOUT:
    g_value_set_uint(value, src->channel_layout);
    break;
  case PROP_AUDIO_RATE:
    g_value_set_uint(value, src->audio_rate);
    break;
  case PROP_AUDIO_FRAMES_PER_BUFFER:
    g_value_set_uint(value, src->frames_per_buffer);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_cef_src_finalize(GObject *object) {
  GstCefSrc *src = GST_CEF_SRC(object);

  if (src->audio_buffers) {
    gst_buffer_list_unref(src->audio_buffers);
    src->audio_buffers = NULL;
  }

  g_list_free_full(src->audio_events, (GDestroyNotify)gst_event_unref);
  src->audio_events = NULL;

  g_free(src->js_flags);
  g_free(src->cef_cache_location);

  g_cond_clear(&src->state_cond);
  g_mutex_clear(&src->state_lock);
}

static void gst_cef_src_init(GstCefSrc *src) {
  GstBaseSrc *base_src = GST_BASE_SRC(src);

  src->n_frames = 0;
  src->current_buffer = NULL;
  src->audio_buffers = NULL;
  src->audio_events = NULL;
  src->started = FALSE;
  src->chromium_debug_port = DEFAULT_CHROMIUM_DEBUG_PORT;
  src->sandbox = DEFAULT_SANDBOX;
  src->js_flags = NULL;
  src->log_severity = DEFAULT_LOG_SEVERITY;
  src->cef_cache_location = NULL;
  src->is_playing = FALSE;
  src->channel_layout = DEFAULT_AUDIO_CHANNELS;
  src->audio_rate = DEFAULT_AUDIO_RATE;
  src->frames_per_buffer = DEFAULT_AUDIO_FRAMES_PER_BUFFER;

  gst_base_src_set_format(base_src, GST_FORMAT_TIME);
  gst_base_src_set_live(base_src, TRUE);

  g_cond_init(&src->state_cond);
  g_mutex_init(&src->state_lock);
}

static GstStateChangeReturn gst_cefsrc_change_state(GstElement *element,
                                                    GstStateChange transition) {
  GstStateChangeReturn result;
  GstCefSrc *cefsrc = (GstCefSrc *)element;

  GST_DEBUG_OBJECT(element, "%s", gst_state_change_get_name(transition));
  result = GST_CALL_PARENT_WITH_DEFAULT(GST_ELEMENT_CLASS, change_state,
                                        (element, transition),
                                        GST_STATE_CHANGE_FAILURE);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    GST_INFO("CEF GST_STATE_CHANGE_PAUSED_TO_READY");
    cefsrc->is_playing = FALSE;
    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    GST_INFO("CEF GST_STATE_CHANGE_READY_TO_PAUSED");
    cefsrc->is_playing = FALSE;
    break;
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    GST_INFO("CEF GST_STATE_CHANGE_PAUSED_TO_PLAYING");
    cefsrc->is_playing = TRUE;
    break;
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    GST_INFO("CEF GST_STATE_CHANGE_PLAYING_TO_PAUSED");
    cefsrc->is_playing = FALSE;
    break;
  case GST_STATE_CHANGE_NULL_TO_READY:
    GST_INFO("CEF GST_STATE_CHANGE_NULL_TO_READY");
    cefsrc->is_playing = FALSE;
    break;
  default:
    cefsrc->is_playing = FALSE;
    break;
  }

  return result;
}

static void gst_cef_src_action_call(GstCefSrc *src, const gchar *name,
                                    const gchar *js, gpointer user_data) {
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("joinRoom");
  message->GetArgumentList()->SetString(0, name);
  message->GetArgumentList()->SetString(1, js);
  src->browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

static void gst_cef_src_action_expose_function(GstCefSrc *src,
                                               const gchar *name,
                                               gpointer user_data) {
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create("exposeFunction");
  message->GetArgumentList()->SetString(0, name);
  src->browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

static void gst_cef_src_action_execute_js(GstCefSrc *src, const gchar *js,
                                          gpointer user_data) {
  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("executeJS");
  message->GetArgumentList()->SetString(0, js);
  src->browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

static void gst_cef_src_class_init(GstCefSrcClass *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  GstPushSrcClass *push_src_class = GST_PUSH_SRC_CLASS(klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS(klass);
  gobject_class->set_property = gst_cef_src_set_property;
  gobject_class->get_property = gst_cef_src_get_property;
  gobject_class->finalize = gst_cef_src_finalize;

  g_object_class_install_property(
      gobject_class, PROP_AUDIO_RATE,
      g_param_spec_uint("rate", "rate", "CEF audio sample rate ", 8000, 96000,
                        DEFAULT_AUDIO_RATE,
                        (GParamFlags)(G_PARAM_READWRITE |
                                      G_PARAM_STATIC_STRINGS |
                                      GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property(
      gobject_class, PROP_AUDIO_LAYOUT,
      g_param_spec_uint("channels", "channels", "CEF audio channel layout", 1,
                        2, DEFAULT_AUDIO_CHANNELS,
                        (GParamFlags)(G_PARAM_READWRITE |
                                      G_PARAM_STATIC_STRINGS |
                                      GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_URL,
      g_param_spec_string("url", "url", "The URL to display", DEFAULT_URL,
                          (GParamFlags)(G_PARAM_READWRITE |
                                        G_PARAM_STATIC_STRINGS |
                                        G_PARAM_CONSTRUCT)));

  g_object_class_install_property(
      gobject_class, PROP_GPU,
      g_param_spec_boolean("gpu", "gpu",
                           "Enable GPU usage in chromium (Improves "
                           "performance if you have GPU)",
                           DEFAULT_GPU,
                           (GParamFlags)(G_PARAM_READWRITE |
                                         G_PARAM_STATIC_STRINGS |
                                         GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_CHROMIUM_DEBUG_PORT,
      g_param_spec_int("chromium-debug-port", "chromium-debug-port",
                       "Set chromium debug port (-1 = disabled) "
                       "deprecated: use chrome-extra-flags instead",
                       -1, G_MAXUINT16, DEFAULT_CHROMIUM_DEBUG_PORT,
                       (GParamFlags)(G_PARAM_READWRITE |
                                     G_PARAM_STATIC_STRINGS |
                                     GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_CHROME_EXTRA_FLAGS,
      g_param_spec_string(
          "chrome-extra-flags", "chrome-extra-flags",
          "Comma delimiter flags to be passed into chrome "
          "(Example: show-fps-counter,remote-debugging-port=9222)",
          NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_SANDBOX,
      g_param_spec_boolean(
          "sandbox", "sandbox", "Toggle chromium sandboxing capabilities",
          DEFAULT_SANDBOX,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_JS_FLAGS,
      g_param_spec_string(
          "js-flags", "js-flags",
          "Space delimited JavaScript flags to be passed to Chromium "
          "(Example: --noexpose_wasm --expose-gc)",
          NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_LOG_SEVERITY,
      g_param_spec_enum(
          "log-severity", "log-severity", "CEF log severity level",
          GST_TYPE_CEF_LOG_SEVERITY_MODE, DEFAULT_LOG_SEVERITY,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property(
      gobject_class, PROP_CEF_CACHE_LOCATION,
      g_param_spec_string(
          "cef-cache-location", "cef-cache-location",
          "Cache location for CEF. Defaults to in memory cache. "
          "(Example: /tmp/cef-cache/)",
          NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                        GST_PARAM_MUTABLE_READY)));

  // @param1: The name of the function called
  cefsrc_signals[SIGNAL_ON_CALLED_FUNCTION] = g_signal_new(
      kOnCalledFunction, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  // normallly this would map to pageerror
  cefsrc_signals[SIGNAL_ON_UNCAUGHT_EXCEPTION] = g_signal_new(
      kOnUncaughtException, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  // @param1: The severity of the error
  // @param2: The error message
  // @param3: The source of the error
  // @param4: The line number of the error
  cefsrc_signals[SIGNAL_ON_CONSOLE_MESSAGE] =
      g_signal_new(kOnConsoleMessage, G_TYPE_FROM_CLASS(klass),
                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
                   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

  /* CefDisplayHandler signals */
  // @param1: The loading progress percentage [0.0 - 1.0]
  cefsrc_signals[SIGNAL_ON_LOAD_PROGRESS] =
      g_signal_new(kOnLoadProgress, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);

  /** CefLoadHandler */
  // @param1: A boolean indicating if the browser is loading
  // @param2: A boolean indicating if the browser can navigate back
  // @param3: A boolean indicating if the browser can navigate forward
  cefsrc_signals[SIGNAL_ON_LOADING_STATE_CHANGE] =
      g_signal_new(kOnLoadingStateChange, G_TYPE_FROM_CLASS(klass),
                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 3,
                   G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

  cefsrc_signals[SIGNAL_ON_LOAD_START] =
      g_signal_new(kOnLoadStart, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                   NULL, NULL, NULL, G_TYPE_NONE, 0);

  // @param1: The HTTP status code
  cefsrc_signals[SIGNAL_ON_LOAD_END] =
      g_signal_new(kOnLoadEnd, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                   NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);

  // @param1: The error code
  // @param2: The error text
  // @param3: The failed URL
  cefsrc_signals[SIGNAL_ON_LOAD_ERROR] = g_signal_new(
      kOnLoadError, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

  // @param1: Message
  cefsrc_signals[SIGNAL_ON_AUDIO_STREAM_ERROR] = g_signal_new(
      kOnAudioStreamError, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  cefsrc_signals[SIGNAL_ON_AFTER_CREATED] =
      g_signal_new(kOnAfterCreated, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  cefsrc_signals[SIGNAL_ON_BEFORE_CLOSE] =
      g_signal_new(kOnBeforeClose, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  // Currently no used
  // @param1: The name of the query
  // @param2: A reference to a char array for returning data
  cefsrc_signals[SIGNAL_UPSTAGE_QUERY] = g_signal_new(
      kQueryUpstage, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  // @param1: Audio stream error message
  cefsrc_signals[SIGNAL_ON_AUDIO_ERROR] = g_signal_new(
      kOnAudioStreamError, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  cefsrc_signals[SIGNAL_ON_RENDER_PROCESS_TERMINATED] = g_signal_new(
      kOnRenderProcessTerminated, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  /**
   * Action signals. These are primarily used to send messages to the render
   * and browser process. You can think of them as function calls. Each signal
   * has a corresponding resolve signal that is emitted when the action is
   * resolved. This is for async support.
   */
  // @param1: The name of the function to call
  // @param2: The JS blob to pass to the function
  klass->call = gst_cef_src_action_call;
  cefsrc_signals[SIGNAL_ACTION_CALL_FUNCTION] =
      g_signal_new(kActionCallFunction, G_TYPE_FROM_CLASS(klass),
                   (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                   G_STRUCT_OFFSET(GstCefSrcClass, call), NULL, NULL, NULL,
                   G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  // @param1: The name of the function to expose
  klass->expose_function = gst_cef_src_action_expose_function;
  cefsrc_signals[SIGNAL_ACTION_EXPOSE_FUNCTION] =
      g_signal_new(kActionExposeFunction, G_TYPE_FROM_CLASS(klass),
                   (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                   G_STRUCT_OFFSET(GstCefSrcClass, expose_function), NULL, NULL,
                   NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  // @param1: JS blob to execute in the browser
  klass->execute_js = gst_cef_src_action_execute_js;
  cefsrc_signals[SIGNAL_ACTION_EXECUTE_JS] =
      g_signal_new(kActionExecuteJS, G_TYPE_FROM_CLASS(klass),
                   (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
                   G_STRUCT_OFFSET(GstCefSrcClass, execute_js), NULL, NULL,
                   NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  cefsrc_signals[SIGNAL_RESOLVE_CALL_FUNCTION] = g_signal_new(
      kResolveCallFunction, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  cefsrc_signals[SIGNAL_RESOLVE_EXPOSE_FUNCTION] = g_signal_new(
      kResolveExposeFunction, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  cefsrc_signals[SIGNAL_RESOLVE_EXECUTE_JS] = g_signal_new(
      kResolveExecuteJS, G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL,
      NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  gstelement_class->change_state = gst_cefsrc_change_state;

  gst_element_class_set_static_metadata(
      gstelement_class, "Chromium Embedded Framework source", "Source/Video",
      "Creates a video stream from an embedded Chromium browser",
      "Mathieu Duponchelle <mathieu@centricular.com>, Steve McFarlin "
      "<steve.mcfarlin@hopin.to>");

  gst_element_class_add_static_pad_template(gstelement_class,
                                            &gst_cef_src_template);

  base_src_class->fixate = GST_DEBUG_FUNCPTR(gst_cef_src_fixate);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR(gst_cef_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR(gst_cef_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR(gst_cef_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR(gst_cef_src_get_times);
  base_src_class->query = GST_DEBUG_FUNCPTR(gst_cef_src_query);

  push_src_class->create = GST_DEBUG_FUNCPTR(gst_cef_src_create);

  GST_DEBUG_CATEGORY_INIT(cef_src_debug, "cefsrc", 0,
                          "Chromium Embedded Framework Source");
  GST_DEBUG_CATEGORY_INIT(cef_console_debug, "cefconsole", 0,
                          "Chromium Embedded Framework JS Console");
}
