/*
 * Copyright (C) 2004, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "HTMLCanvasElement.h"

#include "Attribute.h"
#include "CanvasContextAttributes.h"
#include "CanvasGradient.h"
#include "CanvasPattern.h"
#include "CanvasRenderingContext2D.h"
#include "CanvasStyle.h"
#include "Chrome.h"
#include "Document.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "GraphicsContext.h"
#include "HTMLNames.h"
#include "ImageBuffer.h"
#include "ImageData.h"
#include "MIMETypeRegistry.h"
#include "Page.h"
#include "RenderHTMLCanvas.h"
#include "RenderLayer.h"
#include "Settings.h"
#include <math.h>
#include <stdio.h>

#if USE(JSC)
#include <runtime/JSLock.h>
#endif

#if ENABLE(WEBGL)
#include "WebGLContextAttributes.h"
#include "WebGLRenderingContext.h"
#endif

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
#include "PlatformGraphicsContextSkia.h"
#include "CanvasLayer.h"
#include <cutils/properties.h>
#endif

///////////////////////////////////////////////////////////////////
// Debug includes
#include <SkImageEncoder.h>
#include "utils/CallStack.h"
#include <cutils/log.h>
#include <wtf/text/CString.h>
#include "wtf/CurrentTime.h"

// for debug
#define DEBUG_CANVAS 0
#include "AndroidLog.h"

#if DEBUG_CANVAS
#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "Canvas", __VA_ARGS__)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD() \
    android::ScopedTrace __sst(ATRACE_TAG_ALWAYS, __func__);

#else
#undef XLOG
#define XLOG(...)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD()
#endif

/// @}

namespace WebCore {

using namespace HTMLNames;

// These values come from the WhatWG spec.
static const int DefaultWidth = 300;
static const int DefaultHeight = 150;

// Firefox limits width/height to 32767 pixels, but slows down dramatically before it
// reaches that limit. We limit by area instead, giving us larger maximum dimensions,
// in exchange for a smaller maximum canvas size.
static const float MaxCanvasArea = 32768 * 8192; // Maximum canvas area in CSS pixels

//In Skia, we will also limit width/height to 32767.
static const float MaxSkiaDim = 32767.0F; // Maximum width/height in CSS pixels.

HTMLCanvasElement::HTMLCanvasElement(const QualifiedName& tagName, Document* document)
    : HTMLElement(tagName, document)
    , m_size(DefaultWidth, DefaultHeight)
    , m_rendererIsCanvas(false)
    , m_ignoreReset(false)
#ifdef ANDROID
    /* In Android we capture the drawing into a displayList, and then
       replay that list at various scale factors (sometimes zoomed out, other
       times zoomed in for "normal" reading, yet other times at arbitrary
       zoom values based on the user's choice). In all of these cases, we do
       not re-record the displayList, hence it is usually harmful to perform
       any pre-rounding, since we just don't know the actual drawing resolution
       at record time.
    */
    , m_pageScaleFactor(1)
#else
    , m_pageScaleFactor(document->frame() ? document->frame()->page()->chrome()->scaleFactor() : 1)
#endif
    , m_originClean(true)
    , m_hasCreatedImageBuffer(false)
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    , m_updated(true)
    , m_recordingDisplayList(0)
    , m_hasRecordingDisplayList(false)
    , m_currentContext(0)
#endif
/// @}
{
#if ENABLE(MTK_GLCANVAS)
    char pval[PROPERTY_VALUE_MAX];
    // Get canvas type
    property_get("debug.2dcanvas.recordingcanvas", pval, "0");

    // unless canvas is set as recording canvas, always use canvas as DEFAULT.
    int type = atoi(pval);
    if (type == 2) {
        m_type = RECORDING;
    } else {
        m_type = DEFAULT;
    }

    // Get glcanvas switch
    property_get("debug.2dcanvas.glcanvas_enable", pval, "1");
    m_enableRecordingCanvas = atoi(pval) ? true : false;

    // for testing usage
    property_get("debug.2dcanvas.switch.sw.off", pval, "0");
    m_disableSwitchBack2SW = atoi(pval) ? true : false;
    XLOG("HTMLCanvasElement m_type=%d m_enableRecordingCanvas=%d m_disableSwitchBack2SW=%d", m_type, m_enableRecordingCanvas, m_disableSwitchBack2SW);
#endif

    ASSERT(hasTagName(canvasTag));
}

