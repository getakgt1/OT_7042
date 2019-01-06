

#include "config.h"
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
//#include "hwuiIncludes.h"
#include "PlatformGraphicsContextDisplayList.h"

#include "AndroidLog.h"
#include "Font.h"
#include "GraphicsContext.h"
#include "SkCanvas.h"
#include "SkCornerPathEffect.h"
#include "SkPaint.h"
#include "SkShader.h"
#include "SkiaUtils.h"
#include "SkUtils.h"

#include "utils/Errors.h"
#include "DrawGlInfo.h"
#include "OpenGLDL.h"

#include "SkBitmapRef.h"

// for debug
#define DEBUG_CANVAS 0
#include "AndroidLog.h"

#if DEBUG_CANVAS
#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "Canvas", __VA_ARGS__)
#else
#undef XLOG
#define XLOG(...)
#endif

namespace WebCore {

// These are the flags we need when we call saveLayer for transparency.
// Since it does not appear that webkit intends this to also save/restore
// the matrix or clip, I do not give those flags (for performance)
#define TRANSPARENCY_SAVEFLAGS                                  \
    (SkCanvas::SaveFlags)(SkCanvas::kHasAlphaLayer_SaveFlag |   \
                          SkCanvas::kFullColorLayer_SaveFlag)

//**************************************
// Helper functions
//**************************************

static void setrectForUnderline(SkRect* r, float lineThickness,
                                const FloatPoint& point, int yOffset, float width)
{
#if 0
    if (lineThickness < 1) // Do we really need/want this?
        lineThickness = 1;
#endif
    r->fLeft    = point.x();
    r->fTop     = point.y() + yOffset;
    r->fRight   = r->fLeft + width;
    r->fBottom  = r->fTop + lineThickness;
}

static inline int fastMod(int value, int max)
{
    int sign = SkExtractSign(value);

    value = SkApplySign(value, sign);
    if (value >= max)
        value %= max;
    return SkApplySign(value, sign);
}

static inline void fixPaintForBitmapsThatMaySeam(SkPaint* paint) {
    /*  Bitmaps may be drawn to seem next to other images. If we are drawn
        zoomed, or at fractional coordinates, we may see cracks/edges if
        we antialias, because that will cause us to draw the same pixels
        more than once (e.g. from the left and right bitmaps that share
        an edge).

        Disabling antialiasing fixes this, and since so far we are never
        rotated at non-multiple-of-90 angles, this seems to do no harm
     */
    paint->setAntiAlias(false);
}

//**************************************
// PlatformGraphicsContextSkia
//**************************************

PlatformGraphicsContextDisplayList::PlatformGraphicsContextDisplayList(bool takeCanvasOwnership)
    : PlatformGraphicsContext()
    , m_deleteCanvas(takeCanvasOwnership)
{
    m_gc = 0;
    m_renderer = new OpenGLDLRWrapper();
}

PlatformGraphicsContextDisplayList::~PlatformGraphicsContextDisplayList()
{
    delete m_renderer;
}

bool PlatformGraphicsContextDisplayList::beginRecording(int width, int height)
{
    m_size = IntSize(width, height);
    m_renderer->setViewport(width, height);
    android::status_t result = m_renderer->prepareDirty(0, 0, width, height, false);
    if (DrawGlInfo::kStatusDone != result) {
        XLOG("PlatformGraphicsContextDisplayList::beginRecording] this=[%p] prepareDirty error=[%d]", this , result);
        return false;
    }
    return true;
}


bool PlatformGraphicsContextDisplayList::isPaintingDisabled()
{
    return !m_renderer;
}

//**************************************
// State management
//**************************************

void PlatformGraphicsContextDisplayList::beginTransparencyLayer(float opacity)
{
    m_renderer->saveLayerAlpha(0, 0, static_cast<float>(m_size.width()), static_cast<float>(m_size.height()), (int)(opacity * 255), TRANSPARENCY_SAVEFLAGS);
}


void PlatformGraphicsContextDisplayList::endRecording()
{
    m_renderer->finish();
}

OpenGLDLWrapper* PlatformGraphicsContextDisplayList::genRecordingResult()
{
    return m_renderer->genDisplayList();
}

void PlatformGraphicsContextDisplayList::reset()
{
    m_renderer->reset();
}


void PlatformGraphicsContextDisplayList::endTransparencyLayer()
{
    if (!m_renderer)
        return;
    m_renderer->restore();
}

void PlatformGraphicsContextDisplayList::save()
{
    PlatformGraphicsContext::save();
    // Save our native canvas.
    m_renderer->save(SkCanvas::kMatrixClip_SaveFlag);
}

void PlatformGraphicsContextDisplayList::restore()
{
    PlatformGraphicsContext::restore();
    // Restore our native canvas.
    m_renderer->restore();
}

//**************************************
// Matrix operations
//**************************************

void PlatformGraphicsContextDisplayList::concatCTM(const AffineTransform& affine)
{
    SkMatrix tmp = affine;
    m_renderer->concatMatrix(&tmp);
}

void PlatformGraphicsContextDisplayList::rotate(float angleInRadians)
{
    float value = angleInRadians * (180.0f / 3.14159265f);
    m_renderer->rotate(value);
}

void PlatformGraphicsContextDisplayList::scale(const FloatSize& size)
{
    m_renderer->scale(size.width(), size.height());
}

void PlatformGraphicsContextDisplayList::translate(float x, float y)
{
    m_renderer->translate(x, y);
}

const SkMatrix& PlatformGraphicsContextDisplayList::getTotalMatrix()
{
    m_renderer->getMatrix(&m_totalMatrix);
    return m_totalMatrix;
}

//**************************************
// Clipping
//**************************************

void PlatformGraphicsContextDisplayList::addInnerRoundedRectClip(const IntRect& rect,
                                                      int thickness)
{
    SkPath path;
    SkRect r(rect);

    path.addOval(r, SkPath::kCW_Direction);
    // Only perform the inset if we won't invert r
    if (2 * thickness < rect.width() && 2 * thickness < rect.height()) {
        // Adding one to the thickness doesn't make the border too thick as
        // it's painted over afterwards. But without this adjustment the
        // border appears a little anemic after anti-aliasing.
        r.inset(SkIntToScalar(thickness + 1), SkIntToScalar(thickness + 1));
        path.addOval(r, SkPath::kCCW_Direction);
    }
    const SkRect pathBounds = path.getBounds();
    m_renderer->clipRect(SkScalarToFloat(pathBounds.fLeft), SkScalarToFloat(pathBounds.fTop),
                        SkScalarToFloat(pathBounds.fRight), SkScalarToFloat(pathBounds.fBottom),
                        SkRegion::kIntersect_Op);
}

void PlatformGraphicsContextDisplayList::canvasClip(const Path& path)
{
    clip(path);
}

bool PlatformGraphicsContextDisplayList::clip(const FloatRect& rect)
{
    return m_renderer->clipRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), SkRegion::kIntersect_Op);
}

