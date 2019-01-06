/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2008, 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008 Dirk Schulze <krit@webkit.org>
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
#include "CanvasRenderingContext2D.h"

#include "AffineTransform.h"
#include "CSSMutableStyleDeclaration.h"
#include "CSSParser.h"
#include "CSSPropertyNames.h"
#include "CSSStyleSelector.h"
#include "CachedImage.h"
#include "CanvasGradient.h"
#include "CanvasPattern.h"
#include "CanvasStyle.h"
#include "ExceptionCode.h"
#include "FloatConversion.h"
#include "GraphicsContext.h"
#include "HTMLCanvasElement.h"
#include "HTMLImageElement.h"
#include "HTMLMediaElement.h"
#include "HTMLNames.h"
#include "HTMLVideoElement.h"
#include "ImageBuffer.h"
#include "ImageData.h"
#include "KURL.h"
#include "Page.h"
#include "RenderHTMLCanvas.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include "StrokeStyleApplier.h"
#include "TextMetrics.h"
#include "TextRun.h"

#if ENABLE(ACCELERATED_2D_CANVAS)
#include "Chrome.h"
#include "ChromeClient.h"
#include "DrawingBuffer.h"
#include "FrameView.h"
#include "GraphicsContext3D.h"
#include "SharedGraphicsContext3D.h"
#if USE(ACCELERATED_COMPOSITING)
#include "RenderLayer.h"
#endif
#endif

#include <wtf/ByteArray.h>
#include <wtf/MathExtras.h>
#include <wtf/OwnPtr.h>
#include <wtf/UnusedParam.h>

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
#include "AnimationDetector.h"
#include "BitmapImage.h"
#include "PlatformGraphicsContextDisplayList.h"
#include <cutils/properties.h>
#endif

#include <wtf/text/CString.h>
#include <cutils/log.h>
#include <utils/Trace.h>


// for debug
#define DEBUG_CANVAS 0
#include "AndroidLog.h"
#include <utils/CallStack.h>

#if DEBUG_CANVAS

#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "Canvas", __VA_ARGS__)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD() \
    android::ScopedTrace __sst(ATRACE_TAG_ALWAYS, __func__); \
    TIME_METHOD(); \
    LOG_METHOD()
#else
#undef XLOG
#define XLOG(...)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD()
#endif

/// @}

using namespace std;

namespace WebCore {

using namespace HTMLNames;

static const char* const defaultFont = "10px sans-serif";


class CanvasStrokeStyleApplier : public StrokeStyleApplier {
public:
    CanvasStrokeStyleApplier(CanvasRenderingContext2D* canvasContext)
        : m_canvasContext(canvasContext)
    {
    }

    virtual void strokeStyle(GraphicsContext* c)
    {
        c->setStrokeThickness(m_canvasContext->lineWidth());
        c->setLineCap(m_canvasContext->getLineCap());
        c->setLineJoin(m_canvasContext->getLineJoin());
        c->setMiterLimit(m_canvasContext->miterLimit());
    }

private:
    CanvasRenderingContext2D* m_canvasContext;
};

CanvasRenderingContext2D::CanvasRenderingContext2D(HTMLCanvasElement* canvas, bool usesCSSCompatibilityParseMode, bool usesDashboardCompatibilityMode)
    : CanvasRenderingContext(canvas)
    , m_stateStack(1)
    , m_usesCSSCompatibilityParseMode(usesCSSCompatibilityParseMode)
#if ENABLE(DASHBOARD_SUPPORT)
    , m_usesDashboardCompatibilityMode(usesDashboardCompatibilityMode)
#endif
#if ENABLE(ACCELERATED_2D_CANVAS)
    , m_context3D(0)
#endif
    /// M: added by Willy
    , m_currentDrawingContext(0)
   /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    , m_drawingCount(1)
    , m_animationDetector(new AnimationDetector(5))
#endif
   /// @}
{
#if !ENABLE(DASHBOARD_SUPPORT)
    ASSERT_UNUSED(usesDashboardCompatibilityMode, !usesDashboardCompatibilityMode);
#endif

    // Make sure that even if the drawingContext() has a different default
    // thickness, it is in sync with the canvas thickness.
    setLineWidth(lineWidth());

#if ENABLE(ACCELERATED_2D_CANVAS)
    Page* p = canvas->document()->page();
    if (!p)
        return;
    if (!p->settings()->accelerated2dCanvasEnabled())
        return;
    if (GraphicsContext* c = drawingContext()) {
        m_context3D = p->sharedGraphicsContext3D();
        if (m_context3D) {
            m_drawingBuffer = m_context3D->graphicsContext3D()->createDrawingBuffer(IntSize(canvas->width(), canvas->height()));
            if (!m_drawingBuffer) {
                c->setSharedGraphicsContext3D(0, 0, IntSize());
                m_context3D.clear();
            } else
                c->setSharedGraphicsContext3D(m_context3D.get(), m_drawingBuffer.get(), IntSize(canvas->width(), canvas->height()));
        }
    }
#endif


/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    // for testing usage
    char pval[PROPERTY_VALUE_MAX];

    property_get("debug.2dcanvas.switch.imgdata", pval, "1");
    m_switchForImageData = atoi(pval) ? true : false;

    XLOG("CanvasRenderingContext2D m_switchForImageData=%d", m_switchForImageData);
#endif
/// @}
}

CanvasRenderingContext2D::~CanvasRenderingContext2D()
{
}

bool CanvasRenderingContext2D::isAccelerated() const
{
#if USE(IOSURFACE_CANVAS_BACKING_STORE)
    ImageBuffer* buffer = canvas()->buffer();
    return buffer ? buffer->isAccelerated() : false;
#elif ENABLE(ACCELERATED_2D_CANVAS)
    return m_context3D;
#else
    return false;
#endif
}

bool CanvasRenderingContext2D::paintsIntoCanvasBuffer() const
{
#if ENABLE(ACCELERATED_2D_CANVAS)
    if (m_context3D)
        return m_context3D->paintsIntoCanvasBuffer();
#endif
    return true;
}


void CanvasRenderingContext2D::reset()
{
    m_stateStack.resize(1);
    m_stateStack.first() = State();
    m_path.clear();
#if ENABLE(ACCELERATED_2D_CANVAS)
    if (GraphicsContext* c = drawingContext()) {
        if (m_context3D && m_drawingBuffer) {
            if (m_drawingBuffer->reset(IntSize(canvas()->width(), canvas()->height()))) {
                c->setSharedGraphicsContext3D(m_context3D.get(), m_drawingBuffer.get(), IntSize(canvas()->width(), canvas()->height()));
#if USE(ACCELERATED_COMPOSITING)
                RenderBox* renderBox = canvas()->renderBox();
                if (renderBox && renderBox->hasLayer() && renderBox->layer()->hasAcceleratedCompositing())
                    renderBox->layer()->contentChanged(RenderLayer::CanvasChanged);
#endif
            } else {
                c->setSharedGraphicsContext3D(0, 0, IntSize());
                m_drawingBuffer.clear();
                m_context3D.clear();
            }
        }
    }
#endif
}

CanvasRenderingContext2D::State::State()
    : m_strokeStyle(CanvasStyle::createFromRGBA(Color::black))
    , m_fillStyle(CanvasStyle::createFromRGBA(Color::black))
    , m_lineWidth(1)
    , m_lineCap(ButtCap)
    , m_lineJoin(MiterJoin)
    , m_miterLimit(10)
    , m_shadowBlur(0)
    , m_shadowColor(Color::transparent)
    , m_globalAlpha(1)
    , m_globalComposite(CompositeSourceOver)
    , m_invertibleCTM(true)
    , m_textAlign(StartTextAlign)
    , m_textBaseline(AlphabeticTextBaseline)
    , m_unparsedFont(defaultFont)
    , m_realizedFont(false)
{
}

CanvasRenderingContext2D::State::State(const State& other)
    : FontSelectorClient()
{
    m_unparsedStrokeColor = other.m_unparsedStrokeColor;
    m_unparsedFillColor = other.m_unparsedFillColor;
    m_strokeStyle = other.m_strokeStyle;
    m_fillStyle = other.m_fillStyle;
    m_lineWidth = other.m_lineWidth;
    m_lineCap = other.m_lineCap;
    m_lineJoin = other.m_lineJoin;
    m_miterLimit = other.m_miterLimit;
    m_shadowOffset = other.m_shadowOffset;
    m_shadowBlur = other.m_shadowBlur;
    m_shadowColor = other.m_shadowColor;
    m_globalAlpha = other.m_globalAlpha;
    m_globalComposite = other.m_globalComposite;
    m_transform = other.m_transform;
    m_invertibleCTM = other.m_invertibleCTM;
    m_textAlign = other.m_textAlign;
    m_textBaseline = other.m_textBaseline;
    m_unparsedFont = other.m_unparsedFont;
    m_font = other.m_font;
    m_realizedFont = other.m_realizedFont;

    if (m_realizedFont)
        m_font.fontSelector()->registerForInvalidationCallbacks(this);
}

CanvasRenderingContext2D::State& CanvasRenderingContext2D::State::operator=(const State& other)
{
    if (this == &other)
        return *this;

    if (m_realizedFont)
        m_font.fontSelector()->unregisterForInvalidationCallbacks(this);

    m_unparsedStrokeColor = other.m_unparsedStrokeColor;
    m_unparsedFillColor = other.m_unparsedFillColor;
    m_strokeStyle = other.m_strokeStyle;
    m_fillStyle = other.m_fillStyle;
    m_lineWidth = other.m_lineWidth;
    m_lineCap = other.m_lineCap;
    m_lineJoin = other.m_lineJoin;
    m_miterLimit = other.m_miterLimit;
    m_shadowOffset = other.m_shadowOffset;
    m_shadowBlur = other.m_shadowBlur;
    m_shadowColor = other.m_shadowColor;
    m_globalAlpha = other.m_globalAlpha;
    m_globalComposite = other.m_globalComposite;
    m_transform = other.m_transform;
    m_invertibleCTM = other.m_invertibleCTM;
    m_textAlign = other.m_textAlign;
    m_textBaseline = other.m_textBaseline;
    m_unparsedFont = other.m_unparsedFont;
    m_font = other.m_font;
    m_realizedFont = other.m_realizedFont;

    if (m_realizedFont)
        m_font.fontSelector()->registerForInvalidationCallbacks(this);

    return *this;
}

CanvasRenderingContext2D::State::~State()
{
    if (m_realizedFont)
        m_font.fontSelector()->unregisterForInvalidationCallbacks(this);
}

void CanvasRenderingContext2D::State::fontsNeedUpdate(FontSelector* fontSelector)
{
    ASSERT_ARG(fontSelector, fontSelector == m_font.fontSelector());
    ASSERT(m_realizedFont);

    m_font.update(fontSelector);
}

void CanvasRenderingContext2D::save()
{
    WTRACE_METHOD();
    ASSERT(m_stateStack.size() >= 1);
    m_stateStack.append(state());
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->save();
}

void CanvasRenderingContext2D::restore()
{
    WTRACE_METHOD();
    ASSERT(m_stateStack.size() >= 1);
    if (m_stateStack.size() <= 1)
        return;
    m_path.transform(state().m_transform);
    m_stateStack.removeLast();
    m_path.transform(state().m_transform.inverse());
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->restore();
}

void CanvasRenderingContext2D::setAllAttributesToDefault()
{
    state().m_globalAlpha = 1;
    state().m_shadowOffset = FloatSize();
    state().m_shadowBlur = 0;
    state().m_shadowColor = Color::transparent;
    state().m_globalComposite = CompositeSourceOver;

    GraphicsContext* context = drawingContext();
    if (!context)
        return;

    context->setLegacyShadow(FloatSize(), 0, Color::transparent, ColorSpaceDeviceRGB);
    context->setAlpha(1);
    context->setCompositeOperation(CompositeSourceOver);
}

CanvasStyle* CanvasRenderingContext2D::strokeStyle() const
{
    return state().m_strokeStyle.get();
}

void CanvasRenderingContext2D::setStrokeStyle(PassRefPtr<CanvasStyle> style)
{
    WTRACE_METHOD();
    if (!style)
        return;

    if (state().m_strokeStyle && state().m_strokeStyle->isEquivalentColor(*style))
        return;

    if (style->isCurrentColor()) {
        if (style->hasOverrideAlpha())
            style = CanvasStyle::createFromRGBA(colorWithOverrideAlpha(currentColor(canvas()), style->overrideAlpha()));
        else
            style = CanvasStyle::createFromRGBA(currentColor(canvas()));
    } else
        checkOrigin(style->canvasPattern());

    state().m_strokeStyle = style;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    state().m_strokeStyle->applyStrokeColor(c);
    state().m_unparsedStrokeColor = String();
}

CanvasStyle* CanvasRenderingContext2D::fillStyle() const
{
    return state().m_fillStyle.get();
}

void CanvasRenderingContext2D::setFillStyle(PassRefPtr<CanvasStyle> style)
{
    WTRACE_METHOD();
    if (!style)
        return;

    if (state().m_fillStyle && state().m_fillStyle->isEquivalentColor(*style))
        return;

    if (style->isCurrentColor()) {
        if (style->hasOverrideAlpha())
            style = CanvasStyle::createFromRGBA(colorWithOverrideAlpha(currentColor(canvas()), style->overrideAlpha()));
        else
            style = CanvasStyle::createFromRGBA(currentColor(canvas()));
    } else
        checkOrigin(style->canvasPattern());

    state().m_fillStyle = style;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    state().m_fillStyle->applyFillColor(c);
    state().m_unparsedFillColor = String();
}

float CanvasRenderingContext2D::lineWidth() const
{
    return state().m_lineWidth;
}

void CanvasRenderingContext2D::setLineWidth(float width)
{
    WTRACE_METHOD();
    if (!(isfinite(width) && width > 0))
        return;
    state().m_lineWidth = width;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setStrokeThickness(width);
}

String CanvasRenderingContext2D::lineCap() const
{
    return lineCapName(state().m_lineCap);
}

void CanvasRenderingContext2D::setLineCap(const String& s)
{
    WTRACE_METHOD();
    LineCap cap;
    if (!parseLineCap(s, cap))
        return;
    state().m_lineCap = cap;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setLineCap(cap);
}

String CanvasRenderingContext2D::lineJoin() const
{
    return lineJoinName(state().m_lineJoin);
}

void CanvasRenderingContext2D::setLineJoin(const String& s)
{
    WTRACE_METHOD();
    LineJoin join;
    if (!parseLineJoin(s, join))
        return;
    state().m_lineJoin = join;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setLineJoin(join);
}

float CanvasRenderingContext2D::miterLimit() const
{
    return state().m_miterLimit;
}

void CanvasRenderingContext2D::setMiterLimit(float limit)
{
    WTRACE_METHOD();
    if (!(isfinite(limit) && limit > 0))
        return;
    state().m_miterLimit = limit;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setMiterLimit(limit);
}

float CanvasRenderingContext2D::shadowOffsetX() const
{
    return state().m_shadowOffset.width();
}

void CanvasRenderingContext2D::setShadowOffsetX(float x)
{
    WTRACE_METHOD();
    if (!isfinite(x))
        return;
    /// M: for shadow case @{
    if (state().m_shadowOffset.width() == x)
        return;
    /// @}
    state().m_shadowOffset.setWidth(x);
    applyShadow();
}

float CanvasRenderingContext2D::shadowOffsetY() const
{
    return state().m_shadowOffset.height();
}

void CanvasRenderingContext2D::setShadowOffsetY(float y)
{
    WTRACE_METHOD();
    if (!isfinite(y))
        return;
    /// M: for shadow case @{
    if (state().m_shadowOffset.height() == y)
        return;
    /// @}
    state().m_shadowOffset.setHeight(y);
    applyShadow();
}

float CanvasRenderingContext2D::shadowBlur() const
{
    return state().m_shadowBlur;
}

void CanvasRenderingContext2D::setShadowBlur(float blur)
{
    WTRACE_METHOD();
    if (!(isfinite(blur) && blur >= 0))
        return;
    /// M: for shadow case @{
    if (state().m_shadowBlur == blur)
        return;
    /// @}
    state().m_shadowBlur = blur;
    applyShadow();
}

String CanvasRenderingContext2D::shadowColor() const
{
    return Color(state().m_shadowColor).serialized();
}

void CanvasRenderingContext2D::setShadowColor(const String& color)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    RGBA32 rgba;
    if (!parseColorOrCurrentColor(rgba, color, canvas()))
        return;
    if (state().m_shadowColor == rgba)
        return;
    state().m_shadowColor = rgba;
    /// @}
    applyShadow();
}

