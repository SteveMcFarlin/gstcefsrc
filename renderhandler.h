#ifndef __RENDERHANDLER_H__
#define __RENDERHANDLER_H__

#include "gstcefsrc.h"
#include <iostream>
#include <sstream>
#include <stdio.h>

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080

class RenderHandler : public CefRenderHandler {
public:
  RenderHandler(GstCefSrc *element) : element(element) {}

  ~RenderHandler() {}

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override {
    GST_LOG_OBJECT(element, "getting view rect");
    GST_OBJECT_LOCK(element);
    rect = CefRect(
        0, 0, element->vinfo.width ? element->vinfo.width : DEFAULT_WIDTH,
        element->vinfo.height ? element->vinfo.height : DEFAULT_HEIGHT);
    GST_OBJECT_UNLOCK(element);
  }

  void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
               const RectList &dirtyRects, const void *buffer, int w,
               int h) override {
    GstBuffer *new_buffer;

    GST_LOG_OBJECT(element, "painting, width / height: %d %d", w, h);

    new_buffer = gst_buffer_new_allocate(
        NULL, element->vinfo.width * element->vinfo.height * 4, NULL);
    gst_buffer_fill(new_buffer, 0, buffer, w * h * 4);

    GST_OBJECT_LOCK(element);
    gst_buffer_replace(&(element->current_buffer), new_buffer);
    gst_buffer_unref(new_buffer);
    GST_OBJECT_UNLOCK(element);

    GST_LOG_OBJECT(element, "done painting");
  }

private:
  GstCefSrc *element;

  IMPLEMENT_REFCOUNTING(RenderHandler);
};

#endif // __RENDERHANDLER_H__