bool PlatformGraphicsContextDisplayList::clip(const Path& path)
{
    const SkRect& pathBounds = path.platformPath()->getBounds();
    return m_renderer->clipRect(SkScalarToFloat(pathBounds.fLeft), SkScalarToFloat(pathBounds.fTop),
                        SkScalarToFloat(pathBounds.fRight), SkScalarToFloat(pathBounds.fBottom),
                        SkRegion::kIntersect_Op);
}

bool PlatformGraphicsContextDisplayList::clipConvexPolygon(size_t numPoints,
                                                const FloatPoint*, bool antialias)
{
    if (numPoints <= 1)
        return true;

    // This is only used if HAVE_PATH_BASED_BORDER_RADIUS_DRAWING is defined
    // in RenderObject.h which it isn't for us. TODO: Support that :)
    return true;
}

bool PlatformGraphicsContextDisplayList::clipOut(const IntRect& r)
{
    return m_renderer->clipRect(static_cast<float>(r.x()), static_cast<float>(r.y()),
                        static_cast<float>(r.maxX()), static_cast<float>(r.maxY()), SkRegion::kDifference_Op);
}

bool PlatformGraphicsContextDisplayList::clipOut(const Path& path)
{
    const SkRect pathBounds = path.platformPath()->getBounds();
    return m_renderer->clipRect(SkScalarToFloat(pathBounds.fLeft), SkScalarToFloat(pathBounds.fTop), SkScalarToFloat(pathBounds.fRight)
                        , SkScalarToFloat(pathBounds.fBottom), SkRegion::kDifference_Op);
}

