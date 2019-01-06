/*
 * Copyright 2010, The Android Open Source Project
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

/// M: for canvas hw acceleration @{
#if ENABLE(MTK_GLCANVAS)

#ifndef CanvasTextureGenerator_h
#define CanvasTextureGenerator_h

#include <utils/threads.h>
#include <wtf/PassRefPtr.h>
#include <wtf/Vector.h>
#include "IntSize.h"

namespace WebCore {

using namespace android;

class TilesManager;
class OpenGLDLWrapper;
class OpenGLTextureLayerWrapper;
class OpenGLTextureHandle;
class CanvasTextureInfo;

class CanvasOperation {
public:
    virtual ~CanvasOperation(){};
    virtual void run() = 0;
    virtual int identity() { return -1; }
};

class CanvasDisplayListOperation : public CanvasOperation {
public:

    explicit CanvasDisplayListOperation(int, PassRefPtr<CanvasTextureInfo>, OpenGLDLWrapper*, IntSize& size);
    virtual ~CanvasDisplayListOperation();
    virtual void run();

    int identity() { return m_layerId; }
private:
    OpenGLDLWrapper* m_dlWrapper;
    RefPtr<CanvasTextureInfo> m_texInfo;

    IntSize m_size;
    /// indicate layer created from
    int m_layerId;
};

class RemoveLayerTextureOperation : public CanvasOperation {
public:
    RemoveLayerTextureOperation(OpenGLTextureLayerWrapper* wrapper) {
        m_removeLayer = wrapper;
    }
    virtual ~RemoveLayerTextureOperation() {};
    virtual void run();
private:
    OpenGLTextureLayerWrapper* m_removeLayer;
};

class RemoveTextureHandleOperation : public CanvasOperation {
public:
    RemoveTextureHandleOperation(OpenGLTextureHandle* handle) {
        m_handle = handle;
    }
    virtual ~RemoveTextureHandleOperation() {}
    virtual void run();
private:
    OpenGLTextureHandle* m_handle;
};

class CanvasTextureGenerator : public Thread {
public:
    CanvasTextureGenerator();
    virtual ~CanvasTextureGenerator();

    virtual status_t readyToRun();

    void removeOperationsByLayerId(int layerId);
    void scheduleOperation(CanvasOperation* operation);

private:
    CanvasOperation* popNext();
    virtual bool threadLoop();
    WTF::Vector<CanvasOperation*> mRequestedOperations;

    android::Mutex mRequestedOperationsLock;
    android::Condition mRequestedOperationsCond;

    bool m_deferredMode;

    // defer painting for one second if best in queue has priority
    // QueuedOperation::gDeferPriorityCutoff or higher
    static const nsecs_t gCanvasDeferNsecs = 1000000000;
};

} // namespace WebCore

#endif // TexturesGenerator_h

#endif
/// @}