PassRefPtr<HTMLCanvasElement> HTMLCanvasElement::create(Document* document)
{
    return adoptRef(new HTMLCanvasElement(canvasTag, document));
}

PassRefPtr<HTMLCanvasElement> HTMLCanvasElement::create(const QualifiedName& tagName, Document* document)
{
    return adoptRef(new HTMLCanvasElement(tagName, document));
}

HTMLCanvasElement::~HTMLCanvasElement()
{
    HashSet<CanvasObserver*>::iterator end = m_observers.end();
    for (HashSet<CanvasObserver*>::iterator it = m_observers.begin(); it != end; ++it)
        (*it)->canvasDestroyed(this);
/// M: WebGL @{
#if ENABLE(WEBGL) && PLATFORM(ANDROID) || ENABLE(MTK_GLCANVAS)
//#define LOGWEBGL(...) ((void)android_printLog(ANDROID_LOG_DEBUG, "WebGL", __VA_ARGS__))
//    LOGWEBGL("~HTMLCanvasElement(), this = %p", this);
    document()->unregisterForDocumentActivationCallbacks(this);
    document()->unregisterForDocumentSuspendCallbacks(this);
#endif
/// M: @}
}

void HTMLCanvasElement::parseMappedAttribute(Attribute* attr)
{
    const QualifiedName& attrName = attr->name();
    if (attrName == widthAttr || attrName == heightAttr)
        reset();
    HTMLElement::parseMappedAttribute(attr);
}

RenderObject* HTMLCanvasElement::createRenderer(RenderArena* arena, RenderStyle* style)
{
    Frame* frame = document()->frame();
    if (frame && frame->script()->canExecuteScripts(NotAboutToExecuteScript)) {
        m_rendererIsCanvas = true;
        return new (arena) RenderHTMLCanvas(this);
    }

    m_rendererIsCanvas = false;
    return HTMLElement::createRenderer(arena, style);
}

void HTMLCanvasElement::addObserver(CanvasObserver* observer)
{
    m_observers.add(observer);
}

void HTMLCanvasElement::removeObserver(CanvasObserver* observer)
{
    m_observers.remove(observer);
}

void HTMLCanvasElement::setHeight(int value)
{
    setAttribute(heightAttr, String::number(value));
}

void HTMLCanvasElement::setWidth(int value)
{
    setAttribute(widthAttr, String::number(value));
}