float CanvasRenderingContext2D::globalAlpha() const
{
    return state().m_globalAlpha;
}

void CanvasRenderingContext2D::setGlobalAlpha(float alpha)
{
    WTRACE_METHOD();
    if (!(alpha >= 0 && alpha <= 1))
        return;
    state().m_globalAlpha = alpha;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setAlpha(alpha);
}

String CanvasRenderingContext2D::globalCompositeOperation() const
{
    return compositeOperatorName(state().m_globalComposite);
}

void CanvasRenderingContext2D::setGlobalCompositeOperation(const String& operation)
{
    WTRACE_METHOD();
    CompositeOperator op;
    if (!parseCompositeOperator(operation, op))
        return;
    /// M: W3C test case support start @{
    /* These three operation is not valid option for HTML Canvas element, do nothing for it.
     * Fix case list:
     * 2d.composite.operation.clear.html
     * 2d.composite.operation.darker.html
     * 2d.composite.operation.highlight.html
     */
    if (op == CompositeClear || op == CompositePlusDarker || op == CompositeHighlight)
        return;
    /// @}
    state().m_globalComposite = op;
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    c->setCompositeOperation(op);
#if ENABLE(ACCELERATED_2D_CANVAS) && !ENABLE(SKIA_GPU)
    if (isAccelerated() && op != CompositeSourceOver) {
        c->setSharedGraphicsContext3D(0, 0, IntSize());
        m_drawingBuffer.clear();
        m_context3D.clear();
        // Mark as needing a style recalc so our compositing layer can be removed.
        canvas()->setNeedsStyleRecalc(SyntheticStyleChange);
    }
#endif
}

void CanvasRenderingContext2D::scale(float sx, float sy)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!isfinite(sx) | !isfinite(sy))
        return;

    AffineTransform newTransform = state().m_transform;
    newTransform.scaleNonUniform(sx, sy);
    if (!newTransform.isInvertible()) {
        state().m_invertibleCTM = false;
        return;
    }

    state().m_transform = newTransform;
    c->scale(FloatSize(sx, sy));
    m_path.transform(AffineTransform().scaleNonUniform(1.0 / sx, 1.0 / sy));
}

void CanvasRenderingContext2D::rotate(float angleInRadians)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!isfinite(angleInRadians))
        return;

    AffineTransform newTransform = state().m_transform;
    newTransform.rotate(angleInRadians / piDouble * 180.0);
    if (!newTransform.isInvertible()) {
        state().m_invertibleCTM = false;
        return;
    }

    state().m_transform = newTransform;
    c->rotate(angleInRadians);
    m_path.transform(AffineTransform().rotate(-angleInRadians / piDouble * 180.0));
}

void CanvasRenderingContext2D::translate(float tx, float ty)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!isfinite(tx) | !isfinite(ty))
        return;

    AffineTransform newTransform = state().m_transform;
    newTransform.translate(tx, ty);
    if (!newTransform.isInvertible()) {
        state().m_invertibleCTM = false;
        return;
    }

    state().m_transform = newTransform;
    c->translate(tx, ty);
    m_path.transform(AffineTransform().translate(-tx, -ty));
}