bool PlatformGraphicsContextDisplayList::clipPath(const Path& pathToClip, WindRule clipRule)
{
    SkPath path = *pathToClip.platformPath();
    path.setFillType(clipRule == RULE_EVENODD
            ? SkPath::kEvenOdd_FillType : SkPath::kWinding_FillType);

    const SkRect pathBounds = path.getBounds();
    return m_renderer->clipRect(SkScalarToFloat(pathBounds.fLeft), SkScalarToFloat(pathBounds.fTop), SkScalarToFloat(pathBounds.fRight)
                        , SkScalarToFloat(pathBounds.fBottom), SkRegion::kIntersect_Op);
}

void PlatformGraphicsContextDisplayList::clearRect(const FloatRect& rect)
{
    SkPaint paint;

    setupPaintFill(&paint);
    paint.setXfermodeMode(SkXfermode::kClear_Mode);

    m_renderer->drawRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), &paint);
}

//**************************************
// Drawing
//**************************************

void PlatformGraphicsContextDisplayList::drawBitmapPattern(
        const SkBitmap& bitmap, const SkMatrix& matrix,
        CompositeOperator compositeOp, const FloatRect& destRect)
{
    SkShader* shader = SkShader::CreateBitmapShader(bitmap,
                                                    SkShader::kRepeat_TileMode,
                                                    SkShader::kRepeat_TileMode);
    shader->setLocalMatrix(matrix);
    SkPaint paint;
    setupPaintCommon(&paint);
    paint.setAlpha(getNormalizedAlpha());
    paint.setShader(shader);
    paint.setXfermodeMode(WebCoreCompositeToSkiaComposite(compositeOp));
    /// M: fix memory leak
    SkSafeUnref(shader);
    fixPaintForBitmapsThatMaySeam(&paint);
    //mCanvas->drawRect(destRect, paint);
    m_renderer->drawRect(destRect.x(), destRect.y(), destRect.maxX(), destRect.maxY(), &paint);
}

void PlatformGraphicsContextDisplayList::drawBitmapRect(const SkBitmap& bitmap,
                                             const SkIRect* src, const SkRect& dst,
                                             CompositeOperator op)
{
    SkPaint paint;
    setupPaintCommon(&paint);
    paint.setAlpha(getNormalizedAlpha());
    paint.setXfermodeMode(WebCoreCompositeToSkiaComposite(op));
    fixPaintForBitmapsThatMaySeam(&paint);

    SkRect srcRect;
    srcRect.set(*src);
    SkBitmap* bitmapPtr = const_cast<SkBitmap*>(&bitmap);
    m_renderer->drawBitmap(bitmapPtr, SkScalarToFloat(srcRect.fLeft), SkScalarToFloat(srcRect.fTop)
                            , SkScalarToFloat(srcRect.fRight), SkScalarToFloat(srcRect.fBottom)
                            , SkScalarToFloat(dst.fLeft), SkScalarToFloat(dst.fTop)
                            , SkScalarToFloat(dst.fRight), SkScalarToFloat(dst.fBottom), &paint);
}

void PlatformGraphicsContextDisplayList::drawConvexPolygon(size_t numPoints,
                                                const FloatPoint* points,
                                                bool shouldAntialias)
{
    if (numPoints <= 1)
        return;

    SkPaint paint;
    SkPath path;

    path.incReserve(numPoints);
    path.moveTo(SkFloatToScalar(points[0].x()), SkFloatToScalar(points[0].y()));
    for (size_t i = 1; i < numPoints; i++)
        path.lineTo(SkFloatToScalar(points[i].x()), SkFloatToScalar(points[i].y()));

    const SkRect bounds = path.getBounds();
    if (m_renderer->quickReject(SkScalarToFloat(bounds.fLeft), SkScalarToFloat(bounds.fTop), SkScalarToFloat(bounds.fRight), SkScalarToFloat(bounds.fBottom))) {
        return;
    }

    if (m_state->fillColor & 0xFF000000) {
        setupPaintFill(&paint);
        paint.setAntiAlias(shouldAntialias);
        m_renderer->drawPath(&path, &paint);
    }

    if (m_state->strokeStyle != NoStroke) {
        paint.reset();
        setupPaintStroke(&paint, 0);
        paint.setAntiAlias(shouldAntialias);
        m_renderer->drawPath(&path, &paint);
    }
}

