#ifndef __MESSAGEHANDLER_H__
#define __MESSAGEHANDLER_H__

#include "gstcefsrc.h"
#include <include/wrapper/cef_message_router.h>
#include <iostream>
#include <sstream>
#include <stdio.h>

// Handle messages in the browser process.
class MessageHandler : public CefMessageRouterBrowserSide::Handler {
public:
  // explicit MessageHandler(const CefString &startup_url)
  //     : startup_url_(startup_url) {}
  explicit MessageHandler() {}

  // Called due to cefQuery execution in message_router.html.
  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
               int64_t query_id, const CefString &request, bool persistent,
               CefRefPtr<Callback> callback) override {
    // Only handle messages from the startup URL.
    // Currently this is not used.
    return false;
  }

private:
  // const CefString startup_url_;
  DISALLOW_COPY_AND_ASSIGN(MessageHandler);
};

#endif // __MESSAGEHANDLER_H__