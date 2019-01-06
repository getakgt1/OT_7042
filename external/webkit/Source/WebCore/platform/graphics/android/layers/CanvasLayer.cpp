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

#define LOG_TAG "CanvasLayer"
#define LOG_NDEBUG 1

#include "config.h"
#include "CanvasLayer.h"

#if USE(ACCELERATED_COMPOSITING)

#include "AndroidLog.h"
#include "CanvasTexture.h"
#include "DrawQuadData.h"
#include "Image.h"
#include "ImageBuffer.h"
#include "RenderLayerCompositor.h"
#include "SkBitmap.h"
#include "SkBitmapRef.h"
#include "SkCanvas.h"
#include "TilesManager.h"

#include "LayerContent.h"

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
#include <stdlib.h>

#include "GLUtils.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>


#include "DrawGlInfo.h"
#include "OpenGLDL.h"
#include "CanvasTextureGenerator.h"
#endif

// for debug
#include "AndroidLog.h"

#define DEBUG_CANVAS 0
#if DEBUG_CANVAS
#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, "Canvas", __VA_ARGS__)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#undef WTRACE_METHOD
#define WTRACE_METHOD() \
    LOG_METHOD(); \
    android::ScopedTrace __sst(ATRACE_TAG_ALWAYS, __func__);
#else
#undef XLOG
#define XLOG(...)

#undef WTRACE_METHOD
#define WTRACE_METHOD()
#endif
/// @}

namespace WebCore {

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)

#define SWITCH_TO_SW_GAP 40*1024*1024

static int s_maxTextureSize = 0;
static HashMap<int, CanvasTextureInfo*> s_textures;
static android::Mutex s_texturesLock;

/********************************************
 * Called by both threads
 ********************************************/

PassRefPtr<CanvasTextureInfo> CanvasLayer::getTextureInfo(int uniqueId)
{
    android::Mutex::Autolock lock(s_texturesLock);
    CanvasTextureInfo* texture = s_textures.get(uniqueId);
    if (texture)
        return texture;
    texture = CanvasTextureInfo::create(uniqueId, true);
    s_textures.add(uniqueId, texture);
    XLOG("[CanvasLayer::getTextureInfo] textureInfo size=[%d] wrapper=[%p][%d]"
                , s_textures.size(), texture, texture->refCount());
    return adoptRef(texture);
}

void CanvasLayer::removeTextureInfo(int uniqueId)
{
    android::Mutex::Autolock lock(s_texturesLock);
    s_textures.remove(uniqueId);
}

////////////////////////////////////////////////////////////////////////////////
// CanvasTextureInfo
////////////////////////////////////////////////////////////////////////////////

CanvasTextureInfo::CanvasTextureInfo(int layerId, bool inGlobalMap)
    : m_layerId(layerId)
    , m_inGlobalMap(inGlobalMap)
    , m_texture(0)
    , m_textureInited(false)
    , m_handle(new OpenGLTextureHandle(layerId))
    , m_hasNewerFrame(false)
    , m_drawingSeq(0)
{
}

CanvasTextureInfo::~CanvasTextureInfo()
{
    CanvasTextureGenerator* generator = TilesManager::instance()->getCanvasTextureGenerator();

    if (m_handle) {
        generator->scheduleOperation(new RemoveTextureHandleOperation(m_handle));
        m_handle = 0;
    }

    if (m_texture)
        GLUtils::deleteTextureDeferred(m_texture);

    if (m_inGlobalMap) {
        // Must remove from s_textures
        CanvasLayer::removeTextureInfo(m_layerId);
    }
}

//------- Run On CanvasTexGenerator Thread -------//

android::status_t CanvasTextureInfo::replayDisplayList(OpenGLDLWrapper* dlWrapper)
{
    WTRACE_METHOD();
    SkRect rect;

    android::status_t result = dlWrapper->replayToLayerTexture(m_handle->getProxy(), rect, false);
    return result;
}

//------- Run On Main Thread -------//
bool CanvasTextureInfo::hasTexture()
{
    return m_texture;
}

void CanvasTextureInfo::requireTexture()
{
    if (!m_texture)
        glGenTextures(1, &m_texture);
}

