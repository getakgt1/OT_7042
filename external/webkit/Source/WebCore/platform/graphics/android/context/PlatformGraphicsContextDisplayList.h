/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)

#ifndef platform_graphics_context_display_list_h
#define platform_graphics_context_display_list_h

#include "PlatformGraphicsContext.h"
//#include "hwuiIncludes.h"
#include "OpenGLDL.h"

namespace WebCore {

class PlatformGraphicsContextDisplayList : public PlatformGraphicsContext {
public:
    PlatformGraphicsContextDisplayList(bool takeCanvasOwnership = false);

    virtual ~PlatformGraphicsContextDisplayList();
    virtual bool isPaintingDisabled();

    virtual ContextType type() { return PaintingContext; }
    virtual SkCanvas* recordingCanvas() { return 0; }
    virtual void setTextOffset(FloatSize offset) {}

    virtual PlatformContextType getPlatformContextType() { return DisplayListContext; }

    // FIXME: This is used by ImageBufferAndroid, which should really be
    //        managing the canvas lifecycle itself

    virtual bool deleteUs() const { return m_deleteCanvas; }

    // State management
    virtual void beginTransparencyLayer(float opacity);
    virtual void endTransparencyLayer();
    virtual void save();
    virtual void restore();

    // Matrix operations
    virtual void concatCTM(const AffineTransform& affine);
    virtual void rotate(float angleInRadians);
    virtual void scale(const FloatSize& size);
    virtual void translate(float x, float y);
    virtual const SkMatrix& getTotalMatrix();

    // Clipping
    virtual void addInnerRoundedRectClip(const IntRect& rect, int thickness);
    virtual void canvasClip(const Path& path);
    virtual bool clip(const FloatRect& rect);
    virtual bool clip(const Path& path);
    virtual bool clipConvexPolygon(size_t numPoints, const FloatPoint*, bool antialias);
    virtual bool clipOut(const IntRect& r);
    virtual bool clipOut(const Path& p);
    virtual bool clipPath(const Path& pathToClip, WindRule clipRule);
    virtual SkIRect getTotalClipBounds() { return mCanvas->getTotalClip().getBounds(); }
    /// M: for canvas hw acceleration @{
    virtual void resetClip(const FloatRect& rect);
    /// @}

    // Drawing
    virtual void clearRect(const FloatRect& rect);
    virtual void drawBitmapPattern(const SkBitmap& bitmap, const SkMatrix& matrix,
                           CompositeOperator compositeOp, const FloatRect& destRect);
    virtual void drawBitmapRect(const SkBitmap& bitmap, const SkIRect* src,
                        const SkRect& dst, CompositeOperator op = CompositeSourceOver);
    virtual void drawConvexPolygon(size_t numPoints, const FloatPoint* points,
                           bool shouldAntialias);
    virtual void drawEllipse(const IntRect& rect);
    virtual void drawFocusRing(const Vector<IntRect>& rects, int /* width */,
                       int /* offset */, const Color& color);
    virtual void drawHighlightForText(const Font& font, const TextRun& run,
                              const FloatPoint& point, int h,
                              const Color& backgroundColor, ColorSpace colorSpace,
                              int from, int to, bool isActive);
    virtual void drawLine(const IntPoint& point1, const IntPoint& point2);
    virtual void drawLineForText(const FloatPoint& pt, float width);
    virtual void drawLineForTextChecking(const FloatPoint& pt, float width,
                                         GraphicsContext::TextCheckingLineStyle);
    virtual void drawRect(const IntRect& rect);
    virtual void fillPath(const Path& pathToFill, WindRule fillRule);
    virtual void fillRect(const FloatRect& rect);
    virtual void fillRect(const FloatRect& rect, const Color& color);
    virtual void fillRoundedRect(const IntRect& rect, const IntSize& topLeft,
                         const IntSize& topRight, const IntSize& bottomLeft,
                         const IntSize& bottomRight, const Color& color);
    virtual void strokeArc(const IntRect& r, int startAngle, int angleSpan);
    virtual void strokePath(const Path& pathToStroke);
    virtual void strokeRect(const FloatRect& rect, float lineWidth);
    virtual void drawPosText(const void* text, size_t byteLength,
                             const SkPoint pos[], const SkPaint& paint);
    virtual void drawMediaButton(const IntRect& rect, RenderSkinMediaButton::MediaButton buttonType,
                                 bool translucent = false, bool drawBackground = true,
                                 const IntRect& thumb = IntRect());

    void drawDisplayList(OpenGLDLWrapper* dlWrapper, const FloatRect& srcRect, const FloatRect& dstRect);

    bool beginRecording(int width, int height);
    void endRecording();

    OpenGLDLWrapper* genRecordingResult();
    void reset();
private:




    // shadowsIgnoreTransforms is only true for canvas's ImageBuffer, which will
    // have a GraphicsContext
    virtual bool shadowsIgnoreTransforms() const {
        return m_gc && m_gc->shadowsIgnoreTransforms();
    }

    bool setShader(SkPaint* paint);
    void resetShader();
    void setShadow();
    void resetShadow();

    SkCanvas* mCanvas;
    bool m_deleteCanvas;

    OpenGLDLRWrapper* m_renderer;
    SkMatrix m_totalMatrix;
    IntSize m_size;
};

}
#endif

#endif
/// @}

