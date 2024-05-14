/* gstcef
 * Copyright (C) <2018> Mathieu Duponchelle <mathieu@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "renderprocesshandler.h"
#include "util.h"
#include <cstdio>
#include <glib.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <include/cef_app.h>
#include <include/cef_client.h>
#include <iostream>

#include <include/cef_app.h>
#include <include/cef_render_handler.h>

int main(int argc, char *argv[]) {
  CefSettings settings;

#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(argc, argv);
#endif

  // Create a temporary CommandLine object.
  CefRefPtr<CefCommandLine> command_line = CreateCommandLine(args);

  for (int i = 0; i < argc; i++) {
    printf("\nargv[%d]: %s\n", i, argv[i]);
  }

  if (GetProcessType(command_line) == PROCESS_TYPE_RENDERER) {
    CefRefPtr<RenderProcessHandler> app(new RenderProcessHandler());
    return CefExecuteProcess(args, app, nullptr);
  }

  // Create a CefApp of the correct process type.
  switch (GetProcessType(command_line)) {
  case PROCESS_TYPE_BROWSER:
    // std::cout << "\nBrowser process\n";
    return CefExecuteProcess(args, nullptr, nullptr);
    break;
  case PROCESS_TYPE_RENDERER:
    // std::cout << "\nRenderer process\n";
    break;
  case PROCESS_TYPE_OTHER:
    // std::cout << "\nOther process\n";
    return CefExecuteProcess(args, nullptr, nullptr);
    break;
  }
}