bool CanvasTextureInfo::hasNewerFrame()
{
    android::Mutex::Autolock lock(m_texLock);
    return m_hasNewerFrame;
}

bool CanvasTextureInfo::genDisplayTexture()
{
    android::Mutex::Autolock lock(m_texLock);

    OpenGLTextureLayerWrapper* layer = m_handle->lockBuffer();
    if (!layer)
        return false;

    double startTime = WTF::currentTimeMS();

    glBindTexture(GL_TEXTURE_2D, getWebkitTexture());

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)layer->getEGLImage());

    GLint error = glGetError();
    if (error != GL_NO_ERROR) {
        XLOG("[CanvasLayer::genDisplayTexture] glEGLImageTargetTexture2DOES() failed error=[%x]", error);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    //m_drawingSeq = m_drawingLayer->getCurSeq();
    m_drawingSeq = layer->getCurSeq();
    m_hasNewerFrame = false;

    if (!m_textureInited)
        m_textureInited = true;

    m_handle->releaseBuffer(layer);

    return true;
}


////////////////////////////////////////////////////////////////////////////////
// DisplayListPixelReader
////////////////////////////////////////////////////////////////////////////////
void DisplayListPixelReader::start(OpenGLDLWrapper* dlWrapper, IntSize& size)
{
    dlWrapper->registerPostReplayFuncPtr(this);
    CanvasTextureGenerator* generator = TilesManager::instance()->getCanvasTextureGenerator();
    generator->scheduleOperation(new CanvasDisplayListOperation(m_texInfo->uniqueId(), m_texInfo.get()
                                        , dlWrapper, size));
}

////////////////////////////////////////////////////////////////////////////////
// CanvasLayer
////////////////////////////////////////////////////////////////////////////////
#endif
/// @}

CanvasLayer::CanvasLayer(RenderLayer* owner, HTMLCanvasElement* canvas)
    : LayerAndroid(owner)
    , m_canvas(canvas)
    , m_dirtyCanvas()
    , m_bitmap(0)
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    , m_type(canvas->getCanvasType())
    , m_displayListWrapper(0)
    , m_canvasSize(canvas->size())
    , m_srcPtr(0)
#endif
/// @}
{
    init();
    m_canvas->addObserver(this);
    // Make sure we initialize in case the canvas has already been laid out
    canvasResized(m_canvas);
}

CanvasLayer::CanvasLayer(const CanvasLayer& layer)
    : LayerAndroid(layer)
    , m_canvas(0)
    , m_bitmap(0)
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    , m_displayListWrapper(0)
    , m_textureInfo(layer.m_textureInfo)
    , m_canvasSize(layer.m_canvasSize)
    , m_lastDisplayListSeq(layer.m_lastDisplayListSeq)
