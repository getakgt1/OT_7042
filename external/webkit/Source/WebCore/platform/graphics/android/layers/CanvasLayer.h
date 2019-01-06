/*
 * Copyright 2012, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CanvasLayer_h
#define CanvasLayer_h

#if USE(ACCELERATED_COMPOSITING)

#include "HTMLCanvasElement.h"
#include "ImageData.h"
#include "LayerAndroid.h"
#include "RenderLayer.h"

#include <wtf/RefPtr.h>

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
#include <wtf/PassRefPtr.h>
#include "OpenGLDL.h"

#include <wtf/ThreadSafeRefCounted.h>
#endif
/// @}

namespace WebCore {

class CanvasTexture;

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
/// CanvasTextureInfo should use smart pointer.
class CanvasTextureInfo : public ThreadSafeRefCounted<CanvasTextureInfo> {
public:

    static CanvasTextureInfo* create(int layerId, bool inGlobalMap = false) {
        return new CanvasTextureInfo(layerId, inGlobalMap);
    }
    virtual ~CanvasTextureInfo();

    // ------ Run on Both thread ------ //
    int uniqueId() { return m_layerId; }

    //------- Run On CanvasTexGenerator Thread -------//
    android::status_t replayDisplayList(OpenGLDLWrapper* wrapper);

    //------- Run On Main Thread -------//
    void requireTexture();
    GLuint getWebkitTexture() { return m_texture; }
    bool hasTexture();
    bool isTextureInited() { return m_textureInited; }
    bool hasNewerFrame();
    unsigned getDrawingSeq() { return m_drawingSeq; }

    bool genDisplayTexture();

private:
    CanvasTextureInfo(int layerId, bool inGlobalMap);
    int m_layerId;
    bool m_inGlobalMap;

    // texture for main thread.
    GLuint m_texture;
    bool m_textureInited;

    OpenGLTextureHandle* m_handle;
    bool m_hasNewerFrame;
    unsigned m_drawingSeq;

    android::Condition m_seqItemCond;
    android::Mutex m_texLock;
};

#define DEFAULT_TEXTINFO_ID -1
// synchronzied pixel reader
class DisplayListPixelReader : public ReadPixelFuncPtr {
public:
    DisplayListPixelReader() {
        m_texInfo = adoptRef(CanvasTextureInfo::create(DEFAULT_TEXTINFO_ID));
        m_readFinished = false;
    }
    virtual ~DisplayListPixelReader() {}
    void start(OpenGLDLWrapper* dlWrapper, IntSize& size);
    void wait() {
        android::Mutex::Autolock lock(m_readLock);
        if (!m_readFinished)
            m_readCond.wait(m_readLock);
    }
    // inherited from ReadPixelFuncPtr
    virtual unsigned char* allocPixels() { return 0;}
    virtual void postRead(void* payload) {}
protected:
    RefPtr<CanvasTextureInfo> m_texInfo;
    android::Mutex m_readLock;
    android::Condition m_readCond;
    bool m_readFinished;
};
#endif
/// @}
///////////////////////////////////////////////////////////////////////
// CanvasLayer
///////////////////////////////////////////////////////////////////////

class CanvasLayer : public LayerAndroid, private CanvasObserver {
public:
    CanvasLayer(RenderLayer* owner, HTMLCanvasElement* canvas);
    CanvasLayer(const CanvasLayer& layer);
    virtual ~CanvasLayer();

    virtual LayerAndroid* copy() const { return new CanvasLayer(*this); }
    virtual SubclassType subclassType() const { return LayerAndroid::CanvasLayer; }
    virtual void clearDirtyRegion();

    virtual bool drawGL(bool layerTilesDisabled);
    virtual void contentDraw(SkCanvas* canvas, PaintStyle style);
    virtual bool needsTexture();
    virtual bool needsIsolatedSurface() { return true; }

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    static PassRefPtr<CanvasTextureInfo> getTextureInfo(int uniqueId);
    static void removeTextureInfo(int uniqueId);
#endif
/// @}

protected:
    virtual InvalidateFlags onSetHwAccelerated(bool hwAccelerated);

private:
    virtual void canvasChanged(HTMLCanvasElement*, const FloatRect& changedRect);
    virtual void canvasResized(HTMLCanvasElement*);
    virtual void canvasDestroyed(HTMLCanvasElement*);
    /// M: for canvas hw acceleration @{
    virtual void canvasInactive(HTMLCanvasElement*);
    /// @}

    void init();
    SkBitmapRef* bitmap() const;
    IntRect visibleContentRect() const;
    IntSize offsetFromRenderer() const;

    HTMLCanvasElement* m_canvas;
    IntRect m_visibleContentRect;
    IntSize m_offsetFromRenderer;
    SkRegion m_dirtyCanvas;
    SkBitmapRef* m_bitmap;
    RefPtr<CanvasTexture> m_texture;

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    HTMLCanvasElement::CanvasType m_type;
    OpenGLDLWrapper* m_displayListWrapper;
    RefPtr<CanvasTextureInfo> m_textureInfo;
    IntSize m_canvasSize;
    int m_srcPtr;
    mutable unsigned m_lastDisplayListSeq;
    friend class CanvasTextureInfo;
#endif
/// @}
};

} // namespace WebCore

#endif // USE(ACCELERATED_COMPOSITING)

#endif // CanvasLayer_h