void CanvasRenderingContext2D::transform(float m11, float m12, float m21, float m22, float dx, float dy)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!isfinite(m11) | !isfinite(m21) | !isfinite(dx) | !isfinite(m12) | !isfinite(m22) | !isfinite(dy))
        return;

    AffineTransform transform(m11, m12, m21, m22, dx, dy);
    AffineTransform newTransform = state().m_transform * transform;
    if (!newTransform.isInvertible()) {
        state().m_invertibleCTM = false;
        return;
    }

    state().m_transform = newTransform;
    c->concatCTM(transform);
    m_path.transform(transform.inverse());
}

void CanvasRenderingContext2D::setTransform(float m11, float m12, float m21, float m22, float dx, float dy)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;

    if (!isfinite(m11) | !isfinite(m21) | !isfinite(dx) | !isfinite(m12) | !isfinite(m22) | !isfinite(dy))
        return;

    AffineTransform ctm = state().m_transform;
    if (!ctm.isInvertible())
        return;
    c->concatCTM(c->getCTM().inverse());
    c->concatCTM(canvas()->baseTransform());
    state().m_transform = ctm.inverse() * state().m_transform;
    m_path.transform(ctm);

    state().m_invertibleCTM = true;
    transform(m11, m12, m21, m22, dx, dy);
}

void CanvasRenderingContext2D::setStrokeColor(const String& color)
{
    WTRACE_METHOD();
    if (color == state().m_unparsedStrokeColor)
        return;
    setStrokeStyle(CanvasStyle::createFromString(color, canvas()->document()));
    state().m_unparsedStrokeColor = color;
}

void CanvasRenderingContext2D::setStrokeColor(float grayLevel)
{
    WTRACE_METHOD();
    if (state().m_strokeStyle && state().m_strokeStyle->isEquivalentRGBA(grayLevel, grayLevel, grayLevel, 1.0f))
        return;
    setStrokeStyle(CanvasStyle::createFromGrayLevelWithAlpha(grayLevel, 1.0f));
}

void CanvasRenderingContext2D::setStrokeColor(const String& color, float alpha)
{
    WTRACE_METHOD();
    setStrokeStyle(CanvasStyle::createFromStringWithOverrideAlpha(color, alpha));
}

void CanvasRenderingContext2D::setStrokeColor(float grayLevel, float alpha)
{
    WTRACE_METHOD();
    if (state().m_strokeStyle && state().m_strokeStyle->isEquivalentRGBA(grayLevel, grayLevel, grayLevel, alpha))
        return;
    setStrokeStyle(CanvasStyle::createFromGrayLevelWithAlpha(grayLevel, alpha));
}

void CanvasRenderingContext2D::setStrokeColor(float r, float g, float b, float a)
{
    WTRACE_METHOD();
    if (state().m_strokeStyle && state().m_strokeStyle->isEquivalentRGBA(r, g, b, a))
        return;
    setStrokeStyle(CanvasStyle::createFromRGBAChannels(r, g, b, a));
}

void CanvasRenderingContext2D::setStrokeColor(float c, float m, float y, float k, float a)
{
    WTRACE_METHOD();
    if (state().m_strokeStyle && state().m_strokeStyle->isEquivalentCMYKA(c, m, y, k, a))
        return;
    setStrokeStyle(CanvasStyle::createFromCMYKAChannels(c, m, y, k, a));
}

void CanvasRenderingContext2D::setFillColor(const String& color)
{
    WTRACE_METHOD();
    if (color == state().m_unparsedFillColor)
        return;
    setFillStyle(CanvasStyle::createFromString(color, canvas()->document()));
    state().m_unparsedFillColor = color;
}

void CanvasRenderingContext2D::setFillColor(float grayLevel)
{
    WTRACE_METHOD();
    if (state().m_fillStyle && state().m_fillStyle->isEquivalentRGBA(grayLevel, grayLevel, grayLevel, 1.0f))
        return;
    setFillStyle(CanvasStyle::createFromGrayLevelWithAlpha(grayLevel, 1.0f));
}

void CanvasRenderingContext2D::setFillColor(const String& color, float alpha)
{
    WTRACE_METHOD();
    setFillStyle(CanvasStyle::createFromStringWithOverrideAlpha(color, alpha));
}

void CanvasRenderingContext2D::setFillColor(float grayLevel, float alpha)
{
    WTRACE_METHOD();
    if (state().m_fillStyle && state().m_fillStyle->isEquivalentRGBA(grayLevel, grayLevel, grayLevel, alpha))
        return;
    setFillStyle(CanvasStyle::createFromGrayLevelWithAlpha(grayLevel, alpha));
}

void CanvasRenderingContext2D::setFillColor(float r, float g, float b, float a)
{
    WTRACE_METHOD();
    if (state().m_fillStyle && state().m_fillStyle->isEquivalentRGBA(r, g, b, a))
        return;
    setFillStyle(CanvasStyle::createFromRGBAChannels(r, g, b, a));
}

void CanvasRenderingContext2D::setFillColor(float c, float m, float y, float k, float a)
{
    WTRACE_METHOD();
    if (state().m_fillStyle && state().m_fillStyle->isEquivalentCMYKA(c, m, y, k, a))
        return;
    setFillStyle(CanvasStyle::createFromCMYKAChannels(c, m, y, k, a));
}

void CanvasRenderingContext2D::beginPath()
{
    WTRACE_METHOD();
    m_path.clear();
}

void CanvasRenderingContext2D::closePath()
{
    WTRACE_METHOD();
    if (m_path.isEmpty())
        return;

    FloatRect boundRect = m_path.boundingRect();
    if (boundRect.width() || boundRect.height())
        m_path.closeSubpath();
}

void CanvasRenderingContext2D::moveTo(float x, float y)
{
    WTRACE_METHOD();
    if (!isfinite(x) | !isfinite(y))
        return;
    if (!state().m_invertibleCTM)
        return;
    m_path.moveTo(FloatPoint(x, y));
}

void CanvasRenderingContext2D::lineTo(float x, float y)
{
    WTRACE_METHOD();
    if (!isfinite(x) | !isfinite(y))
        return;
    if (!state().m_invertibleCTM)
        return;

    FloatPoint p1 = FloatPoint(x, y);
    if (!m_path.hasCurrentPoint())
        m_path.moveTo(p1);
    else if (p1 != m_path.currentPoint())
        m_path.addLineTo(FloatPoint(x, y));
}

void CanvasRenderingContext2D::quadraticCurveTo(float cpx, float cpy, float x, float y)
{
    WTRACE_METHOD();
    if (!isfinite(cpx) | !isfinite(cpy) | !isfinite(x) | !isfinite(y))
        return;
    if (!state().m_invertibleCTM)
        return;
    if (!m_path.hasCurrentPoint())
        m_path.moveTo(FloatPoint(cpx, cpy));

    FloatPoint p1 = FloatPoint(x, y);
    if (p1 != m_path.currentPoint())
        m_path.addQuadCurveTo(FloatPoint(cpx, cpy), p1);
}

void CanvasRenderingContext2D::bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y)
{
    WTRACE_METHOD();
    if (!isfinite(cp1x) | !isfinite(cp1y) | !isfinite(cp2x) | !isfinite(cp2y) | !isfinite(x) | !isfinite(y))
        return;
    if (!state().m_invertibleCTM)
        return;
    if (!m_path.hasCurrentPoint())
        m_path.moveTo(FloatPoint(cp1x, cp1y));

    FloatPoint p1 = FloatPoint(x, y);
    if (p1 != m_path.currentPoint())
        m_path.addBezierCurveTo(FloatPoint(cp1x, cp1y), FloatPoint(cp2x, cp2y), p1);
}

void CanvasRenderingContext2D::arcTo(float x1, float y1, float x2, float y2, float r, ExceptionCode& ec)
{
    WTRACE_METHOD();
    ec = 0;
    if (!isfinite(x1) | !isfinite(y1) | !isfinite(x2) | !isfinite(y2) | !isfinite(r))
        return;

    if (r < 0) {
        ec = INDEX_SIZE_ERR;
        return;
    }

    if (!state().m_invertibleCTM)
        return;

    FloatPoint p1 = FloatPoint(x1, y1);
    FloatPoint p2 = FloatPoint(x2, y2);

    if (!m_path.hasCurrentPoint())
        m_path.moveTo(p1);
    else if (p1 == m_path.currentPoint() || p1 == p2 || !r)
        lineTo(x1, y1);
    else
        m_path.addArcTo(p1, p2, r);
}

void CanvasRenderingContext2D::arc(float x, float y, float r, float sa, float ea, bool anticlockwise, ExceptionCode& ec)
{
    WTRACE_METHOD();
    ec = 0;
    if (!isfinite(x) | !isfinite(y) | !isfinite(r) | !isfinite(sa) | !isfinite(ea))
        return;

    if (r < 0) {
        ec = INDEX_SIZE_ERR;
        return;
    }

    /// M: fix w3c arc relative case @{
    if (!r || sa == ea) {
        lineTo(x + r * cosf(sa), y + r * sinf(sa));
        return;
    }
    /// @}

    if (!state().m_invertibleCTM)
        return;

    /// M: fix w3c arc relative case @{
    // If 'sa' and 'ea' differ by more than 2Pi, just add a circle starting/ending at 'sa'
    if (anticlockwise && sa - ea >= 2 * piFloat) {
        m_path.addArc(FloatPoint(x, y), r, sa, sa - 2 * piFloat, anticlockwise);
        return;
    }
    if (!anticlockwise && ea - sa >= 2 * piFloat) {
        m_path.addArc(FloatPoint(x, y), r, sa, sa + 2 * piFloat, anticlockwise);
        return;
    }
    /// @}

    m_path.addArc(FloatPoint(x, y), r, sa, ea, anticlockwise);
}

static bool validateRectForCanvas(float& x, float& y, float& width, float& height)
{
    if (!isfinite(x) | !isfinite(y) | !isfinite(width) | !isfinite(height))
        return false;

    if (!width && !height)
        return false;

    if (width < 0) {
        width = -width;
        x -= width;
    }

    if (height < 0) {
        height = -height;
        y -= height;
    }

    return true;
}

