#ifndef CEF_RENDER_HANDLER_H_
#define CEF_RENDER_HANDLER_H_

#include "include/wrapper/cef_message_router.h"
#include <include/cef_app.h>
#include <include/cef_render_handler.h>

struct V8Handler : public CefV8Handler {
public:
  V8Handler() {}

  void SendProcessMessage(CefRefPtr<CefProcessMessage> msg);

  virtual bool Execute(const CefString &name, CefRefPtr<CefV8Value> object,
                       const CefV8ValueList &arguments,
                       CefRefPtr<CefV8Value> &retval,
                       CefString &exception) override;

  CefRefPtr<CefBrowser> browser_;
  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(V8Handler);
};

class RenderProcessHandler : public CefApp, public CefRenderProcessHandler {
public:
  RenderProcessHandler();

  // CefApp methods:
  virtual CefRefPtr<CefRenderProcessHandler>
  GetRenderProcessHandler() override {
    return this;
  }

  virtual void OnWebKitInitialized() override;
  virtual void OnContextCreated(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) override;
  virtual void OnContextReleased(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefV8Context> context) override;
  virtual bool
  OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefProcessId source_process,
                           CefRefPtr<CefProcessMessage> message) override;
  virtual void
  OnUncaughtException(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefV8Context> context,
                      CefRefPtr<CefV8Exception> exception,
                      CefRefPtr<CefV8StackTrace> stackTrace) override;

private:
  /**
   * @brief Expose a JS function to the browser.
   *
   * @param browser
   * @param frame
   * @param handler
   *
   * @return void
   */
  void ExposeFunction(CefRefPtr<CefBrowser> &browser,
                      CefRefPtr<CefFrame> &frame,
                      const CefRefPtr<CefProcessMessage> &message);
  /**
   * @brief Execute a blob of JS code in the frame.
   *
   * @param browser
   * @param frame
   * @param code
   */
  void ExecuteJavaScript(CefRefPtr<CefBrowser> &browser,
                         CefRefPtr<CefFrame> &frame,
                         const CefRefPtr<CefProcessMessage> &message);

  bool CallJavaScript(CefRefPtr<CefBrowser> &browser,
                      CefRefPtr<CefFrame> &frame,
                      const CefRefPtr<CefProcessMessage> &message);

private:
  CefRefPtr<CefMessageRouterRendererSide> message_router_;
  CefRefPtr<V8Handler> handler_;
  // Include the default reference counting implementation.
  IMPLEMENT_REFCOUNTING(RenderProcessHandler);
};

#endif // CEF_TESTS_CEFSIMPLE_HELPER_APP_H_