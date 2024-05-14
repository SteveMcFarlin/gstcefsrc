#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include <string>

const char kCefVersionFunction[] = "cefsrcVersion";
const char kCefVersion[] = "1.0.0";

// Should not need this one as it will be a `call` action
// const char kJoinRoomMessage[] = "joinRoom";
// Should not need this one as we can use kActionExposeFunction
// const char kExposeFunctionMessage[] = "exposeFunction";
// Dito
// const char kActionExecuteJS[] = "executeJS";

/**
 * gstcefsrc signals
 */
// Maps to console in puppeteer
#define kOnConsoleMessage "on-console-message"

// Maps to pageerror in puppeteer
#define kOnUncaughtException "on-uncaught-exception"

// TODO: Not implemented as a signal currently
//  Maps to close in puppeteer
#define kOnClose "on-close"

#define kOnLoadProgress "on-load-progress"
// static char kOnStatusMessage[] = "on-status-message";
#define kOnLoadingStateChange "on-loading-state-change"
#define kOnLoadStart "on-load-start"
#define kOnLoadEnd "on-load-end"
#define kOnLoadError "on-load-error"
#define kOnAfterCreated "on-after-created"
#define kOnBeforeClose "on-before-close"
#define kOnRenderProcessTerminated "on-render-process-terminated"

#define kQueryUpstage "query-upstage"
#define kOnAudioStreamError "on-audio-stream-error"

// This is used when the mixer calls a function on window.
#define kOnCalledFunction "on-called-function"

/**
 * gstcefsrc actions
 */
#define kActionCallFunction "call-function"
#define kActionExposeFunction "expose-function"
#define kActionExecuteJS "execute-js"
#define kResolveCallFunction "resolve-call-function"
#define kResolveExposeFunction "resolve-expose-function"
#define kResolveExecuteJS "resolve-execute-js"

#endif // __CONSTANTS_H__