void CanvasRenderingContext2D::rect(float x, float y, float width, float height)
{
    WTRACE_METHOD();
    if (!state().m_invertibleCTM)
        return;

    if (!isfinite(x) || !isfinite(y) || !isfinite(width) || !isfinite(height))
        return;

    if (!width && !height) {
        m_path.moveTo(FloatPoint(x, y));
        return;
    }

    m_path.addRect(FloatRect(x, y, width, height));
}

#if ENABLE(DASHBOARD_SUPPORT)
void CanvasRenderingContext2D::clearPathForDashboardBackwardCompatibilityMode()
{
    if (m_usesDashboardCompatibilityMode)
        m_path.clear();
}
#endif

void CanvasRenderingContext2D::fill()
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!m_path.isEmpty()) {
        c->fillPath(m_path);
        didDraw(m_path.boundingRect());
    }

#if ENABLE(DASHBOARD_SUPPORT)
    clearPathForDashboardBackwardCompatibilityMode();
#endif
}

void CanvasRenderingContext2D::stroke()
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    if (!m_path.isEmpty()) {
#if PLATFORM(QT)
        // Fast approximation of the stroke's bounding rect.
        // This yields a slightly oversized rect but is very fast
        // compared to Path::strokeBoundingRect().
        FloatRect boundingRect = m_path.platformPath().controlPointRect();
        boundingRect.inflate(state().m_miterLimit + state().m_lineWidth);
#else
        CanvasStrokeStyleApplier strokeApplier(this);
        FloatRect boundingRect = m_path.strokeBoundingRect(&strokeApplier);
#endif
        c->strokePath(m_path);
        didDraw(boundingRect);
    }

#if ENABLE(DASHBOARD_SUPPORT)
    clearPathForDashboardBackwardCompatibilityMode();
#endif
}

void CanvasRenderingContext2D::clip()
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;
    c->canvasClip(m_path);
#if ENABLE(DASHBOARD_SUPPORT)
    clearPathForDashboardBackwardCompatibilityMode();
#endif
}

bool CanvasRenderingContext2D::isPointInPath(const float x, const float y)
{
    GraphicsContext* c = drawingContext();
    if (!c)
        return false;
    if (!state().m_invertibleCTM)
        return false;

    FloatPoint point(x, y);
    AffineTransform ctm = state().m_transform;
    FloatPoint transformedPoint = ctm.inverse().mapPoint(point);
    if (!isfinite(transformedPoint.x()) || !isfinite(transformedPoint.y()))
        return false;
    return m_path.contains(transformedPoint);
}

void CanvasRenderingContext2D::clearRect(float x, float y, float width, float height)
{
    WTRACE_METHOD();
    if (!validateRectForCanvas(x, y, width, height))
        return;
    GraphicsContext* context = drawingContext();
    if (!context)
        return;
    if (!state().m_invertibleCTM)
        return;
    FloatRect rect(x, y, width, height);

    save();
    setAllAttributesToDefault();
    context->clearRect(rect);

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->isRecordingCanvasEnable()) {
        FloatRect canvasRect(0, 0, canvas()->width(), canvas()->height());
        if (rect.contains(canvasRect)) {
            m_animationDetector->count();
        }
        if ((canvas()->getCanvasType() == HTMLCanvasElement::DEFAULT) && m_animationDetector->isAnimating()) {
            canvas()->setIsAnimating();
        }
    }
#endif
    didDraw(rect);
    restore();
}

/// M: W3C test case support start @{
/* For composition relative test case */
bool CanvasRenderingContext2D::rectContainsCanvas(const FloatRect& rect) const
{
    WTRACE_METHOD();
    FloatQuad quad(rect);
    FloatQuad canvasQuad(FloatRect(0, 0, canvas()->width(), canvas()->height()));
    return state().m_transform.mapQuad(quad).containsQuad(canvasQuad);
}

void CanvasRenderingContext2D::didDrawEntireCanvas()
{
    didDraw(FloatRect(FloatPoint::zero(), canvas()->size()), CanvasDidDrawApplyClip);
}

void CanvasRenderingContext2D::clearCanvas()
{
    WTRACE_METHOD();
    FloatRect canvasRect(0, 0, canvas()->width(), canvas()->height());
    GraphicsContext* c = drawingContext();
    if (!c)
        return;

    c->save();
    c->setCTM(canvas()->baseTransform());
    c->clearRect(canvasRect);
    c->restore();
}

static bool isFullCanvasCompositeMode(CompositeOperator op)
{
    // See 4.8.11.1.3 Compositing
    // CompositeSourceAtop and CompositeDestinationOut are not listed here as the platforms already
    // implement the specification's behavior.
    return op == CompositeSourceIn || op == CompositeSourceOut || op == CompositeDestinationIn || op == CompositeDestinationAtop;
}

PassOwnPtr<ImageBuffer> CanvasRenderingContext2D::createCompositingBuffer(const IntRect& bufferRect)
{
    WTRACE_METHOD();
    RenderingMode renderMode = isAccelerated() ? Accelerated : Unaccelerated;
    return ImageBuffer::create(bufferRect.size(), ColorSpaceDeviceRGB, renderMode);
}

Path CanvasRenderingContext2D::transformAreaToDevice(const Path& path) const
{
    Path transformed(path);
    transformed.transform(state().m_transform);
    transformed.transform(canvas()->baseTransform());
    return transformed;
}

Path CanvasRenderingContext2D::transformAreaToDevice(const FloatRect& rect) const
{
    Path path;
    path.addRect(rect);
    return transformAreaToDevice(path);
}

void CanvasRenderingContext2D::compositeBuffer(ImageBuffer* buffer, const IntRect& bufferRect, CompositeOperator op)
{
    WTRACE_METHOD();
    IntRect canvasRect(0, 0, canvas()->width(), canvas()->height());
    canvasRect = canvas()->baseTransform().mapRect(canvasRect);

    GraphicsContext* c = drawingContext();
    if (!c)
        return;

    c->save();
    c->setCTM(AffineTransform());
    c->setCompositeOperation(op);

    c->save();
    c->clipOut(bufferRect);
    c->clearRect(canvasRect);
    c->restore();

    c->drawImageBuffer(buffer, ColorSpaceDeviceRGB, bufferRect.location(), state().m_globalComposite);
    c->restore();
}


template<class T> IntRect CanvasRenderingContext2D::calculateCompositingBufferRect(const T& area, IntSize* croppedOffset)
{
    WTRACE_METHOD();
    IntRect canvasRect(0, 0, canvas()->width(), canvas()->height());
    canvasRect = canvas()->baseTransform().mapRect(canvasRect);
    Path path = transformAreaToDevice(area);
    IntRect bufferRect = enclosingIntRect(path.boundingRect());
    IntPoint originalLocation = bufferRect.location();
    bufferRect.intersect(canvasRect);
    if (croppedOffset)
        *croppedOffset = originalLocation - bufferRect.location();
    return bufferRect;
}

template<class T> void CanvasRenderingContext2D::fullCanvasCompositedFill(const T& area)
{
    WTRACE_METHOD();
    ASSERT(isFullCanvasCompositeMode(state().m_globalComposite));

    IntRect bufferRect = calculateCompositingBufferRect(area, 0);
    if (bufferRect.isEmpty()) {
        clearCanvas();
        return;
    }

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
        GraphicsContext* c = drawingContext();
        if (!c)
            return;
        c->fillRect(area);
    } else
#endif
    /// @}
    {
        OwnPtr<ImageBuffer> buffer = createCompositingBuffer(bufferRect);
        if (!buffer)
            return;

        Path path = transformAreaToDevice(area);
        path.translate(FloatSize(-bufferRect.x(), -bufferRect.y()));

        buffer->context()->setCompositeOperation(CompositeSourceOver);
        state().m_fillStyle->applyFillColor(buffer->context());
        buffer->context()->fillPath(path);

        compositeBuffer(buffer.get(), bufferRect, state().m_globalComposite);
    }
}
/// @}

void CanvasRenderingContext2D::fillRect(float x, float y, float width, float height)
{
    WTRACE_METHOD();
    if (!validateRectForCanvas(x, y, width, height))
        return;

    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    // from the HTML5 Canvas spec:
    // If x0 = x1 and y0 = y1, then the linear gradient must paint nothing
    // If x0 = x1 and y0 = y1 and r0 = r1, then the radial gradient must paint nothing
    Gradient* gradient = c->fillGradient();
    if (gradient && gradient->isZeroSize())
        return;

    FloatRect rect(x, y, width, height);

    /// M: W3C test case support start @{
    /* Fix case list:
     * 2d.composite.uncovered.fill.copy.html
     * 2d.composite.uncovered.fill.destination-atop.html
     * 2d.composite.uncovered.fill.destination-in.html
     * 2d.composite.uncovered.fill.source-in.html
     * 2d.composite.uncovered.fill.source-out.html
     */
    if (rectContainsCanvas(rect)) {
        c->fillRect(rect);
        didDrawEntireCanvas();
    } else if (isFullCanvasCompositeMode(state().m_globalComposite)) {
        fullCanvasCompositedFill(rect);
        didDrawEntireCanvas();
    } else if (state().m_globalComposite == CompositeCopy) {
        clearCanvas();
        c->fillRect(rect);
        didDrawEntireCanvas();
    } else {
        c->fillRect(rect);
        didDraw(rect);
    }
    /// @}
}

void CanvasRenderingContext2D::strokeRect(float x, float y, float width, float height)
{
    WTRACE_METHOD();
    if (!validateRectForCanvas(x, y, width, height))
        return;
    strokeRect(x, y, width, height, state().m_lineWidth);
}