CanvasRenderingContext* HTMLCanvasElement::getContext(const String& type, CanvasContextAttributes* attrs)
{
    // A Canvas can either be "2D" or "webgl" but never both. If you request a 2D canvas and the existing
    // context is already 2D, just return that. If the existing context is WebGL, then destroy it
    // before creating a new 2D context. Vice versa when requesting a WebGL canvas. Requesting a
    // context with any other type string will destroy any existing context.
    
    // FIXME - The code depends on the context not going away once created, to prevent JS from
    // seeing a dangling pointer. So for now we will disallow the context from being changed
    // once it is created.
    if (type == "2d") {
        if (m_context && !m_context->is2d())
            return 0;
        if (!m_context) {
            bool usesDashbardCompatibilityMode = false;
#if ENABLE(DASHBOARD_SUPPORT)
            if (Settings* settings = document()->settings())
                usesDashbardCompatibilityMode = settings->usesDashboardBackwardCompatibilityMode();
#endif
            m_context = adoptPtr(new CanvasRenderingContext2D(this, document()->inQuirksMode(), usesDashbardCompatibilityMode));
#if USE(IOSURFACE_CANVAS_BACKING_STORE) || (ENABLE(ACCELERATED_2D_CANVAS) && USE(ACCELERATED_COMPOSITING))
            if (m_context) {
                // Need to make sure a RenderLayer and compositing layer get created for the Canvas
                setNeedsStyleRecalc(SyntheticStyleChange);
            }
#endif
            /// M: for canvas hw acceleration @{
        #if ENABLE(MTK_GLCANVAS)
            document()->registerForDocumentActivationCallbacks(this);
            document()->registerForDocumentSuspendCallbacks(this);
        #endif
            /// @}
        }
        /// M: for canvas acceleration @{
        m_context->updateDrawingContext();
        /// @}
        return m_context.get();
    }
#if ENABLE(WEBGL)    
    Settings* settings = document()->settings();
    if (settings && settings->webGLEnabled()
#if !PLATFORM(CHROMIUM) && !PLATFORM(GTK)
        && settings->acceleratedCompositingEnabled()
#endif
        ) {
        // Accept the legacy "webkit-3d" name as well as the provisional "experimental-webgl" name.
        // Once ratified, we will also accept "webgl" as the context name.
        if ((type == "webkit-3d") ||
            (type == "experimental-webgl")) {
            if (m_context && !m_context->is3d())
                return 0;
            if (!m_context) {
                m_context = WebGLRenderingContext::create(this, static_cast<WebGLContextAttributes*>(attrs));
                if (m_context) {
                    // Need to make sure a RenderLayer and compositing layer get created for the Canvas
                    setNeedsStyleRecalc(SyntheticStyleChange);
/// M: WebGL @{
#if PLATFORM(ANDROID)
                    document()->registerForDocumentActivationCallbacks(this);
                    document()->registerForDocumentSuspendCallbacks(this);
                    document()->setContainsWebGLContent(true);
#endif
/// M: @}
                }
            }
            return m_context.get();
        }
    }
#else
    UNUSED_PARAM(attrs);
#endif
    return 0;
}

void HTMLCanvasElement::didDraw(const FloatRect& rect)
{
#if ENABLE(MTK_GLCANVAS)
    m_updated = true;
#endif

    m_copiedImage.clear(); // Clear our image snapshot if we have one.

    if (RenderBox* ro = renderBox()) {
        FloatRect destRect = ro->contentBoxRect();
        FloatRect r = mapRect(rect, FloatRect(0, 0, size().width(), size().height()), destRect);
        r.intersect(destRect);
        if (r.isEmpty() || m_dirtyRect.contains(r))
            return;

        m_dirtyRect.unite(r);
#if PLATFORM(ANDROID)
        // We handle invals ourselves and don't want webkit to repaint if we
        // have put the canvas on a layer
        if (!ro->hasLayer())
#endif
        ro->repaintRectangle(enclosingIntRect(m_dirtyRect));
    }

    HashSet<CanvasObserver*>::iterator end = m_observers.end();
    for (HashSet<CanvasObserver*>::iterator it = m_observers.begin(); it != end; ++it)
        (*it)->canvasChanged(this, rect);
}

void HTMLCanvasElement::reset()
{
    if (m_ignoreReset)
        return;

    bool ok;
    bool hadImageBuffer = hasCreatedImageBuffer();
    int w = getAttribute(widthAttr).toInt(&ok);
    if (!ok || w < 0)
        w = DefaultWidth;
    int h = getAttribute(heightAttr).toInt(&ok);
    if (!ok || h < 0)
        h = DefaultHeight;

    IntSize oldSize = size();
    setSurfaceSize(IntSize(w, h)); // The image buffer gets cleared here.

#if ENABLE(WEBGL)
    if (m_context && m_context->is3d() && oldSize != size())
        static_cast<WebGLRenderingContext*>(m_context.get())->reshape(width(), height());
#endif

    if (m_context && m_context->is2d())
        static_cast<CanvasRenderingContext2D*>(m_context.get())->reset();

    if (RenderObject* renderer = this->renderer()) {
        if (m_rendererIsCanvas) {
            if (oldSize != size())
                toRenderHTMLCanvas(renderer)->canvasSizeChanged();
            if (hadImageBuffer)
                renderer->repaint();
        }
    }

    HashSet<CanvasObserver*>::iterator end = m_observers.end();
    for (HashSet<CanvasObserver*>::iterator it = m_observers.begin(); it != end; ++it)
        (*it)->canvasResized(this);
}