void PlatformGraphicsContextDisplayList::drawEllipse(const IntRect& rect)
{
    SkPaint paint;
    SkRect oval(rect);

    if (m_state->fillColor & 0xFF000000) {
        setupPaintFill(&paint);
        m_renderer->drawOval(SkScalarToFloat(oval.fLeft), SkScalarToFloat(oval.fTop), SkScalarToFloat(oval.fRight), SkScalarToFloat(oval.fBottom), &paint);
    }
    if (m_state->strokeStyle != NoStroke) {
        paint.reset();
        setupPaintStroke(&paint, &oval);
        m_renderer->drawOval(SkScalarToFloat(oval.fLeft), SkScalarToFloat(oval.fTop), SkScalarToFloat(oval.fRight), SkScalarToFloat(oval.fBottom), &paint);
    }
}

void PlatformGraphicsContextDisplayList::drawFocusRing(const Vector<IntRect>& rects,
                                            int /* width */, int /* offset */,
                                            const Color& color)
{
    // Not Implement
#if 0
    unsigned rectCount = rects.size();
    if (!rectCount)
        return;

    SkRegion focusRingRegion;
    const SkScalar focusRingOutset = WebCoreFloatToSkScalar(0.8);
    for (unsigned i = 0; i < rectCount; i++) {
        SkIRect r = rects[i];
        r.inset(-focusRingOutset, -focusRingOutset);
        focusRingRegion.op(r, SkRegion::kUnion_Op);
    }

    SkPath path;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);

    paint.setColor(color.rgb());
    paint.setStrokeWidth(focusRingOutset * 2);
    paint.setPathEffect(new SkCornerPathEffect(focusRingOutset * 2))->unref();
    focusRingRegion.getBoundaryPath(&path);
    mCanvas->drawPath(path, paint);
#endif

}


void PlatformGraphicsContextDisplayList::drawHighlightForText(
        const Font& font, const TextRun& run, const FloatPoint& point, int h,
        const Color& backgroundColor, ColorSpace colorSpace, int from,
        int to, bool isActive)
{
    /// Not Implement
#if 0
    IntRect rect = (IntRect)font.selectionRectForText(run, point, h, from, to);
    if (isActive)
        fillRect(rect, backgroundColor);
    else {
        int x = rect.x(), y = rect.y(), w = rect.width(), h = rect.height();
        const int t = 3, t2 = t * 2;

        fillRect(IntRect(x, y, w, t), backgroundColor);
        fillRect(IntRect(x, y+h-t, w, t), backgroundColor);
        fillRect(IntRect(x, y+t, t, h-t2), backgroundColor);
        fillRect(IntRect(x+w-t, y+t, t, h-t2), backgroundColor);
    }
#endif
}