void CanvasRenderingContext2D::strokeRect(float x, float y, float width, float height, float lineWidth)
{
    WTRACE_METHOD();
    if (!validateRectForCanvas(x, y, width, height))
        return;

    if (!(lineWidth >= 0))
        return;

    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    FloatRect rect(x, y, width, height);

    FloatRect boundingRect = rect;
    boundingRect.inflate(lineWidth / 2);

    c->strokeRect(rect, lineWidth);
    didDraw(boundingRect);
}

#if USE(CG)
static inline CGSize adjustedShadowSize(CGFloat width, CGFloat height)
{
    // Work around <rdar://problem/5539388> by ensuring that shadow offsets will get truncated
    // to the desired integer.
    static const CGFloat extraShadowOffset = narrowPrecisionToCGFloat(1.0 / 128);
    if (width > 0)
        width += extraShadowOffset;
    else if (width < 0)
        width -= extraShadowOffset;

    if (height > 0)
        height += extraShadowOffset;
    else if (height < 0)
        height -= extraShadowOffset;

    return CGSizeMake(width, height);
}
#endif

void CanvasRenderingContext2D::setShadow(float width, float height, float blur)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    setShadow(FloatSize(width, height), blur, Color::transparent);
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, const String& color)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    RGBA32 rgba = Color::transparent;
    if (!parseColorOrCurrentColor(rgba, color, canvas()))
        return;
    setShadow(FloatSize(width, height), blur, rgba);
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, float grayLevel)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    setShadow(FloatSize(width, height), blur, makeRGBA32FromFloats(grayLevel, grayLevel, grayLevel, 1.0f));
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, const String& color, float alpha)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    RGBA32 rgba;
    if (!parseColorOrCurrentColor(rgba, color, canvas()))
        return;
    setShadow(FloatSize(width, height), blur, colorWithOverrideAlpha(rgba, alpha));
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, float grayLevel, float alpha)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    setShadow(FloatSize(width, height), blur, makeRGBA32FromFloats(grayLevel, grayLevel, grayLevel, alpha));
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, float r, float g, float b, float a)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    setShadow(FloatSize(width, height), blur, makeRGBA32FromFloats(r, g, b, a));
    /// @}
}

void CanvasRenderingContext2D::setShadow(float width, float height, float blur, float c, float m, float y, float k, float a)
{
    WTRACE_METHOD();
    /// M: for shadow case @{
    setShadow(FloatSize(width, height), blur, makeRGBAFromCMYKA(c, m, y, k, a));
    /// @}
}

/// M: for shadow case @{
void CanvasRenderingContext2D::setShadow(FloatSize offset, float blur, RGBA32 color)
{
    if (state().m_shadowOffset == offset && state().m_shadowBlur == blur && state().m_shadowColor == color)
        return;
    bool wasDrawingShadows = shouldDrawShadows();
    state().m_shadowOffset = offset;
    state().m_shadowBlur = blur;
    state().m_shadowColor = color;

    if (!wasDrawingShadows && !shouldDrawShadows())
        return;

    applyShadow();
}

bool CanvasRenderingContext2D::shouldDrawShadows() const
{
    return alphaChannel(state().m_shadowColor) && (state().m_shadowBlur || !state().m_shadowOffset.isZero());
}
/// @}

void CanvasRenderingContext2D::clearShadow()
{
    WTRACE_METHOD();
    state().m_shadowOffset = FloatSize();
    state().m_shadowBlur = 0;
    state().m_shadowColor = Color::transparent;
    applyShadow();
}

void CanvasRenderingContext2D::applyShadow()
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;

    /// M: for shadow case @{
    if (shouldDrawShadows()) {
        float width = state().m_shadowOffset.width();
        float height = state().m_shadowOffset.height();

    /// M: for canvas hw acceleration @{
    // Because we don't support shadow now, transfer it back to 2d drawing when there is shadow.
    #if ENABLE(MTK_GLCANVAS)
        if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
            // the following two function should be called in sequence. Switch the order is wrong.
            canvas()->stopRecording();
            // update drawing context again.
            c = drawingContext();
            if (!c) {
                XLOG("[Error] CanvasRenderingContext2D::applyShadow : After switch to 2d drawing, there is no drawing context.");
                return;
            }
        }
        canvas()->disableRecordingCanvas();
    #endif
    /// @}
        c->setLegacyShadow(FloatSize(width, -height), state().m_shadowBlur, state().m_shadowColor, ColorSpaceDeviceRGB);
    } else {
        c->setLegacyShadow(FloatSize(), 0, Color::transparent, ColorSpaceDeviceRGB);
    }
    /// @}
}

static IntSize size(HTMLImageElement* image)
{
    if (CachedImage* cachedImage = image->cachedImage())
        return cachedImage->imageSize(1.0f); // FIXME: Not sure about this.
    return IntSize();
}

#if ENABLE(VIDEO)
static IntSize size(HTMLVideoElement* video)
{
    if (MediaPlayer* player = video->player())
        return player->naturalSize();
    return IntSize();
}
#endif

static inline FloatRect normalizeRect(const FloatRect& rect)
{
    return FloatRect(min(rect.x(), rect.maxX()),
        min(rect.y(), rect.maxY()),
        max(rect.width(), -rect.width()),
        max(rect.height(), -rect.height()));
}

/// M: W3C test case support start @{
/* For composition relative test case */
static void drawImageToContext(Image* image, GraphicsContext* context, ColorSpace styleColorSpace, const FloatRect& dest, const FloatRect& src, CompositeOperator op)
{
    WTRACE_METHOD();
    context->drawImage(image, styleColorSpace, dest, src, op);
}

static void drawImageToContext(ImageBuffer* imageBuffer, GraphicsContext* context, ColorSpace styleColorSpace, const FloatRect& dest, const FloatRect& src, CompositeOperator op)
{
    WTRACE_METHOD();
    context->drawImageBuffer(imageBuffer, styleColorSpace, dest, src, op);
}

template<class T> void  CanvasRenderingContext2D::fullCanvasCompositedDrawImage(T* image, ColorSpace styleColorSpace, const FloatRect& dest, const FloatRect& src, CompositeOperator op)
{
    WTRACE_METHOD();
    ASSERT(isFullCanvasCompositeMode(op));

    IntSize croppedOffset;
    IntRect bufferRect = calculateCompositingBufferRect(dest, &croppedOffset);
    if (bufferRect.isEmpty()) {
        clearCanvas();
        return;
    }

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
        GraphicsContext* c = drawingContext();
        if (!c)
            return;
        drawImageToContext(image, c, styleColorSpace, dest, src, op);
    } else
#endif
    /// @}
    {
        /* Create another buffer then draw image with request source and destination upon it. */
        OwnPtr<ImageBuffer> buffer = createCompositingBuffer(bufferRect);
        if (!buffer)
            return;

        GraphicsContext* c = drawingContext();
        if (!c)
            return;

        FloatRect adjustedDest = dest;
        adjustedDest.setLocation(FloatPoint(0, 0));
        AffineTransform effectiveTransform = c->getCTM();
        IntRect transformedAdjustedRect = enclosingIntRect(effectiveTransform.mapRect(adjustedDest));
        buffer->context()->translate(-transformedAdjustedRect.location().x(), -transformedAdjustedRect.location().y());
        buffer->context()->translate(croppedOffset.width(), croppedOffset.height());
        buffer->context()->concatCTM(effectiveTransform);
        drawImageToContext(image, buffer->context(), styleColorSpace, adjustedDest, src, CompositeSourceOver);

        /* Composite the requested image result and canvas bitmap */
        compositeBuffer(buffer.get(), bufferRect, op);
    }
}
/// @}

void CanvasRenderingContext2D::drawImage(HTMLImageElement* image, float x, float y, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!image) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    IntSize s = size(image);
    drawImage(image, x, y, s.width(), s.height(), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLImageElement* image,
    float x, float y, float width, float height, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!image) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    IntSize s = size(image);
    drawImage(image, FloatRect(0, 0, s.width(), s.height()), FloatRect(x, y, width, height), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLImageElement* image,
    float sx, float sy, float sw, float sh,
    float dx, float dy, float dw, float dh, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!image) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    drawImage(image, FloatRect(sx, sy, sw, sh), FloatRect(dx, dy, dw, dh), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLImageElement* image, const FloatRect& srcRect, const FloatRect& dstRect, ExceptionCode& ec)
{
    WTRACE_METHOD();
    drawImage(image, srcRect, dstRect, state().m_globalComposite, ec);
}

void CanvasRenderingContext2D::drawImage(HTMLImageElement* image, const FloatRect& srcRect, const FloatRect& dstRect, const CompositeOperator& op, ExceptionCode& ec)
{
    WTRACE_METHOD();

    if (!image) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }

    ec = 0;

    if (!isfinite(dstRect.x()) || !isfinite(dstRect.y()) || !isfinite(dstRect.width()) || !isfinite(dstRect.height())
        || !isfinite(srcRect.x()) || !isfinite(srcRect.y()) || !isfinite(srcRect.width()) || !isfinite(srcRect.height()))
        return;

    if (!dstRect.width() || !dstRect.height())
        return;

    if (!image->complete())
        return;

    FloatRect normalizedSrcRect = normalizeRect(srcRect);
    FloatRect normalizedDstRect = normalizeRect(dstRect);

    FloatRect imageRect = FloatRect(FloatPoint(), size(image));
    if (!imageRect.contains(normalizedSrcRect) || !srcRect.width() || !srcRect.height()) {
        ec = INDEX_SIZE_ERR;
        return;
    }

    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    CachedImage* cachedImage = image->cachedImage();
    if (!cachedImage)
        return;

    checkOrigin(image);

    /// M: W3C test case support start @{
    /* Fix case list:
     * 2d.composite.uncovered.image.copy.html
     * 2d.composite.uncovered.image.destination-atop.html
     * 2d.composite.uncovered.image.destination-in.html
     * 2d.composite.uncovered.image.source-in.html
     * 2d.composite.uncovered.image.source-out.html
     */
    if (rectContainsCanvas(normalizedDstRect)) {
        c->drawImage(cachedImage->imageForRenderer(image->renderer()), ColorSpaceDeviceRGB, normalizedDstRect, normalizedSrcRect, op);
        didDrawEntireCanvas();
    } else if (isFullCanvasCompositeMode(op)) {
        fullCanvasCompositedDrawImage(cachedImage->imageForRenderer(image->renderer()), ColorSpaceDeviceRGB, normalizedDstRect, normalizedSrcRect, op);
        didDrawEntireCanvas();
    } else if (op == CompositeCopy) {
        clearCanvas();
        c->drawImage(cachedImage->imageForRenderer(image->renderer()), ColorSpaceDeviceRGB, normalizedDstRect, normalizedSrcRect, op);
        didDrawEntireCanvas();
    } else {
        c->drawImage(cachedImage->imageForRenderer(image->renderer()), ColorSpaceDeviceRGB, normalizedDstRect, normalizedSrcRect, op);
        didDraw(normalizedDstRect);
    }
    /// @}
}