void HTMLCanvasElement::paint(GraphicsContext* context, const IntRect& r)
{
    // Clear the dirty rect
    m_dirtyRect = FloatRect();

    if (context->paintingDisabled())
        return;
    
    if (m_context) {
        if (!m_context->paintsIntoCanvasBuffer())
            return;
        m_context->paintRenderingResultsToCanvas();
    }

    if (hasCreatedImageBuffer()) {
        ImageBuffer* imageBuffer = buffer();
        if (imageBuffer) {
            if (m_presentedImage)
                context->drawImage(m_presentedImage.get(), ColorSpaceDeviceRGB, r);
            else if (imageBuffer->drawsUsingCopy())
                context->drawImage(copiedImage(), ColorSpaceDeviceRGB, r);
            else
                context->drawImageBuffer(imageBuffer, ColorSpaceDeviceRGB, r);
        }
    }

#if ENABLE(WEBGL)    
    if (is3D())
        static_cast<WebGLRenderingContext*>(m_context.get())->markLayerComposited();
#endif
}

#if ENABLE(WEBGL)
bool HTMLCanvasElement::is3D() const
{
    return m_context && m_context->is3d();
}

/// M: WebGL @{
#if PLATFORM(ANDROID)
void HTMLCanvasElement::documentDidBecomeActive()
{
    if (m_context && m_context->is3d()) {
        WebGLRenderingContext* context3D = static_cast<WebGLRenderingContext*>(m_context.get());
        context3D->recreateSurface();
    }
}

void HTMLCanvasElement::documentWillBecomeInactive()
{
    if (m_context && m_context->is3d()) {
        WebGLRenderingContext* context3D = static_cast<WebGLRenderingContext*>(m_context.get());
        context3D->releaseSurface();
    }
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    // Nofity the observer the canvas is inactive
    if (m_context && m_context->is2d()) {
        HashSet<CanvasObserver*>::iterator end = m_observers.end();
        for (HashSet<CanvasObserver*>::iterator it = m_observers.begin(); it != end; ++it)
            (*it)->canvasInactive(this);
    }
#endif
    /// @}
}

void HTMLCanvasElement::documentWasSuspended()
{
    if (m_context && m_context->is3d()) {
        WebGLRenderingContext* context3D = static_cast<WebGLRenderingContext*>(m_context.get());
        context3D->releaseSurface();
    }
}

void HTMLCanvasElement::documentWillResume()
{
    if (m_context && m_context->is3d()) {
        WebGLRenderingContext* context3D = static_cast<WebGLRenderingContext*>(m_context.get());
        context3D->recreateSurface();
    }
}
#endif
/// M: @}
#endif

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
class CanvasPixelReader : public DisplayListPixelReader {
public:
    CanvasPixelReader(HTMLCanvasElement* elem)
        : m_canvas(elem){}
    virtual ~CanvasPixelReader(){};
    virtual unsigned char* allocPixels() {
        return m_canvas->getCanvasPixelArray()->data()->data();
    }
    // run on CanvasTexGenerator
    virtual void postRead(void* dataPtr) {
        {
            android::Mutex::Autolock lock(m_readLock);
            m_readFinished = true;
        }
        m_readCond.signal();
    }
private:
    HTMLCanvasElement* m_canvas;
};