void PlatformGraphicsContextDisplayList::drawLine(const IntPoint& point1,
                                       const IntPoint& point2)
{
    StrokeStyle style = m_state->strokeStyle;
    if (style == NoStroke)
        return;

    SkPaint paint;
    const int idx = SkAbs32(point2.x() - point1.x());
    const int idy = SkAbs32(point2.y() - point1.y());

    // Special-case horizontal and vertical lines that are really just dots
    if (setupPaintStroke(&paint, 0, !idy) && (!idx || !idy)) {
        const float diameter = SkScalarToFloat(paint.getStrokeWidth());
        const float radius = diameter/2.0;
        float x = (float)(SkMin32(point1.x(), point2.x()));
        float y = (float)(SkMin32(point1.y(), point2.y()));
        float dx, dy;
        int count;
        FloatRect bounds;

        if (!idy) { // Horizontal
            bounds.setX(x);
            bounds.setY(y - radius);
            bounds.setWidth((float)idx);
            bounds.setHeight(2 * radius);

            x += radius;
            dx = diameter * 2;
            dy = 0;
            count = idx;
        } else { // Vertical
            bounds.setX(x - radius);
            bounds.setY(y);
            bounds.setWidth(2 * radius);
            bounds.setHeight((float)idy);

            y += radius;
            dx = 0;
            dy = diameter * 2;
            count = idy;
        }

        // The actual count is the number of ONs we hit alternating
        // ON(diameter), OFF(diameter), ...
        {
            float width = count / diameter;
            // Now compute the number of cells (ON and OFF)
            count = (int)width;
            // Now compute the number of ONs
            count = (count + 1) >> 1;
        }

        SkAutoMalloc storage(count * 2 * sizeof(float));
        float* verts = (float*)storage.get();
        for (int i = 0; i < count; i++) {
            verts[i * 2] = SkScalarToFloat(x);
            verts[i * 2 + 1] = SkScalarToFloat(y);
            x += dx;
            y += dy;
        }
        paint.setStyle(SkPaint::kFill_Style);
        paint.setPathEffect(0);

        m_renderer->save(SkCanvas::kClip_SaveFlag);
        m_renderer->clipRect(bounds.x(), bounds.y(), bounds.maxX(), bounds.maxY(), SkRegion::kIntersect_Op);
        m_renderer->drawPoints(verts, (count * 2), &paint);
        m_renderer->restore();
    } else {
        SkAutoMalloc storage(4 * sizeof(float));
        float* verts = (float*)storage.get();
        verts[0] = (float)point1.x();
        verts[1] = (float)point1.y();
        verts[2] = (float)point2.x();
        verts[3] = (float)point2.y();
        m_renderer->drawPoints(verts, 4, &paint);
    }
}

void PlatformGraphicsContextDisplayList::drawLineForText(const FloatPoint& pt, float width)
{
    SkRect r;
    setrectForUnderline(&r, m_state->strokeThickness, pt, 0, width);

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(m_state->strokeColor);

    m_renderer->drawRect(SkScalarToFloat(r.fLeft), SkScalarToFloat(r.fTop), SkScalarToFloat(r.fRight), SkScalarToFloat(r.fBottom), &paint);
}

void PlatformGraphicsContextDisplayList::drawLineForTextChecking(const FloatPoint& pt,
        float width, GraphicsContext::TextCheckingLineStyle)
{
    // TODO: Should we draw different based on TextCheckingLineStyle?
    SkRect r;
    setrectForUnderline(&r, m_state->strokeThickness, pt, 0, width);

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorRED); // Is this specified somewhere?

    m_renderer->drawRect(SkScalarToFloat(r.fLeft), SkScalarToFloat(r.fTop), SkScalarToFloat(r.fRight), SkScalarToFloat(r.fBottom), &paint);
}

void PlatformGraphicsContextDisplayList::drawRect(const IntRect& rect)
{
    SkPaint paint;
    SkRect r(rect);

    if (m_state->fillColor & 0xFF000000) {
        setupPaintFill(&paint);
        m_renderer->drawRect(SkScalarToFloat(r.fLeft), SkScalarToFloat(r.fTop), SkScalarToFloat(r.fRight), SkScalarToFloat(r.fBottom), &paint);
    }

    // According to GraphicsContext.h, stroking inside drawRect always means
    // a stroke of 1 inside the rect.
    if (m_state->strokeStyle != NoStroke && (m_state->strokeColor & 0xFF000000)) {
        paint.reset();
        setupPaintStroke(&paint, &r);
        paint.setPathEffect(0); // No dashing please
        paint.setStrokeWidth(SK_Scalar1); // Always just 1.0 width
        r.inset(SK_ScalarHalf, SK_ScalarHalf); // Ensure we're "inside"
        m_renderer->drawRect(SkScalarToFloat(r.fLeft), SkScalarToFloat(r.fTop), SkScalarToFloat(r.fRight), SkScalarToFloat(r.fBottom), &paint);
    }
}

