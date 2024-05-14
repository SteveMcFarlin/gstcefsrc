#include "browserclient.h"
#include "constants.h"
#include "gstcefsrc.h"

// CefLoadHandler implementation
void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool isLoading, bool canGoBack,
                                         bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();

  g_signal_emit_by_name(mElement, kOnLoadingStateChange, isLoading, canGoBack,
                        canGoForward);
}

void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                TransitionType transition_type) {
  CEF_REQUIRE_UI_THREAD();
  g_signal_emit_by_name(mElement, kOnLoadStart);
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame, int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();
  g_signal_emit_by_name(mElement, kOnLoadEnd);
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                                const CefString &errorText,
                                const CefString &failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  // TODO: Should we map this to 'puppeteer error'?
  g_signal_emit_by_name(mElement, kOnLoadError, errorCode, errorText.ToString(),
                        failedUrl.ToString());
}

bool BrowserClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();

  std::string name = message->GetName().ToString();
  if (name == kResolveCallFunction) {
    g_signal_emit_by_name(mElement, kResolveCallFunction);
  } else if (name == kResolveExposeFunction) {
    g_signal_emit_by_name(mElement, kResolveExposeFunction);
  } else if (name == kOnUncaughtException) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    g_signal_emit_by_name(mElement, kOnUncaughtException, args->GetString(0),
                          args->GetString(1), args->GetInt(2));
  } else if (name == kOnCalledFunction) {
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    g_signal_emit_by_name(mElement, kOnCalledFunction, args->GetString(0));
  }

  bool handled = message_router_->OnProcessMessageReceived(
      browser, frame, source_process, message);
  return handled;
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

  if (!message_router_) {
    // Create the browser-side router for query handling.
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterBrowserSide::Create(config);

    // Register handlers with the router.
    // Currently we are not using the query handlers from
    // the Page to the Browser process via the MessageRouter.
    message_handler_.reset(new MessageHandler());
    message_router_->AddHandler(message_handler_.get(), false);
  }

  g_signal_emit_by_name(mElement, kOnAfterCreated);
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  mElement->browser = nullptr;
  g_mutex_lock(&mElement->state_lock);
  mElement->started = FALSE;
  g_cond_signal(&mElement->state_cond);
  g_mutex_unlock(&mElement->state_lock);

  g_signal_emit_by_name(mElement, kOnBeforeClose);
}

void BrowserClient::MakeBrowser(int arg) {
  CefWindowInfo window_info;
  CefRefPtr<CefBrowser> browser;
  CefBrowserSettings browser_settings;

  window_info.SetAsWindowless(0);
  browser_settings.windowless_frame_rate = 30;

  browser = CefBrowserHost::CreateBrowserSync(
      window_info, this, std::string(mElement->url), browser_settings, nullptr,
      nullptr);

  browser->GetHost()->SetAudioMuted(true);

  mElement->browser = browser;

  g_mutex_lock(&mElement->state_lock);
  mElement->started = TRUE;
  g_cond_signal(&mElement->state_cond);
  g_mutex_unlock(&mElement->state_lock);
}

void BrowserClient::CloseBrowser(int arg) {
  mElement->browser->GetHost()->CloseBrowser(true);
}