PassRefPtr<ByteArray> HTMLCanvasElement::getUnmultipliedImageData(const IntRect& rect)
{
    if (!m_canvasPixelAry || !m_canvasPixelAry->data())
        return 0;

    RefPtr<ByteArray> result = ByteArray::create(rect.width() * rect.height() * 4);
    unsigned char* data = result->data();

    if (rect.x() < 0 || rect.y() < 0 || rect.maxX() > width() || rect.maxY() > height())
        memset(data, 0, result->length());

    int originx = rect.x();
    int destx = 0;
    if (originx < 0) {
        destx = -originx;
        originx = 0;
    }
    int endx = rect.x() + rect.width();
    if (endx > m_size.width())
        endx = m_size.width();
    int numColumns = endx - originx;

    int originy = rect.y();
    int desty = 0;
    if (originy < 0) {
        desty = -originy;
        originy = 0;
    }
    int endy = rect.y() + rect.height();
    if (endy > m_size.height())
        endy = m_size.height();
    int numRows = endy - originy;

    SkBitmap srcBitmap;
    srcBitmap.setConfig(SkBitmap::kARGB_8888_Config, width(), height());
    srcBitmap.setPixels(m_canvasPixelAry->data()->data());

    unsigned srcPixelsPerRow = srcBitmap.rowBytesAsPixels();
    unsigned destBytesPerRow = 4 * rect.width();

    SkBitmap destBitmap;
    destBitmap.setConfig(SkBitmap::kARGB_8888_Config, rect.width(), rect.height(), destBytesPerRow);
    destBitmap.setPixels(data);

    SkCanvas canvas(srcBitmap);
    canvas.readPixels(&destBitmap, rect.x(), rect.y(), SkCanvas::kRGBA_Unpremul_Config8888);
    return result.release();
}


void HTMLCanvasElement::setIsAnimating()
{
    if (m_context->is3d() || !m_enableRecordingCanvas)
        return;
    android_printLog(ANDROID_LOG_DEBUG, "Canvas", "[HTMLCanvasElement::setIsAnimating] size=[%d,%d]", m_size.width(), m_size.height());
    m_type = ANIMATING;
}

bool HTMLCanvasElement::startRecording(bool update)
{
    if (m_context->is3d() || !m_enableRecordingCanvas)
        return false;

    android_printLog(ANDROID_LOG_DEBUG, "Canvas", "[HTMLCanvasElement::startRecording] size=[%d,%d]", m_size.width(), m_size.height());

    makeRenderingResultsAvailable();
    IntRect rect(0, 0, m_size.width(), m_size.height());
    RefPtr<ByteArray> byteArray = getUnmultipliedImageData(rect);
    RefPtr<ImageData> imageData = ImageData::create(rect.size(), byteArray.release());

    // convert to recording canvas
    m_type = RECORDING;
    m_context->updateDrawingContext();

    // clear sw buffer
    m_hasCreatedImageBuffer = false;
    m_imageBuffer.clear();
    m_copiedImage.clear();

    CanvasRenderingContext2D* ctx = static_cast<CanvasRenderingContext2D*>(m_context.get());
    ExceptionCode ec = 0;
    // update current result to 3d
    if (imageData)
        ctx->putImageData(imageData.get(), 0, 0, ec);
    else {
        XLOG("startRecording imageData empty");
    }
    return true;
}