void PlatformGraphicsContextDisplayList::fillPath(const Path& pathToFill, WindRule fillRule)
{
    SkPath* path = pathToFill.platformPath();
    if (!path)
        return;

    switch (fillRule) {
    case RULE_NONZERO:
        path->setFillType(SkPath::kWinding_FillType);
        break;
    case RULE_EVENODD:
        path->setFillType(SkPath::kEvenOdd_FillType);
        break;
    }

    SkPaint paint;
    setupPaintFill(&paint);

    bool setupShader = setShader(&paint);

    m_renderer->drawPath(path, &paint);

    if (setupShader) {
        resetShader();
    }
}

void PlatformGraphicsContextDisplayList::setShadow()
{
    if (SkColorGetA(m_state->shadow.color) > 0) {
    }

}

void PlatformGraphicsContextDisplayList::resetShadow()
{

}

bool PlatformGraphicsContextDisplayList::setShader(SkPaint* paint)
{
    SkiaShader* skiaShader = 0;
    if (paint->getShader()) {
        SkShader* shader = paint->getShader();

        Gradient* grad = m_gc->state().fillGradient.get();
        Pattern* pat = m_gc->state().fillPattern.get();

        if (grad) {
            SkShader::GradientInfo info;

            memset(&info, 0, sizeof(SkShader::GradientInfo));
            SkShader::GradientType type = shader->asAGradient(&info);

            SkColor* colors = new SkColor[info.fColorCount];
            SkScalar* colorOffset = new SkScalar[info.fColorCount];

            info.fColors = colors;
            info.fColorOffsets = colorOffset;

            type = shader->asAGradient(&info);

            float* bounds = new float[4];
            bounds[0] = info.fPoint[0].fX;
            bounds[1] = info.fPoint[0].fY;
            bounds[2] = info.fPoint[1].fX;
            bounds[3] = info.fPoint[1].fY;

            switch(type) {
                case SkShader::kLinear_GradientType:
                    skiaShader = m_renderer->createSkiaLinearGradientShader(bounds, colors, colorOffset, info.fColorCount,
                                     shader, info.fTileMode, 0,
                                     (shader->getFlags() & SkShader::kOpaqueAlpha_Flag) == 0);
                    m_renderer->setupShader(skiaShader);
                    break;
                case SkShader::kRadial2_GradientType:
                    /* HWUI only support one point radial gradient */
                    if (info.fPoint[0] == info.fPoint[1] && info.fRadius[0] <= 0.0f) {
                        float radius = SkScalarToFloat(info.fRadius[1]);
                        if (radius <= 0)
                            radius = SkScalarToFloat(SK_ScalarMin);
                        skiaShader = m_renderer->createSkiaCircularGradientShader(bounds[2], bounds[3], radius,
                                        colors, colorOffset, info.fColorCount, shader, info.fTileMode, 0,
                                        (shader->getFlags() & SkShader::kOpaqueAlpha_Flag) == 0);
                        m_renderer->setupShader(skiaShader);
                        break;
                    } else {
                        skiaShader = m_renderer->createSkiaTwoPointRadialShader(m_size.width(), m_size.height(), shader,
                                         paint, 0, (shader->getFlags() & SkShader::kOpaqueAlpha_Flag) == 0);
                        m_renderer->setupShader(skiaShader);
                        break;
                    }
                    ///TODO:  We don't support two-point gradient now.
                /* Not define in spec */
                case SkShader::kNone_GradientType:
                case SkShader::kColor_GradientType:
                case SkShader::kRadial_GradientType:
                case SkShader::kSweep_GradientType:
                default:
                    // Not Implemented others.
                    delete [] colors;
                    delete [] colorOffset;
                    delete [] bounds;
                    break;
            }
        } else if (pat) {
            SkBitmapRef* ref = pat->tileImage()->nativeImageForCurrentFrame();

            if (ref) {
                SkShader::TileMode tileModeX = pat->repeatX() ? SkShader::kRepeat_TileMode : SkShader::kClamp_TileMode;
                SkShader::TileMode tileModeY = pat->repeatY() ? SkShader::kRepeat_TileMode : SkShader::kClamp_TileMode;
                skiaShader = m_renderer->createSkiaBitmapShader(&ref->bitmap(), shader, tileModeX,
                                    tileModeY, 0,
                                    (shader->getFlags() & SkShader::kOpaqueAlpha_Flag) == 0);
                m_renderer->setupShader(skiaShader);
            }
        }
    }

    return (skiaShader != 0);
}