#endif
/// @}
{
/// M: for canvas hw acceleration @{
    bool isRecordCanvas = false;
#if ENABLE(MTK_GLCANVAS)
    // if the performance is too bad, recording speed and replay speed has huge gap, switch back to sw.
    bool switchToSW = false;
    if (layer.m_canvas)
        m_type = layer.m_canvas->getCanvasType();
    else
        m_type = HTMLCanvasElement::DEFAULT;
    isRecordCanvas = (m_type == HTMLCanvasElement::RECORDING);
    XLOG("CanvasLayer(const CanvasLayer& layer) isRecordCanvas = %d", isRecordCanvas);
    if (isRecordCanvas)
        m_srcPtr = reinterpret_cast<int>(&layer);
#endif
    /// @}

    init();

    WTRACE_METHOD();
    if (!layer.m_canvas) {
        // The canvas has already been destroyed - this shouldn't happen
        ALOGW("Creating a CanvasLayer for a destroyed canvas!");
        m_visibleContentRect = IntRect();
        m_offsetFromRenderer = IntSize();
    #if ENABLE(MTK_GLCANVAS)
        if (isRecordCanvas && m_texture)
            m_texture->setHwAccelerated(false);
        else
    #endif
            m_texture->setHwAccelerated(false);
        return;
    }
    // We are making a copy for the UI, sync the interesting bits
    m_visibleContentRect = layer.visibleContentRect();
    m_offsetFromRenderer = layer.offsetFromRenderer();

    /// M: for canvas hw acceleration @{
    bool previousState;
#if ENABLE(MTK_GLCANVAS)
    if (isRecordCanvas)
        previousState = true;
    else
#endif
        previousState = m_texture->hasValidTexture();
    /// @}

    if (!previousState && layer.m_dirtyCanvas.isEmpty()) {
        // We were previously in software and don't have anything new to draw,
        // so stay in software
        m_bitmap = layer.bitmap();
        SkSafeRef(m_bitmap);
    } else {
    /// M: for canvas hw acceleration @{
    #if ENABLE(MTK_GLCANVAS)
        if (isRecordCanvas) {
            m_displayListWrapper = layer.m_canvas->genDisplayListWrapper();
            XLOG("[CanvasLayer::copy] =HW= [%p][%d] src=[%p] texInfo=[%p][%d] displayList=[%p][%d] c=[%p][%d %d] [%f]"
                        , this, gettid(), &layer, m_textureInfo.get(), m_textureInfo->refCount()
                        , m_displayListWrapper
                        , m_displayListWrapper ? m_displayListWrapper->getSeq() : 0
                        , layer.m_canvas
                        , m_canvasSize.width(), m_canvasSize.height(), WTF::currentTimeMS());
            if (m_displayListWrapper) {
                m_lastDisplayListSeq = layer.m_lastDisplayListSeq = m_displayListWrapper->getSeq();
                CanvasTextureGenerator* generator = TilesManager::instance()->getCanvasTextureGenerator();
                generator->scheduleOperation(new CanvasDisplayListOperation(uniqueId(), m_textureInfo.get()
                                            , m_displayListWrapper, m_canvasSize));
                if ((m_lastDisplayListSeq - m_textureInfo->getDrawingSeq()) * m_displayListWrapper->getDisplayListSize() > SWITCH_TO_SW_GAP) {
                    android_printLog(ANDROID_LOG_DEBUG, "CanvasLayer", "m_lastDisplayListSeq=%d current drawing=%d display list size=%d",
                            m_lastDisplayListSeq, m_textureInfo->getDrawingSeq(), m_displayListWrapper->getDisplayListSize());
                    switchToSW = true;
                }
            }
            // Need to do a full inval of the canvas content as we are mode switching
            m_dirtyRegion.op(m_visibleContentRect.x(), m_visibleContentRect.y(),
                    m_visibleContentRect.maxX(), m_visibleContentRect.maxY(), SkRegion::kUnion_Op);
        } else
    #endif
        {
            // Attempt to upload to a surface texture
            if (!m_texture->uploadImageBuffer(layer.m_canvas->buffer())) {
                // Blargh, no surface texture or ImageBuffer - fall back to software
                m_bitmap = layer.bitmap();
                SkSafeRef(m_bitmap);
                // Merge the canvas invals with the layer's invals to repaint the needed
                // tiles.
                SkRegion::Iterator iter(layer.m_dirtyCanvas);
                const IntPoint& offset = m_visibleContentRect.location();
                for (; !iter.done(); iter.next()) {
                    SkIRect diff = iter.rect();
                    diff.fLeft += offset.x();
                    diff.fRight += offset.x();
                    diff.fTop += offset.y();
                    diff.fBottom += offset.y();
                    m_dirtyRegion.op(diff, SkRegion::kUnion_Op);
                }
            }
        #if ENABLE(MTK_GLCANVAS)
            else {
                // animating canvas, convert to recording canvas
                if (layer.m_canvas->getCanvasType() == HTMLCanvasElement::ANIMATING) {
                    layer.m_canvas->startRecording();
                }
            }
        #endif
            if (previousState != m_texture->hasValidTexture()) {
                // Need to do a full inval of the canvas content as we are mode switching
                m_dirtyRegion.op(m_visibleContentRect.x(), m_visibleContentRect.y(),
                        m_visibleContentRect.maxX(), m_visibleContentRect.maxY(), SkRegion::kUnion_Op);
            }
        }

    #if ENABLE(MTK_GLCANVAS)
        // animating canvas, convert to recording canvas
        if (switchToSW) {
            layer.m_canvas->stopRecording(false);
            layer.m_canvas->disableRecordingCanvas();
        }
    #endif
    /// @}
    }
}

