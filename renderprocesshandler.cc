#include "renderprocesshandler.h"
#include "constants.h"
#include "gstcefsrc.h"
#include "include/wrapper/cef_message_router.h"

#include <iostream>

// const std::string kOnUncaughtException = "on-uncaught-exception";

void DumpArguments(CefRefPtr<CefListValue> args) {
  int size = args->GetSize();
  std::cout << "args size: " << size << std::endl;

  for (int i = 0; i < size; i++) {
    CefValueType type = args->GetType(i);

    if (type == VTYPE_STRING) {
      std::cout << "args[" << i << "]: " << args->GetString(i).ToString()
                << std::endl;
    }
  }
}

void V8Handler::SendProcessMessage(CefRefPtr<CefProcessMessage> msg) {
  browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

bool V8Handler::Execute(const CefString &name, CefRefPtr<CefV8Value> object,
                        const CefV8ValueList &arguments,
                        CefRefPtr<CefV8Value> &retval, CefString &exception) {

  if (name == kCefVersionFunction) {
    retval = CefV8Value::CreateString(kCefVersion);
    return true;
  } else if (name == kOnCalledFunction) {
    CefRefPtr<CefProcessMessage> msg =
        CefProcessMessage::Create(kOnCalledFunction);
    CefRefPtr<CefListValue> args = msg->GetArgumentList();
    args->SetString(0, name);
    return true;
  }

  // We do not handle the function.
  return false;
}

RenderProcessHandler::RenderProcessHandler() {}

void RenderProcessHandler::OnWebKitInitialized() {
  // Create the renderer-side router for query handling.
  CefMessageRouterConfig config;
  config.js_query_function = "upstageQuery";
  config.js_cancel_function = "cancelQuery";
  message_router_ = CefMessageRouterRendererSide::Create(config);
  handler_ = new V8Handler();
}

void RenderProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefRefPtr<CefV8Context> context) {
  CefRefPtr<CefV8Value> object = context->GetGlobal();
  CefRefPtr<CefV8Value> window = context->GetGlobal();
  handler_->browser_ = browser;

  CefRefPtr<CefV8Value> func =
      CefV8Value::CreateFunction("cefsrcVersion", handler_);
  window->SetValue("cefsrcVersion", func, V8_PROPERTY_ATTRIBUTE_NONE);
}

void RenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefV8Context> context) {}

void RenderProcessHandler::OnUncaughtException(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Exception> exception,
    CefRefPtr<CefV8StackTrace> stackTrace) {
  CefRefPtr<CefProcessMessage> msg =
      CefProcessMessage::Create(kOnUncaughtException);
  CefRefPtr<CefListValue> args = msg->GetArgumentList();
  args->SetString(0, exception->GetMessage());
  args->SetString(1, exception->GetSourceLine());
  args->SetInt(2, exception->GetLineNumber());

  browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

void RenderProcessHandler::ExposeFunction(
    CefRefPtr<CefBrowser> &browser, CefRefPtr<CefFrame> &frame,
    const CefRefPtr<CefProcessMessage> &message) {
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  std::string fn_name = args->GetString(0).ToString();
  CefRefPtr<CefV8Context> context = browser->GetMainFrame()->GetV8Context();
  context->Enter();
  CefRefPtr<CefV8Value> window = context->GetGlobal();
  CefRefPtr<CefV8Value> new_fn = CefV8Value::CreateFunction(fn_name, handler_);
  window->SetValue(fn_name, new_fn, V8_PROPERTY_ATTRIBUTE_NONE);
  context->Exit();

  // TODO: If an error occurs we should return an error to the caller.
  CefRefPtr<CefProcessMessage> msg =
      CefProcessMessage::Create(kResolveExposeFunction);
  // TODO: We may need an ID here for upstream to differentiate calls.
  CefRefPtr<CefListValue> resolve_args = msg->GetArgumentList();
  resolve_args->SetString(0, fn_name);
  resolve_args->SetString(1, "ID?");
  browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

void RenderProcessHandler::ExecuteJavaScript(
    CefRefPtr<CefBrowser> &browser, CefRefPtr<CefFrame> &frame,
    const CefRefPtr<CefProcessMessage> &message) {
  CefRefPtr<CefV8Context> context = browser->GetMainFrame()->GetV8Context();
  context->Enter();
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  std::string code = args->GetString(0).ToString();
  frame->ExecuteJavaScript(code, frame->GetURL(), 0);
  context->Exit();

  // TODO: If an error occurs we should return an error to the caller.
  // TODO: We may need an ID here for upstream to differentiate calls.
  CefRefPtr<CefProcessMessage> msg =
      CefProcessMessage::Create(kResolveExecuteJS);
  CefRefPtr<CefListValue> resolve_args = msg->GetArgumentList();
  resolve_args->SetString(0, "ID?");
  browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

/**
 * TODO: Currently we are getting a function from the window and executing it.
 *       We could instead store these functions in a map and execute them by
 *       name. This would be done in ExposeFunction. One concern is we would
 *       need to keep track of the context for each function.
 */

bool RenderProcessHandler::CallJavaScript(
    CefRefPtr<CefBrowser> &browser, CefRefPtr<CefFrame> &frame,
    const CefRefPtr<CefProcessMessage> &message) {
  bool retval = false;
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  std::string fn_name = args->GetString(0).ToString();
  CefRefPtr<CefV8Context> context = browser->GetMainFrame()->GetV8Context();
  context->Enter();
  CefRefPtr<CefV8Value> window = context->GetGlobal();
  CefRefPtr<CefV8Value> function = window->GetValue(fn_name);

  if (function.get() && function->IsFunction()) {
    // TODO: We could populate fn_args with the arguments [1,n] from the
    // message args.
    CefV8ValueList fn_args;
    CefRefPtr<CefV8Value> fn_ret = function->ExecuteFunction(nullptr, fn_args);

    // TODO: We could return the value of fn_ret to the caller.
    if (fn_ret->IsNull()) {
      retval = true;
    }
  } else {
    // TODO: Use GST_WARNING here
  }

  context->Exit();

  // TODO: If reval is false we should return an error to the caller.
  CefRefPtr<CefProcessMessage> msg =
      CefProcessMessage::Create(kResolveCallFunction);
  CefRefPtr<CefListValue> resolve_args = msg->GetArgumentList();
  resolve_args->SetString(0, fn_name);
  // TODO: We may need an ID here for upstream to differentiate calls.
  args->SetString(1, "ID?");
  browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);

  return retval;
}

bool RenderProcessHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {

  std::string name = message->GetName().ToString();

  // TODO: If debug
  // DumpArguments(message->GetArgumentList());

  if (name == kActionCallFunction) {
    CallJavaScript(browser, frame, message);
    return true;
  } else if (name == kActionExposeFunction) {
    ExposeFunction(browser, frame, message);
    return true;
  } else if (name == kActionExecuteJS) {
    ExecuteJavaScript(browser, frame, message);
    return true;
  }

  bool handled = message_router_->OnProcessMessageReceived(
      browser, frame, source_process, message);

  return handled;
}