void PlatformGraphicsContextDisplayList::resetShader()
{
        m_renderer->resetShader();
}

void PlatformGraphicsContextDisplayList::fillRect(const FloatRect& rect)
{
    SkPaint paint;
    setupPaintFill(&paint);
    bool setupShader = setShader(&paint);

    m_renderer->drawRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), &paint);

    if (setupShader) {
        resetShader();
    }
}

void PlatformGraphicsContextDisplayList::fillRect(const FloatRect& rect,
                                       const Color& color)
{
    if (color.rgb() & 0xFF000000) {
        SkPaint paint;

        setupPaintCommon(&paint);
        paint.setColor(color.rgb()); // Punch in the specified color
        paint.setShader(0); // In case we had one set

        // Sometimes we record and draw portions of the page, using clips
        // for each portion. The problem with this is that webkit, sometimes,
        // sees that we're only recording a portion, and they adjust some of
        // their rectangle coordinates accordingly (e.g.
        // RenderBoxModelObject::paintFillLayerExtended() which calls
        // rect.intersect(paintInfo.rect) and then draws the bg with that
        // rect. The result is that we end up drawing rects that are meant to
        // seam together (one for each portion), but if the rects have
        // fractional coordinates (e.g. we are zoomed by a fractional amount)
        // we will double-draw those edges, resulting in visual cracks or
        // artifacts.

        // The fix seems to be to just turn off antialasing for rects (this
        // entry-point in GraphicsContext seems to have been sufficient,
        // though perhaps we'll find we need to do this as well in fillRect(r)
        // as well.) Currently setupPaintCommon() enables antialiasing.

        // Since we never show the page rotated at a funny angle, disabling
        // antialiasing seems to have no real down-side, and it does fix the
        // bug when we're zoomed (and drawing portions that need to seam).
        paint.setAntiAlias(false);
        m_renderer->drawRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), &paint);
    }
}

void PlatformGraphicsContextDisplayList::fillRoundedRect(
        const IntRect& rect, const IntSize& topLeft, const IntSize& topRight,
        const IntSize& bottomLeft, const IntSize& bottomRight,
        const Color& color)
{
    SkPaint paint;
    SkPath path;
    SkScalar radii[8];

    radii[0] = SkIntToScalar(topLeft.width());
    radii[1] = SkIntToScalar(topLeft.height());
    radii[2] = SkIntToScalar(topRight.width());
    radii[3] = SkIntToScalar(topRight.height());
    radii[4] = SkIntToScalar(bottomRight.width());
    radii[5] = SkIntToScalar(bottomRight.height());
    radii[6] = SkIntToScalar(bottomLeft.width());
    radii[7] = SkIntToScalar(bottomLeft.height());
    path.addRoundRect(rect, radii);

    setupPaintFill(&paint);
    paint.setColor(color.rgb());
    m_renderer->drawPath(&path, &paint);
}

void PlatformGraphicsContextDisplayList::strokeArc(const IntRect& r, int startAngle,
                                        int angleSpan)
{
    SkPath path;
    SkPaint paint;
    SkRect oval(r);

    if (m_state->strokeStyle == NoStroke) {
        setupPaintFill(&paint); // We want the fill color
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(SkFloatToScalar(m_state->strokeThickness));
    } else
        setupPaintStroke(&paint, 0);

    // We do this before converting to scalar, so we don't overflow SkFixed
    startAngle = fastMod(startAngle, 360);
    angleSpan = fastMod(angleSpan, 360);

    path.addArc(oval, SkIntToScalar(-startAngle), SkIntToScalar(-angleSpan));
    //mCanvas->drawPath(path, paint);
    m_renderer->drawPath(&path, &paint);
}