CanvasLayer::~CanvasLayer()
{
    if (m_canvas)
        m_canvas->removeObserver(this);
    SkSafeUnref(m_bitmap);

    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING) {
        XLOG("[CanvasLayer::Destructor] src=[%p] this=[%p][%d] texInfo=[%p][%d]"
                , m_srcPtr
                , this, type(), m_textureInfo.get(), m_textureInfo->refCount());
        if (type() == LayerAndroid::WebCoreLayer) {
            CanvasTextureGenerator* ctg = TilesManager::instance()->getCanvasTextureGenerator();
            ctg->removeOperationsByLayerId(uniqueId());
        }
    }
#endif
    /// @}
}

void CanvasLayer::init()
{
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING)
        m_textureInfo = CanvasLayer::getTextureInfo(uniqueId());
    else
#endif
    {
        m_texture = CanvasTexture::getCanvasTexture(this);
    }
/// @}
}

void CanvasLayer::canvasChanged(HTMLCanvasElement*, const FloatRect& changedRect)
{
    /// M: for canvas hw acceleration @{
    bool validTexture = false;
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING)
        validTexture = m_texture && !m_texture->hasValidTexture();
    else
#endif
    {
        validTexture = (m_texture && !m_texture->hasValidTexture());
    }

    if (validTexture) {
    /// @}
        // We only need to track invals if we aren't using a SurfaceTexture.
        // If we drop out of hwa, we will do a full inval anyway
        SkIRect irect = SkIRect::MakeXYWH(changedRect.x(), changedRect.y(),
                                          changedRect.width(), changedRect.height());
        m_dirtyCanvas.op(irect, SkRegion::kUnion_Op);
    }
    owningLayer()->compositor()->scheduleLayerFlush();
}

void CanvasLayer::canvasResized(HTMLCanvasElement*)
{
    const IntSize& size = m_canvas->size();
    m_dirtyCanvas.setRect(0, 0, size.width(), size.height());
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    m_canvasSize = size;
    if (m_type == HTMLCanvasElement::RECORDING)
        return;
#endif
    // If we are smaller than one tile, don't bother using a surface texture
    if (size.width() <= TilesManager::tileWidth()
            && size.height() <= TilesManager::tileHeight())
        m_texture->setSize(IntSize());
    else
        m_texture->setSize(size);
    /// @}
}

void CanvasLayer::canvasDestroyed(HTMLCanvasElement*)
{
    m_canvas = 0;
}

/// M: for canvas hw acceleration @{
void CanvasLayer::canvasInactive(HTMLCanvasElement*)
{
#if ENABLE(MTK_GLCANVAS)
    if (type() == LayerAndroid::WebCoreLayer) {
        CanvasTextureGenerator* ctg = TilesManager::instance()->getCanvasTextureGenerator();
        ctg->removeOperationsByLayerId(uniqueId());
    }
#endif
}
/// @}

void CanvasLayer::clearDirtyRegion()
{
    LayerAndroid::clearDirtyRegion();
    m_dirtyCanvas.setEmpty();
    if (m_canvas)
        m_canvas->clearDirtyRect();
}

SkBitmapRef* CanvasLayer::bitmap() const
{
    if (!m_canvas || !m_canvas->buffer())
        return 0;
    return m_canvas->copiedImage()->nativeImageForCurrentFrame();
}

IntRect CanvasLayer::visibleContentRect() const
{
    if (!m_canvas
            || !m_canvas->renderer()
            || !m_canvas->renderer()->style()
            || !m_canvas->inDocument()
            || m_canvas->renderer()->style()->visibility() != VISIBLE)
        return IntRect();
    return m_canvas->renderBox()->contentBoxRect();
}