bool HTMLCanvasElement::stopRecording(bool update)
{
    if (m_context->is3d() || !m_enableRecordingCanvas)
        return false;

    if (m_disableSwitchBack2SW)
        return false;

    android_printLog(ANDROID_LOG_DEBUG, "Canvas", "[HTMLCanvasElement::stopRecording] size=[%d,%d]", m_size.width(), m_size.height());

    RefPtr<ImageData> imageData = NULL;
    if (update) {
        makeRenderingResultsAvailable();
        IntRect rect(0, 0, m_size.width(), m_size.height());
        RefPtr<ByteArray> byteArray = getUnmultipliedImageData(rect);
        imageData = ImageData::create(rect.size(), byteArray.release());
    }

    // convert to recording canvas
    m_type = DEFAULT;
    m_context->updateDrawingContext();

    // clear HW buffer
    if (m_currentContext) {
        delete m_currentContext;
        m_currentContext = 0;
        m_recordingDisplayList = 0;
    }
    m_hasRecordingDisplayList = false;
    m_canvasPixelAry = 0;
    m_updated = true;

    if (update) {
        CanvasRenderingContext2D* ctx = static_cast<CanvasRenderingContext2D*>(m_context.get());
        ExceptionCode ec = 0;
        // update current result to 3d
        if (imageData)
            ctx->putImageData(imageData.get(), 0, 0, ec);
        else {
            XLOG("stopRecording imageData empty");
        }
    }

    // Nofity the observer the canvas is inactive to clear queue
    if (m_context && m_context->is2d()) {
        HashSet<CanvasObserver*>::iterator end = m_observers.end();
        for (HashSet<CanvasObserver*>::iterator it = m_observers.begin(); it != end; ++it)
            (*it)->canvasInactive(this);
    }

    return true;
}

void HTMLCanvasElement::disableRecordingCanvas()
{
    if (m_disableSwitchBack2SW)
        return;

    android_printLog(ANDROID_LOG_DEBUG, "Canvas", "[HTMLCanvasElement::disableRecordingCanvas] size=[%d,%d]", m_size.width(), m_size.height());
    m_enableRecordingCanvas = false;
}

#endif
/// @}

void HTMLCanvasElement::makeRenderingResultsAvailable()
{
    WTRACE_METHOD();
    if (m_context)
        m_context->paintRenderingResultsToCanvas();

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == RECORDING && m_updated && m_recordingDisplayList) {
        OpenGLDLWrapper* displayListWrapper = m_recordingDisplayList->genRecordingResult();
        if (displayListWrapper) {
            IntSize size(width(), height());
            CanvasPixelReader reader(this);
            reader.start(displayListWrapper, size);
            reader.wait();

            // once we getImageData, keep it as long as possible.
            // If there is no update between two get, we don't read data again.
            m_updated = false;
        }
    }
#endif
    /// @}
}

void HTMLCanvasElement::makePresentationCopy()
{
    if (!m_presentedImage) {
        // The buffer contains the last presented data, so save a copy of it.
        m_presentedImage = buffer()->copyImage();
    }
}

void HTMLCanvasElement::clearPresentationCopy()
{
    m_presentedImage.clear();
}

void HTMLCanvasElement::setSurfaceSize(const IntSize& size)
{
    IntSize oldSize = m_size;

    m_size = size;
    m_hasCreatedImageBuffer = false;
    m_imageBuffer.clear();
    m_copiedImage.clear();

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_currentContext) {
        XLOG("[HTMLCanvasElement::setSurfaceSize] this=[%p] curCtx=[%p] [%d %d]->[%d %d]"
                    , this, m_currentContext
                    , oldSize.width(), oldSize.height(), size.width(), size.height());
        delete m_currentContext;
        m_currentContext = 0;
        m_recordingDisplayList = 0;
    }
    m_hasRecordingDisplayList = false;
    m_canvasPixelAry = 0;
    m_updated = true;
#endif
    if (m_context)
        m_context->updateDrawingContext();
    /// @}

    android_printLog(ANDROID_LOG_DEBUG, "Canvas", "[HTMLCanvasElement::setSurfaceSize] size=[%d,%d]", m_size.width(), m_size.height());
}