void PlatformGraphicsContextDisplayList::strokePath(const Path& pathToStroke)
{
    SkPath* path = pathToStroke.platformPath();
    if (!path)
        return;

    SkPaint paint;
    setupPaintStroke(&paint, 0);

    m_renderer->drawPath(path, &paint);
}

void PlatformGraphicsContextDisplayList::strokeRect(const FloatRect& rect, float lineWidth)
{
    SkPaint paint;

    setupPaintStroke(&paint, 0);
    paint.setStrokeWidth(SkFloatToScalar(lineWidth));
    m_renderer->drawRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), &paint);
}

void PlatformGraphicsContextDisplayList::drawPosText(const void* text, size_t byteLength,
                                              const SkPoint pos[], const SkPaint& paint)
{
    int characterSize = 0;
    switch (paint.getTextEncoding()) {
        case SkPaint::kUTF16_TextEncoding:
        case SkPaint::kGlyphID_TextEncoding:
            characterSize = byteLength/2;
            break;
        case SkPaint::kUTF8_TextEncoding:
            {
                const char* start = (const char*)text;
                const char* stop = start + byteLength;
                while (start < text) {
                    SkUTF8_NextUnichar(&start);
                    characterSize++;
                }
            }
            break;
        default:
            /* wrong condition */
            XLOG("[PlatformGraphicsContextDisplayList::drawPosText] encoding=[%d] is wrong", paint.getTextEncoding());
            return;
    }

    SkAutoMalloc storage(2 * characterSize * sizeof(float));
    float* positions = (float*)storage.get();
    // convert position from SkScalar to float
    for (int i = 0; i < characterSize; i++) {
        positions[i * 2] = SkScalarToFloat(pos[i].fX);
        positions[i * 2 + 1] = SkScalarToFloat(pos[i].fY);
    }

    m_renderer->drawPosText((const char*)text, (int)byteLength, characterSize, (const float*)positions, (SkPaint*)&paint);
}


void PlatformGraphicsContextDisplayList::drawMediaButton(const IntRect& rect, RenderSkinMediaButton::MediaButton buttonType,
                                                  bool translucent, bool drawBackground,
                                                  const IntRect& thumb)
{
//    RenderSkinMediaButton::Draw(mCanvas, rect, buttonType, translucent, drawBackground, thumb);
}


void PlatformGraphicsContextDisplayList::drawDisplayList(OpenGLDLWrapper* dlWrapper, const FloatRect& srcRect, const FloatRect& dstRect)
{
#if 0
    XLOG("[PlatformDisplay::drawDisplay] =a= dlWrapper=[%p]][%d] [%d %d] [%f %f %f %f] [%f %f %f %f]"
                    , dlWrapper, dlWrapper?dlWrapper->getDisplayListSize():0
                    , dlWrapper?dlWrapper->getWidth():0, dlWrapper?dlWrapper->getHeight():0
                    , srcRect.x(), srcRect.y(), srcRect.width(), srcRect.height()
                    , dstRect.x(), dstRect.y(), dstRect.width(), dstRect.height());
#endif
    if (dlWrapper) {
        SkPaint paint;
        setupPaintCommon(&paint);
        paint.setAlpha(getNormalizedAlpha());

        m_renderer->saveLayer(dstRect.x(), dstRect.y(), dstRect.maxX(), dstRect.maxY(), &paint, SkCanvas::kClip_SaveFlag | SkCanvas::kMatrix_SaveFlag | SkCanvas::kClipToLayer_SaveFlag);
        m_renderer->drawDisplayList(dlWrapper, srcRect.x(), srcRect.y(), srcRect.maxX(), srcRect.maxY()
                                        , dstRect.x(), dstRect.y(), dstRect.maxX(), dstRect.maxY());
        m_renderer->restore();
    }

}

/// M: for canvas hw acceleration @{
void PlatformGraphicsContextDisplayList::resetClip(const FloatRect& rect)
{
    m_renderer->clipRect(rect.x(), rect.y(), rect.maxX(), rect.maxY(), SkRegion::kReplace_Op);
}
/// @}


}   // WebCore

#endif
/// @}
