#ifndef __REQUESTHANDLER_H__
#define __REQUESTHANDLER_H__

#include "constants.h"
#include "gstcefsrc.h"
#include <iostream>
#include <sstream>
#include <stdio.h>

class RequestHandler : public CefRequestHandler {
public:
  RequestHandler(GstCefSrc *element) : element(element) {}

  ~RequestHandler() {}

  virtual void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                         TerminationStatus status) override {
    GST_WARNING_OBJECT(element, "Render subprocess terminated, reloading URL!");

    std::string str_status = "";
    switch (status) {
    case TS_ABNORMAL_TERMINATION:
      str_status = "abnormal termination";
      break;
    case TS_PROCESS_WAS_KILLED:
      str_status = "process was killed";
      break;
    case TS_PROCESS_CRASHED:
      str_status = "process crashed";
      break;
    case TS_PROCESS_OOM:
      str_status = "process ran out of memory";
      break;
    };

    g_signal_emit_by_name(element, kOnRenderProcessTerminated,
                          str_status.c_str());

    // TODO: Should we reload the URL or just stop the pipeline?
    browser->Reload();
  }

  virtual void OnRenderViewReady(CefRefPtr<CefBrowser> browser) override {}

  virtual void
  OnDocumentAvailableInMainFrame(CefRefPtr<CefBrowser> browser) override {}

private:
  GstCefSrc *element;
  IMPLEMENT_REFCOUNTING(RequestHandler);
};

#endif // __REQUESTHANDLER_H__