#ifndef __BROWSER_CLIENT_H__
#define __BROWSER_CLIENT_H__

#include "gstcefsrc.h"
#include "messagehandler.h"
#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/cef_render_handler.h>
#include <include/wrapper/cef_helpers.h>
#include <include/wrapper/cef_message_router.h>

class BrowserClient : public CefClient,
                      public CefLifeSpanHandler,
                      public CefLoadHandler {
public:
  BrowserClient(CefRefPtr<CefRenderHandler> rptr,
                CefRefPtr<CefAudioHandler> aptr,
                CefRefPtr<CefRequestHandler> rqptr,
                CefRefPtr<CefDisplayHandler> display_handler,
                GstCefSrc *element)
      : render_handler(rptr), audio_handler(aptr), request_handler(rqptr),
        display_handler(display_handler), mElement(element), browser_ct_(0) {}

  virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
    return this;
  }

  virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override {
    return render_handler;
  }

  virtual CefRefPtr<CefAudioHandler> GetAudioHandler() override {
    return audio_handler;
  }

  virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override {
    return request_handler;
  }

  virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
    return display_handler;
  }

  virtual bool
  OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefProcessId source_process,
                           CefRefPtr<CefProcessMessage> message) override;

  // CefLifeSpanHandler implementation
  virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler implementation
  virtual void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                    bool isLoading, bool canGoBack,
                                    bool canGoForward) override;

  virtual void OnLoadStart(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           TransitionType transition_type) override;

  virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int httpStatusCode) override;

  virtual void OnLoadError(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                           const CefString &errorText,
                           const CefString &failedUrl) override;

  void MakeBrowser(int);
  void CloseBrowser(int);

private:
  CefRefPtr<CefRenderHandler> render_handler;
  CefRefPtr<CefAudioHandler> audio_handler;
  CefRefPtr<CefRequestHandler> request_handler;
  CefRefPtr<CefDisplayHandler> display_handler;

public:
  GstCefSrc *mElement;

  CefRefPtr<CefMessageRouterBrowserSide> message_router_;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> message_handler_;

  // Track the number of browsers using this Client.
  int browser_ct_;

  IMPLEMENT_REFCOUNTING(BrowserClient);
};

#endif // __BROWSER_CLIENT_H__