void CanvasRenderingContext2D::drawImage(HTMLCanvasElement* canvas, float x, float y, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!canvas) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    drawImage(canvas, x, y, canvas->width(), canvas->height(), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLCanvasElement* canvas,
    float x, float y, float width, float height, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!canvas) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    drawImage(canvas, FloatRect(0, 0, canvas->width(), canvas->height()), FloatRect(x, y, width, height), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLCanvasElement* canvas,
    float sx, float sy, float sw, float sh,
    float dx, float dy, float dw, float dh, ExceptionCode& ec)
{
    drawImage(canvas, FloatRect(sx, sy, sw, sh), FloatRect(dx, dy, dw, dh), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLCanvasElement* sourceCanvas, const FloatRect& srcRect,
    const FloatRect& dstRect, ExceptionCode& ec)
{
    WTRACE_METHOD();

    if (!sourceCanvas) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }

    FloatRect srcCanvasRect = FloatRect(FloatPoint(), sourceCanvas->size());

    if (!srcCanvasRect.width() || !srcCanvasRect.height()) {
        ec = INVALID_STATE_ERR;
        return;
    }

    if (!srcCanvasRect.contains(normalizeRect(srcRect)) || !srcRect.width() || !srcRect.height()) {
        ec = INDEX_SIZE_ERR;
        return;
    }

    ec = 0;

    if (!dstRect.width() || !dstRect.height())
        return;

    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    FloatRect sourceRect = c->roundToDevicePixels(srcRect);
    FloatRect destRect = c->roundToDevicePixels(dstRect);

    // FIXME: Do this through platform-independent GraphicsContext API.
    ImageBuffer* buffer = sourceCanvas->buffer();
    if (!buffer)
        return;

    checkOrigin(sourceCanvas);
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
        if (state().m_globalComposite == CompositeCopy) {
            clearCanvas();
        }
        if (sourceCanvas->hasRecordingDisplayList()) {
            PlatformGraphicsContext* platformCtx = c->platformContext();
            if (platformCtx->getPlatformContextType() == PlatformGraphicsContext::DisplayListContext) {
                PlatformGraphicsContextDisplayList* platformDisplayList = static_cast<PlatformGraphicsContextDisplayList*>(platformCtx);
                platformDisplayList->drawDisplayList(sourceCanvas->genDisplayListWrapper(), sourceRect, destRect);
            }
        } else {
            // since source canvas is empty, clear dest canvas data
            clearCanvas();
        }
        didDraw(destRect);

    } else
#endif
    /// @}
    {
    #if ENABLE(ACCELERATED_2D_CANVAS)
        // If we're drawing from one accelerated canvas 2d to another, avoid calling sourceCanvas->makeRenderingResultsAvailable()
        // as that will do a readback to software.
        CanvasRenderingContext* sourceContext = sourceCanvas->renderingContext();
        // FIXME: Implement an accelerated path for drawing from a WebGL canvas to a 2d canvas when possible.
        if (!isAccelerated() || !sourceContext || !sourceContext->isAccelerated() || !sourceContext->is2d())
            sourceCanvas->makeRenderingResultsAvailable();
    #else
        sourceCanvas->makeRenderingResultsAvailable();
    #endif

        /// M: W3C test case support start @{
        if (rectContainsCanvas(dstRect)) {
            c->drawImageBuffer(buffer, ColorSpaceDeviceRGB, dstRect, srcRect, state().m_globalComposite);
            didDrawEntireCanvas();
        } else if (isFullCanvasCompositeMode(state().m_globalComposite)) {
            fullCanvasCompositedDrawImage(buffer, ColorSpaceDeviceRGB, dstRect, srcRect, state().m_globalComposite);
            didDrawEntireCanvas();
        } else if (state().m_globalComposite == CompositeCopy) {
            clearCanvas();
            c->drawImageBuffer(buffer, ColorSpaceDeviceRGB, dstRect, srcRect, state().m_globalComposite);
            didDrawEntireCanvas();
        } else {
            c->drawImageBuffer(buffer, ColorSpaceDeviceRGB, dstRect, srcRect, state().m_globalComposite);
            didDraw(dstRect);
        }
        /// @}
    }
}

#if ENABLE(VIDEO)
void CanvasRenderingContext2D::drawImage(HTMLVideoElement* video, float x, float y, ExceptionCode& ec)
{
    if (!video) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    IntSize s = size(video);
    drawImage(video, x, y, s.width(), s.height(), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLVideoElement* video,
                                         float x, float y, float width, float height, ExceptionCode& ec)
{
    if (!video) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    IntSize s = size(video);
    drawImage(video, FloatRect(0, 0, s.width(), s.height()), FloatRect(x, y, width, height), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLVideoElement* video,
    float sx, float sy, float sw, float sh,
    float dx, float dy, float dw, float dh, ExceptionCode& ec)
{
    drawImage(video, FloatRect(sx, sy, sw, sh), FloatRect(dx, dy, dw, dh), ec);
}

void CanvasRenderingContext2D::drawImage(HTMLVideoElement* video, const FloatRect& srcRect, const FloatRect& dstRect,
                                         ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!video) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }

    ec = 0;

    if (video->readyState() == HTMLMediaElement::HAVE_NOTHING || video->readyState() == HTMLMediaElement::HAVE_METADATA)
        return;

    FloatRect videoRect = FloatRect(FloatPoint(), size(video));
    if (!videoRect.contains(normalizeRect(srcRect)) || !srcRect.width() || !srcRect.height()) {
        ec = INDEX_SIZE_ERR;
        return;
    }

    if (!dstRect.width() || !dstRect.height())
        return;

    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    checkOrigin(video);

    FloatRect sourceRect = c->roundToDevicePixels(srcRect);
    FloatRect destRect = c->roundToDevicePixels(dstRect);

    c->save();
    c->clip(destRect);
    c->translate(destRect.x(), destRect.y());
    c->scale(FloatSize(destRect.width() / sourceRect.width(), destRect.height() / sourceRect.height()));
    c->translate(-sourceRect.x(), -sourceRect.y());
    video->paintCurrentFrameInContext(c, IntRect(IntPoint(), size(video)));
    c->restore();
    didDraw(destRect);
}
#endif

void CanvasRenderingContext2D::drawImageFromRect(HTMLImageElement* image,
    float sx, float sy, float sw, float sh,
    float dx, float dy, float dw, float dh,
    const String& compositeOperation)
{
    WTRACE_METHOD();
    CompositeOperator op;
    if (!parseCompositeOperator(compositeOperation, op))
        op = CompositeSourceOver;

    ExceptionCode ec;
    drawImage(image, FloatRect(sx, sy, sw, sh), FloatRect(dx, dy, dw, dh), op, ec);
}

void CanvasRenderingContext2D::setAlpha(float alpha)
{
    WTRACE_METHOD();
    setGlobalAlpha(alpha);
}

void CanvasRenderingContext2D::setCompositeOperation(const String& operation)
{
    setGlobalCompositeOperation(operation);
}

void CanvasRenderingContext2D::prepareGradientForDashboard(CanvasGradient* gradient) const
{
#if ENABLE(DASHBOARD_SUPPORT)
    if (m_usesDashboardCompatibilityMode)
        gradient->setDashboardCompatibilityMode();
#else
    UNUSED_PARAM(gradient);
#endif
}

PassRefPtr<CanvasGradient> CanvasRenderingContext2D::createLinearGradient(float x0, float y0, float x1, float y1, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) {
        ec = NOT_SUPPORTED_ERR;
        return 0;
    }

    RefPtr<CanvasGradient> gradient = CanvasGradient::create(FloatPoint(x0, y0), FloatPoint(x1, y1));
    prepareGradientForDashboard(gradient.get());
    return gradient.release();
}

PassRefPtr<CanvasGradient> CanvasRenderingContext2D::createRadialGradient(float x0, float y0, float r0, float x1, float y1, float r1, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!isfinite(x0) || !isfinite(y0) || !isfinite(r0) || !isfinite(x1) || !isfinite(y1) || !isfinite(r1)) {
        ec = NOT_SUPPORTED_ERR;
        return 0;
    }

    if (r0 < 0 || r1 < 0) {
        ec = INDEX_SIZE_ERR;
        return 0;
    }

    RefPtr<CanvasGradient> gradient = CanvasGradient::create(FloatPoint(x0, y0), r0, FloatPoint(x1, y1), r1);
    prepareGradientForDashboard(gradient.get());
    return gradient.release();
}

PassRefPtr<CanvasPattern> CanvasRenderingContext2D::createPattern(HTMLImageElement* image,
    const String& repetitionType, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!image) {
        ec = TYPE_MISMATCH_ERR;
        return 0;
    }
    bool repeatX, repeatY;
    ec = 0;
    CanvasPattern::parseRepetitionType(repetitionType, repeatX, repeatY, ec);
    if (ec)
        return 0;

    if (!image->complete())
        return 0;

    CachedImage* cachedImage = image->cachedImage();
    if (!cachedImage || !image->cachedImage()->image())
        return CanvasPattern::create(Image::nullImage(), repeatX, repeatY, true);

    bool originClean = !canvas()->securityOrigin().taintsCanvas(KURL(KURL(), cachedImage->response().url())) && cachedImage->image()->hasSingleSecurityOrigin();
    return CanvasPattern::create(cachedImage->image(), repeatX, repeatY, originClean);
}