IntSize CanvasLayer::offsetFromRenderer() const
{
    /// M: add similiar check as CanvasLayer::visibleContentRect. @{
    if (!m_canvas
            || !m_canvas->renderBox()
            || !m_canvas->renderBox()->layer()
            || !m_canvas->renderBox()->layer()->backing()
            || !m_canvas->renderBox()->layer()->backing()->graphicsLayer())
        return IntSize();
    /// @}
    return m_canvas->renderBox()->layer()->backing()->graphicsLayer()->offsetFromRenderer();
}

bool CanvasLayer::needsTexture()
{
/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING) {
        if (content()
                && !content()->isEmpty()
                && (content()->hasDecorations()))
            return true;
        return false;
    }
#endif
/// @}
    return (m_bitmap && !masksToBounds()) || LayerAndroid::needsTexture();
}

void CanvasLayer::contentDraw(SkCanvas* canvas, PaintStyle style)
{
    WTRACE_METHOD();

    LayerAndroid::contentDraw(canvas, style);
    if (!m_bitmap || masksToBounds())
        return;
    SkBitmap& bitmap = m_bitmap->bitmap();
    SkRect dst = SkRect::MakeXYWH(m_visibleContentRect.x() - m_offsetFromRenderer.width(),
                                  m_visibleContentRect.y() - m_offsetFromRenderer.height(),
                                  m_visibleContentRect.width(), m_visibleContentRect.height());
    canvas->drawBitmapRect(bitmap, 0, dst, 0);
}

bool CanvasLayer::drawGL(bool layerTilesDisabled)
{
    WTRACE_METHOD();

    bool ret = LayerAndroid::drawGL(layerTilesDisabled);
    /// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING) {
        m_textureInfo->requireTexture();

        m_textureInfo->genDisplayTexture();

        GLuint displayTexture = 0;
        if (m_textureInfo->isTextureInited())
            displayTexture = m_textureInfo->getWebkitTexture();

        XLOG("[CanvasLayer::drawGL] =c= this=[%p] textureInfo=[%p] displayTexture=[%d] [%d %d] [%d][%d]"
                        , this, m_textureInfo.get(), displayTexture
                        , m_canvasSize.width(), m_canvasSize.height()
                        , m_textureInfo->getDrawingSeq(), m_lastDisplayListSeq
                        );

        if (displayTexture) {
            SkRect rect = SkRect::MakeXYWH(m_visibleContentRect.x() - m_offsetFromRenderer.width(),
                                           m_visibleContentRect.y() - m_offsetFromRenderer.height(),
                                           m_visibleContentRect.width(), m_visibleContentRect.height());
            TextureQuadData data(displayTexture, GL_TEXTURE_2D,
                                 GL_LINEAR, LayerQuad, &m_drawTransform, &rect);
            data.setInvertUV(true);
            TilesManager::instance()->shader()->drawQuad(&data);
        }
    } else
#endif
    {
        m_texture->requireTexture();
        if (!m_bitmap && m_texture->updateTexImage()) {
            SkRect rect = SkRect::MakeXYWH(m_visibleContentRect.x() - m_offsetFromRenderer.width(),
                                           m_visibleContentRect.y() - m_offsetFromRenderer.height(),
                                           m_visibleContentRect.width(), m_visibleContentRect.height());
            TextureQuadData data(m_texture->texture(), GL_TEXTURE_EXTERNAL_OES,
                                 GL_LINEAR, LayerQuad, &m_drawTransform, &rect);
            TilesManager::instance()->shader()->drawQuad(&data);
        }
    }
/// @}
    return ret;
}

LayerAndroid::InvalidateFlags CanvasLayer::onSetHwAccelerated(bool hwAccelerated)
{
    /// M: for canvas hw acceleration @{
    bool isHwAccelerated = false;
#if ENABLE(MTK_GLCANVAS)
    if (m_type == HTMLCanvasElement::RECORDING)
        isHwAccelerated = (m_texture && m_texture->setHwAccelerated(hwAccelerated));
    else
#endif
        isHwAccelerated = (m_texture && m_texture->setHwAccelerated(hwAccelerated));

    if (isHwAccelerated)
        return LayerAndroid::InvalidateLayers;
    /// @}
    return LayerAndroid::InvalidateNone;
}

} // namespace WebCore

#endif // USE(ACCELERATED_COMPOSITING)