String HTMLCanvasElement::toDataURL(const String& mimeType, const double* quality, ExceptionCode& ec)
{
    if (!m_originClean) {
        ec = SECURITY_ERR;
        return String();
    }

    if (m_size.isEmpty() || !buffer()) {
        return String("data:,");
    }

    String lowercaseMimeType = mimeType.lower();

    // FIXME: Make isSupportedImageMIMETypeForEncoding threadsafe (to allow this method to be used on a worker thread).
    if (mimeType.isNull() || !MIMETypeRegistry::isSupportedImageMIMETypeForEncoding(lowercaseMimeType))
        lowercaseMimeType = "image/png";

#if USE(CG) || (USE(SKIA) && !PLATFORM(ANDROID))
    // FIXME: Consider using this code path on Android. http://b/4572024
    // Try to get ImageData first, as that may avoid lossy conversions.
    RefPtr<ImageData> imageData = getImageData();

    if (imageData)
        return ImageDataToDataURL(*imageData, lowercaseMimeType, quality);
#endif

    makeRenderingResultsAvailable();
      
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == RECORDING) {
        IntRect rect(0, 0, width(), height());
        RefPtr<ByteArray> byteArray = getUnmultipliedImageData(rect);
        if (byteArray) {
            return ByteArrayToDataURL(byteArray.release(), m_size, lowercaseMimeType, quality);
        } else {
            return String::format("data:image/png;base64,");
        }
    }
#endif
/// @}
    return buffer()->toDataURL(lowercaseMimeType, quality);
}

PassRefPtr<ImageData> HTMLCanvasElement::getImageData()
{
    if (!m_context || !m_context->is3d())
       return 0;

#if ENABLE(WEBGL)    
    WebGLRenderingContext* ctx = static_cast<WebGLRenderingContext*>(m_context.get());

    return ctx->paintRenderingResultsToImageData();
#else
    return 0;
#endif
}

IntRect HTMLCanvasElement::convertLogicalToDevice(const FloatRect& logicalRect) const
{
    // Prevent under/overflow by ensuring the rect's bounds stay within integer-expressible range
    int left = clampToInteger(floorf(logicalRect.x() * m_pageScaleFactor));
    int top = clampToInteger(floorf(logicalRect.y() * m_pageScaleFactor));
    int right = clampToInteger(ceilf(logicalRect.maxX() * m_pageScaleFactor));
    int bottom = clampToInteger(ceilf(logicalRect.maxY() * m_pageScaleFactor));

    return IntRect(IntPoint(left, top), convertToValidDeviceSize(right - left, bottom - top));
}

IntSize HTMLCanvasElement::convertLogicalToDevice(const FloatSize& logicalSize) const
{
    // Prevent overflow by ensuring the rect's bounds stay within integer-expressible range
    float width = clampToInteger(ceilf(logicalSize.width() * m_pageScaleFactor));
    float height = clampToInteger(ceilf(logicalSize.height() * m_pageScaleFactor));
    return convertToValidDeviceSize(width, height);
}

IntSize HTMLCanvasElement::convertToValidDeviceSize(float width, float height) const
{
    width = ceilf(width);
    height = ceilf(height);
    
    if (width < 1 || height < 1 || width * height > MaxCanvasArea)
        return IntSize();

#if USE(SKIA)
    if (width > MaxSkiaDim || height > MaxSkiaDim)
        return IntSize();
#endif

    return IntSize(width, height);
}

const SecurityOrigin& HTMLCanvasElement::securityOrigin() const
{
    return *document()->securityOrigin();
}

CSSStyleSelector* HTMLCanvasElement::styleSelector()
{
    return document()->styleSelector();
}

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
////////////////////////////////////////////////////
void HTMLCanvasElement::createRecordingGraphicsContext() const
{
    FloatSize unscaledSize(width(), height());
    IntSize size = convertLogicalToDevice(unscaledSize);
    if (!size.width() || !size.height())
        return;

    m_recordingDisplayList = new PlatformGraphicsContextDisplayList(true);
    m_recordingDisplayList->beginRecording(size.width(), size.height());

    m_currentContext = new GraphicsContext(m_recordingDisplayList);
    m_currentContext->scale(FloatSize(size.width() / unscaledSize.width(), size.height() / unscaledSize.height()));
    m_currentContext->setShadowsIgnoreTransforms(true);

    XLOG("[HTMLCanvasElement::createRecordingPicture] this=[%p] picture=[%p] context=[%p] [%d %d] unscale=[%f %f]"
                , this, m_recordingDisplayList, m_currentContext, size.width(), size.height()
                , unscaledSize.width(), unscaledSize.height());
    m_hasRecordingDisplayList = true;

}