PassRefPtr<CanvasPattern> CanvasRenderingContext2D::createPattern(HTMLCanvasElement* canvas,
    const String& repetitionType, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!canvas) {
        ec = TYPE_MISMATCH_ERR;
        return 0;
    }
    if (!canvas->width() || !canvas->height()) {
        ec = INVALID_STATE_ERR;
        return 0;
    }

    bool repeatX, repeatY;
    ec = 0;
    CanvasPattern::parseRepetitionType(repetitionType, repeatX, repeatY, ec);
    if (ec)
        return 0;

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas->getCanvasType() == HTMLCanvasElement::RECORDING) {
        GraphicsContext* c = canvas->drawingContext();
        if (!c)
            return 0;
        canvas->makeRenderingResultsAvailable();

        IntRect size(0, 0, canvas->width(), canvas->height());
        RefPtr<ByteArray> byteArray = canvas->getUnmultipliedImageData(size);
        if (!byteArray)
            return 0;

        RefPtr<Image> image = BitmapImage::create(IntSize(canvas->width(), canvas->height()), byteArray.release());
        return CanvasPattern::create(image.release(), repeatX, repeatY, canvas->originClean());
    } else
#endif
        return CanvasPattern::create(canvas->copiedImage(), repeatX, repeatY, canvas->originClean());
    /// @}
}

void CanvasRenderingContext2D::didDraw(const FloatRect& r, unsigned options)
{
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;

    FloatRect dirtyRect = r;
    if (options & CanvasDidDrawApplyTransform) {
        AffineTransform ctm = state().m_transform;
        dirtyRect = ctm.mapRect(r);
    }

    if (options & CanvasDidDrawApplyShadow && alphaChannel(state().m_shadowColor)) {
        // The shadow gets applied after transformation
        FloatRect shadowRect(dirtyRect);
        shadowRect.move(state().m_shadowOffset);
        shadowRect.inflate(state().m_shadowBlur);
        dirtyRect.unite(shadowRect);
    }

    if (options & CanvasDidDrawApplyClip) {
        // FIXME: apply the current clip to the rectangle. Unfortunately we can't get the clip
        // back out of the GraphicsContext, so to take clip into account for incremental painting,
        // we'd have to keep the clip path around.
    }

#if ENABLE(ACCELERATED_2D_CANVAS)
    if (isAccelerated())
        drawingContext()->markDirtyRect(enclosingIntRect(dirtyRect));
#endif
#if ENABLE(ACCELERATED_2D_CANVAS) && USE(ACCELERATED_COMPOSITING)
    // If we are drawing to hardware and we have a composited layer, just call contentChanged().
    RenderBox* renderBox = canvas()->renderBox();
    if (isAccelerated() && renderBox && renderBox->hasLayer() && renderBox->layer()->hasAcceleratedCompositing())
        renderBox->layer()->contentChanged(RenderLayer::CanvasChanged);
    else
#endif
        canvas()->didDraw(dirtyRect);
}

GraphicsContext* CanvasRenderingContext2D::drawingContext() const
{
    /// M: for canvas hw acceleration @{
    return m_currentDrawingContext;
    /// @}
}

static PassRefPtr<ImageData> createEmptyImageData(const IntSize& size)
{
    RefPtr<ImageData> data = ImageData::create(size);
    memset(data->data()->data()->data(), 0, data->data()->data()->length());
    return data.release();
}

PassRefPtr<ImageData> CanvasRenderingContext2D::createImageData(PassRefPtr<ImageData> imageData, ExceptionCode& ec) const
{
    WTRACE_METHOD();
    if (!imageData) {
        ec = NOT_SUPPORTED_ERR;
        return 0;
    }

    return createEmptyImageData(imageData->size());
}

PassRefPtr<ImageData> CanvasRenderingContext2D::createImageData(float sw, float sh, ExceptionCode& ec) const
{
    ec = 0;
    if (!sw || !sh) {
        ec = INDEX_SIZE_ERR;
        return 0;
    }
    if (!isfinite(sw) || !isfinite(sh)) {
        ec = NOT_SUPPORTED_ERR;
        return 0;
    }

    FloatSize unscaledSize(fabs(sw), fabs(sh));
    IntSize scaledSize = canvas()->convertLogicalToDevice(unscaledSize);
    if (scaledSize.width() < 1)
        scaledSize.setWidth(1);
    if (scaledSize.height() < 1)
        scaledSize.setHeight(1);

    float area = 4.0f * scaledSize.width() * scaledSize.height();
    if (area > static_cast<float>(std::numeric_limits<int>::max()))
        return 0;

    return createEmptyImageData(scaledSize);
}

PassRefPtr<ImageData> CanvasRenderingContext2D::getImageData(float sx, float sy, float sw, float sh, ExceptionCode& ec) const
{
    if (!canvas()->originClean()) {
        ec = SECURITY_ERR;
        return 0;
    }
    if (!sw || !sh) {
        ec = INDEX_SIZE_ERR;
        return 0;
    }
    if (!isfinite(sx) || !isfinite(sy) || !isfinite(sw) || !isfinite(sh)) {
        ec = NOT_SUPPORTED_ERR;
        return 0;
    }

    if (sw < 0) {
        sx += sw;
        sw = -sw;
    }    
    if (sh < 0) {
        sy += sh;
        sh = -sh;
    }

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING)
        canvas()->makeRenderingResultsAvailable();
#endif
    /// @}
    
    FloatRect unscaledRect(sx, sy, sw, sh);
    IntRect scaledRect = canvas()->convertLogicalToDevice(unscaledRect);
    if (scaledRect.width() < 1)
        scaledRect.setWidth(1);
    if (scaledRect.height() < 1)
        scaledRect.setHeight(1);

    /// M: for canvas hw acceleration @{
    RefPtr<ByteArray> byteArray;
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
        byteArray = canvas()->getUnmultipliedImageData(scaledRect);
    } else
#endif
    {
        ImageBuffer* buffer = canvas()->buffer();
        if (!buffer)
            return createEmptyImageData(scaledRect.size());

        byteArray = buffer->getUnmultipliedImageData(scaledRect);
    }
    /// @}
    if (!byteArray)
        return 0;

#if ENABLE(MTK_GLCANVAS)
    if (m_switchForImageData) {
        if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
            // the following two function should be called in sequence. Switch the order is wrong.
            canvas()->stopRecording();
        }
        canvas()->disableRecordingCanvas();
    }
#endif
    return ImageData::create(scaledRect.size(), byteArray.release());
}

void CanvasRenderingContext2D::putImageData(ImageData* data, float dx, float dy, ExceptionCode& ec)
{
    if (!data) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    putImageData(data, dx, dy, 0, 0, data->width(), data->height(), ec);
}

void CanvasRenderingContext2D::putImageData(ImageData* data, float dx, float dy, float dirtyX, float dirtyY,
                                            float dirtyWidth, float dirtyHeight, ExceptionCode& ec)
{
    WTRACE_METHOD();
    if (!data) {
        ec = TYPE_MISMATCH_ERR;
        return;
    }
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dirtyX) || !isfinite(dirtyY) || !isfinite(dirtyWidth) || !isfinite(dirtyHeight)) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    ImageBuffer* buffer = canvas()->buffer();
    if (!buffer)
        return;

    if (dirtyWidth < 0) {
        dirtyX += dirtyWidth;
        dirtyWidth = -dirtyWidth;
    }

    if (dirtyHeight < 0) {
        dirtyY += dirtyHeight;
        dirtyHeight = -dirtyHeight;
    }

    FloatRect clipRect(dirtyX, dirtyY, dirtyWidth, dirtyHeight);
    clipRect.intersect(IntRect(0, 0, data->width(), data->height()));
    IntSize destOffset(static_cast<int>(dx), static_cast<int>(dy));
    IntRect destRect = enclosingIntRect(clipRect);
    destRect.move(destOffset);
    destRect.intersect(IntRect(IntPoint(), buffer->size()));
    if (destRect.isEmpty())
        return;
    IntRect sourceRect(destRect);
    sourceRect.move(-destOffset);

    // TODO: Must implement putImageData
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (canvas()->getCanvasType() == HTMLCanvasElement::RECORDING) {
        RefPtr<ByteArray> byteArray = data->data()->data();
        if (!byteArray)
            return;
        GraphicsContext* c = drawingContext();
        if (!c)
            return;

        // putImageData() is not affected by clipping regions
        resetClip();
        IntRect dest = IntRect(destOffset.width(), destOffset.height(), destRect.width(), destRect.height());
        RefPtr<Image> image = BitmapImage::create(IntSize(data->width(), data->height()), byteArray.release());
        c->clearRect(FloatRect((float)destRect.x(), (float)destRect.y(), (float)destRect.width(), (float)destRect.height()));
        c->drawImage(image.get(), ColorSpaceDeviceRGB, destRect, sourceRect, CompositeSourceOver);
        // recover clip region
        clip();
    } else
#endif
    {
        buffer->putUnmultipliedImageData(data->data()->data(), IntSize(data->width(), data->height()), sourceRect, IntPoint(destOffset));
    }
    /// @}

    didDraw(destRect, CanvasDidDrawApplyNone); // ignore transform, shadow and clip
}

String CanvasRenderingContext2D::font() const
{
    return state().m_unparsedFont;
}

void CanvasRenderingContext2D::setFont(const String& newFont)
{
    WTRACE_METHOD();
    RefPtr<CSSMutableStyleDeclaration> tempDecl = CSSMutableStyleDeclaration::create();
    CSSParser parser(!m_usesCSSCompatibilityParseMode);

    String declarationText("font: ");
    declarationText += newFont;
    parser.parseDeclaration(tempDecl.get(), declarationText);
    if (!tempDecl->length())
        return;

    // The parse succeeded.
    state().m_unparsedFont = newFont;

    // Map the <canvas> font into the text style. If the font uses keywords like larger/smaller, these will work
    // relative to the canvas.
    RefPtr<RenderStyle> newStyle = RenderStyle::create();
    if (RenderStyle* computedStyle = canvas()->computedStyle())
        newStyle->setFontDescription(computedStyle->fontDescription());
    newStyle->font().update(newStyle->font().fontSelector());

    // Now map the font property into the style.
    CSSStyleSelector* styleSelector = canvas()->styleSelector();
    styleSelector->applyPropertyToStyle(CSSPropertyFont, tempDecl->getPropertyCSSValue(CSSPropertyFont).get(), newStyle.get());

    state().m_font = newStyle->font();
    state().m_font.update(styleSelector->fontSelector());
    state().m_realizedFont = true;
    styleSelector->fontSelector()->registerForInvalidationCallbacks(&state());
}

String CanvasRenderingContext2D::textAlign() const
{
    return textAlignName(state().m_textAlign);
}

void CanvasRenderingContext2D::setTextAlign(const String& s)
{
    WTRACE_METHOD();
    TextAlign align;
    if (!parseTextAlign(s, align))
        return;
    state().m_textAlign = align;
}

String CanvasRenderingContext2D::textBaseline() const
{
    return textBaselineName(state().m_textBaseline);
}

void CanvasRenderingContext2D::setTextBaseline(const String& s)
{
    TextBaseline baseline;
    if (!parseTextBaseline(s, baseline))
        return;
    state().m_textBaseline = baseline;
}

void CanvasRenderingContext2D::fillText(const String& text, float x, float y)
{
    drawTextInternal(text, x, y, true);
}

void CanvasRenderingContext2D::fillText(const String& text, float x, float y, float maxWidth)
{
    drawTextInternal(text, x, y, true, maxWidth, true);
}

void CanvasRenderingContext2D::strokeText(const String& text, float x, float y)
{
    drawTextInternal(text, x, y, false);
}

void CanvasRenderingContext2D::strokeText(const String& text, float x, float y, float maxWidth)
{
    drawTextInternal(text, x, y, false, maxWidth, true);
}

PassRefPtr<TextMetrics> CanvasRenderingContext2D::measureText(const String& text)
{
    RefPtr<TextMetrics> metrics = TextMetrics::create();

#if PLATFORM(QT)
    // We always use complex text shaping since it can't be turned off for QPainterPath::addText().
    Font::CodePath oldCodePath = Font::codePath();
    Font::setCodePath(Font::Complex);
#endif

    metrics->setWidth(accessFont().width(TextRun(text.characters(), text.length())));

#if PLATFORM(QT)
    Font::setCodePath(oldCodePath);
#endif

    return metrics.release();
}

void CanvasRenderingContext2D::drawTextInternal(const String& text, float x, float y, bool fill, float maxWidth, bool useMaxWidth)
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;
    if (!isfinite(x) | !isfinite(y))
        return;
    /// M: W3C test case support start @{
    /* Fix 2d.text.draw.fill.maxWidth.negative.html, check parameter correctness */
    if ((useMaxWidth && !isfinite(maxWidth)) || (useMaxWidth && maxWidth <= 0)) {
        return;
    }
    /// @}

    const Font& font = accessFont();
    const FontMetrics& fontMetrics = font.fontMetrics();

    // FIXME: Handle maxWidth.
    // FIXME: Need to turn off font smoothing.

    RenderStyle* computedStyle = canvas()->computedStyle();
    bool rtl = computedStyle ? !computedStyle->isLeftToRightDirection() : false;
    bool override = computedStyle ? computedStyle->unicodeBidi() == Override : false;

    unsigned length = text.length();
    const UChar* string = text.characters();
    TextRun textRun(string, length, false, 0, 0, TextRun::AllowTrailingExpansion, rtl, override);

    // Draw the item text at the correct point.
    FloatPoint location(x, y);
    switch (state().m_textBaseline) {
    case TopTextBaseline:
    case HangingTextBaseline:
        location.setY(y + fontMetrics.ascent());
        break;
    case BottomTextBaseline:
    case IdeographicTextBaseline:
        location.setY(y - fontMetrics.descent());
        break;
    case MiddleTextBaseline:
        location.setY(y - fontMetrics.descent() + fontMetrics.height() / 2);
        break;
    case AlphabeticTextBaseline:
    default:
         // Do nothing.
        break;
    }

    /// M: W3C test case support start @{
    /* Fix 2d.text relative case */
    float fontWidth = font.width(TextRun(text, false, 0, 0, TextRun::AllowTrailingExpansion, rtl, override));
    useMaxWidth = (useMaxWidth && (fontWidth > maxWidth));
    float width = useMaxWidth ? maxWidth : fontWidth;
    /// @}

    TextAlign align = state().m_textAlign;
    if (align == StartTextAlign)
         align = rtl ? RightTextAlign : LeftTextAlign;
    else if (align == EndTextAlign)
        align = rtl ? LeftTextAlign : RightTextAlign;

    switch (align) {
    case CenterTextAlign:
        location.setX(location.x() - width / 2);
        break;
    case RightTextAlign:
        location.setX(location.x() - width);
        break;
    default:
        break;
    }

    // The slop built in to this mask rect matches the heuristic used in FontCGWin.cpp for GDI text.
    FloatRect textRect = FloatRect(location.x() - fontMetrics.height() / 2, location.y() - fontMetrics.ascent() - fontMetrics.lineGap(),
                                   width + fontMetrics.height(), fontMetrics.lineSpacing());
    if (!fill)
        textRect.inflate(c->strokeThickness() / 2);

#if USE(CG)
    CanvasStyle* drawStyle = fill ? state().m_fillStyle.get() : state().m_strokeStyle.get();
    if (drawStyle->canvasGradient() || drawStyle->canvasPattern()) {
        // FIXME: The rect is not big enough for miters on stroked text.
        IntRect maskRect = enclosingIntRect(textRect);

#if USE(IOSURFACE_CANVAS_BACKING_STORE)
        OwnPtr<ImageBuffer> maskImage = ImageBuffer::create(maskRect.size(), ColorSpaceDeviceRGB, Accelerated);
#else
        OwnPtr<ImageBuffer> maskImage = ImageBuffer::create(maskRect.size());
#endif

        GraphicsContext* maskImageContext = maskImage->context();

        if (fill)
            maskImageContext->setFillColor(Color::black, ColorSpaceDeviceRGB);
        else {
            maskImageContext->setStrokeColor(Color::black, ColorSpaceDeviceRGB);
            maskImageContext->setStrokeThickness(c->strokeThickness());
        }

        maskImageContext->setTextDrawingMode(fill ? TextModeFill : TextModeStroke);

        /// M: W3C test case support start @{
        /* Fix 2d.text relative case */
        if (useMaxWidth) {
            maskImageContext->translate(location.x() - maskRect.x(), location.y() - maskRect.y());
            // We draw when fontWidth is 0 so compositing operations (eg, a "copy" op) still work.
            maskImageContext->scale(FloatSize((fontWidth > 0 ? (width / fontWidth) : 0), 1));
            maskImageContext->drawBidiText(font, textRun, FloatPoint(0, 0));
        } else {
            maskImageContext->translate(-maskRect.x(), -maskRect.y());
            maskImageContext->drawBidiText(font, textRun, location);
        }
        /// @}

        c->save();
        c->clipToImageBuffer(maskImage.get(), maskRect);
        drawStyle->applyFillColor(c);
        c->fillRect(maskRect);
        c->restore();

        return;
    }
#endif

    c->setTextDrawingMode(fill ? TextModeFill : TextModeStroke);

#if PLATFORM(QT)
    // We always use complex text shaping since it can't be turned off for QPainterPath::addText().
    Font::CodePath oldCodePath = Font::codePath();
    Font::setCodePath(Font::Complex);
#endif

    /// M: W3C test case support start @{
    /* Fix 2d.text relative case */
    if (useMaxWidth) {
        c->save();
        c->translate(location.x(), location.y());
        // We draw when fontWidth is 0 so compositing operations (eg, a "copy" op) still work.
        c->scale(FloatSize((fontWidth > 0 ? (width / fontWidth) : 0), 1));
        c->drawBidiText(font, textRun, FloatPoint(0, 0));
        c->restore();
    } else {
        c->drawBidiText(font, textRun, location);
    }
    /// @}

    if (fill)
        didDraw(textRect);
    else {
        // When stroking text, pointy miters can extend outside of textRect, so we
        // punt and dirty the whole canvas.
        didDraw(FloatRect(0, 0, canvas()->width(), canvas()->height()));
    }

#if PLATFORM(QT)
    Font::setCodePath(oldCodePath);
#endif
}

const Font& CanvasRenderingContext2D::accessFont()
{
    canvas()->document()->updateStyleIfNeeded();

    if (!state().m_realizedFont)
        setFont(state().m_unparsedFont);
    return state().m_font;
}

void CanvasRenderingContext2D::paintRenderingResultsToCanvas()
{
#if ENABLE(ACCELERATED_2D_CANVAS)
    if (GraphicsContext* c = drawingContext())
        c->syncSoftwareCanvas();
#endif
}

#if ENABLE(ACCELERATED_2D_CANVAS) && USE(ACCELERATED_COMPOSITING)
PlatformLayer* CanvasRenderingContext2D::platformLayer() const
{
    return m_drawingBuffer ? m_drawingBuffer->platformLayer() : 0;
}
#endif

/// M: for canvas hw acceleration @{
void CanvasRenderingContext2D::resetClip()
{
    WTRACE_METHOD();
    GraphicsContext* c = drawingContext();
    if (!c)
        return;
    if (!state().m_invertibleCTM)
        return;
    c->resetClip(FloatRect(0, 0, canvas()->width(), canvas()->height()));
}
/// @}

} // namespace WebCore