OpenGLDLWrapper* HTMLCanvasElement::genDisplayListWrapper()
{
    if (!m_hasRecordingDisplayList)
        return 0;
    m_recordingDisplayList->endRecording();

    OpenGLDLWrapper* displayList = m_recordingDisplayList->genRecordingResult();
    m_recordingDisplayList->reset();
    return displayList;
}


////////////////////////////////////////////////////
#endif
/// @}

void HTMLCanvasElement::createImageBuffer() const
{
    ASSERT(!m_imageBuffer);

    m_hasCreatedImageBuffer = true;

    FloatSize unscaledSize(width(), height());
    IntSize size = convertLogicalToDevice(unscaledSize);
    if (!size.width() || !size.height())
        return;

#if USE(IOSURFACE_CANVAS_BACKING_STORE)
    if (document()->settings()->canvasUsesAcceleratedDrawing())
        m_imageBuffer = ImageBuffer::create(size, ColorSpaceDeviceRGB, Accelerated);
    else
        m_imageBuffer = ImageBuffer::create(size, ColorSpaceDeviceRGB, Unaccelerated);
#else
    m_imageBuffer = ImageBuffer::create(size);
#endif
    // The convertLogicalToDevice MaxCanvasArea check should prevent common cases
    // where ImageBuffer::create() returns 0, however we could still be low on memory.
    if (!m_imageBuffer)
        return;
    m_imageBuffer->context()->scale(FloatSize(size.width() / unscaledSize.width(), size.height() / unscaledSize.height()));
    m_imageBuffer->context()->setShadowsIgnoreTransforms(true);
    m_imageBuffer->context()->setImageInterpolationQuality(DefaultInterpolationQuality);

#if USE(JSC)
    JSC::JSLock lock(JSC::SilenceAssertionsOnly);
    scriptExecutionContext()->globalData()->heap.reportExtraMemoryCost(m_imageBuffer->dataSize());
#endif

    if (m_context)
        m_context->updateDrawingContext();
}

GraphicsContext* HTMLCanvasElement::drawingContext() const
{
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == RECORDING) {
        if (!m_hasRecordingDisplayList)
            createRecordingGraphicsContext();
        return m_hasRecordingDisplayList ? m_currentContext : 0;
    } else
#endif
        return buffer() ? m_imageBuffer->context() : 0;
/// @}
}

ImageBuffer* HTMLCanvasElement::buffer() const
{
    if (!m_hasCreatedImageBuffer)
        createImageBuffer();
    return m_imageBuffer.get();
}

Image* HTMLCanvasElement::copiedImage() const
{
    if (!m_copiedImage && buffer()) {
        if (m_context)
            m_context->paintRenderingResultsToCanvas();
        m_copiedImage = buffer()->copyImage();
    }
    return m_copiedImage.get();
}

void HTMLCanvasElement::clearCopiedImage()
{
    m_copiedImage.clear();
}

AffineTransform HTMLCanvasElement::baseTransform() const
{
    ASSERT(m_hasCreatedImageBuffer);
    FloatSize unscaledSize(width(), height());
    IntSize size = convertLogicalToDevice(unscaledSize);
    AffineTransform transform;
    if (size.width() && size.height())
        transform.scaleNonUniform(size.width() / unscaledSize.width(), size.height() / unscaledSize.height());
    return m_imageBuffer->baseTransform() * transform;
}

}
