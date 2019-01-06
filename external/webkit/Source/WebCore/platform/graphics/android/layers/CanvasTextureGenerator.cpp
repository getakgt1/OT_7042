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
#include "config.h"

#if ENABLE(MTK_GLCANVAS)
#include "CanvasTextureGenerator.h"

#include "AndroidLog.h"
#include "GLUtils.h"

#include "OpenGLDL.h"
#include "CanvasLayer.h"


// for debug
#define DEBUG_CANVAS 0

#if DEBUG_CANVAS
#include <cutils/log.h>

#define LOG_TAG "CanvasTexGenerator"
#undef XLOG
#define XLOG(...) android_printLog(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD() \
    LOG_METHOD(); \
    android::ScopedTrace __sst(ATRACE_TAG_ALWAYS, __func__);
#else
#undef XLOG
#define XLOG(...)

#define WTRACE_TAG ATRACE_TAG_ALWAYS
#define WTRACE_METHOD()
#endif

namespace WebCore {

CanvasDisplayListOperation::CanvasDisplayListOperation(int layerId, PassRefPtr<CanvasTextureInfo> texInfo
        , OpenGLDLWrapper* wrapper, IntSize& size)
    : m_dlWrapper(wrapper)
    , m_texInfo(texInfo)
    , m_size(size)
    , m_layerId(layerId)
{
    XLOG("[CanvasDisplayListOperation::Cosntructor] this=[%p] texInf=[%p][%d]"
                        , this, m_texInfo.get(), m_texInfo->refCount());
}

CanvasDisplayListOperation::~CanvasDisplayListOperation()
{
    XLOG("[CanvasDisplyaListOperation::Destructor] this=[%p] texInfo=[%p][%d]"
                    , this, m_texInfo.get(), m_texInfo->refCount());
    delete m_dlWrapper;
}

void CanvasDisplayListOperation::run()
{
    // check if need to create layer
    m_texInfo->replayDisplayList(m_dlWrapper);
}

void RemoveLayerTextureOperation::run()
{
    delete m_removeLayer;
}

void RemoveTextureHandleOperation::run()
{
    delete m_handle;
}

CanvasTextureGenerator::CanvasTextureGenerator()
  : Thread(false)
  , m_deferredMode(false)
{
}

CanvasTextureGenerator::~CanvasTextureGenerator()
{
}

void CanvasTextureGenerator::removeOperationsByLayerId(int layerId)
{
    android::Mutex::Autolock lock(mRequestedOperationsLock);
    XLOG("[CTG::removeOpertaionsByLayerId] +++ layerId=[%d] in [%d]"
                    , layerId, mRequestedOperations.size());
    int removedCnt = 0;
    for (int i = 0; i < mRequestedOperations.size();) {
        CanvasOperation* revOp = mRequestedOperations[i];
        if (revOp->identity() == layerId) {
            mRequestedOperations.remove(i);
            delete revOp;
            removedCnt++;
        } else {
            i++;
        }
    }
    XLOG("[CTG::removeOpertaionsByLayerId] --- removed=[%d] in [%d]"
                    , removedCnt, mRequestedOperations.size());
}

void CanvasTextureGenerator::scheduleOperation(CanvasOperation* operation)
{
    bool signal = false;
    {
        android::Mutex::Autolock lock(mRequestedOperationsLock);

        //TODO: need implement a threshold.
        //   If exceed the threshold, must drop frame.
        mRequestedOperations.append(operation);
        m_deferredMode &= (mRequestedOperations.size() > 1);
        // signal if we weren't in deferred mode, or if we can no longer defer
        signal = !m_deferredMode;
        XLOG("[CTG::scheduleOperation] signal=[%d] m_deferredMode=[%d] size=[%d]"
                , signal, m_deferredMode, mRequestedOperations.size());
    }
    if (signal)
        mRequestedOperationsCond.signal();
}


status_t CanvasTextureGenerator::readyToRun()
{
    // initial Hwui context info
    HwuiContextInfo* ctxInfo = HwuiContextInfo::getInstance();
    if (ctxInfo->needInit()) {
        bool ret = ctxInfo->init();

        ctxInfo->makeContextCurrent();

        // UICaches and UIFontRenderer must initialize first.
        SkPaint paint;
        FontPlatformData data;
        data.setupPaint(&paint);
        ctxInfo->fontRenderCheckInit(&paint);
    }

    return NO_ERROR;
}

// Must be called from within a lock!
CanvasOperation* CanvasTextureGenerator::popNext()
{
    CanvasOperation* current = mRequestedOperations.first();
    mRequestedOperations.remove(0);
    return current;
}

bool CanvasTextureGenerator::threadLoop()
{
    // Check if we have any pending operations.
    mRequestedOperationsLock.lock();

    if (!m_deferredMode) {
        // if we aren't currently deferring work, wait for new work to arrive
        while (!mRequestedOperations.size())
            mRequestedOperationsCond.wait(mRequestedOperationsLock);
    } else {
        // if we only have deferred work, wait for better work, or a timeout
        //mRequestedOperationsCond.waitRelative(mRequestedOperationsLock, gCanvasDeferNsecs);
    }

    mRequestedOperationsLock.unlock();

    bool stop = false;
    while (!stop) {
        CanvasOperation* currentOperation = 0;

        mRequestedOperationsLock.lock();
        XLOG("[CTG::threadLoop] tid=[%d] %d operations in the queue", gettid(), mRequestedOperations.size());

        if (mRequestedOperations.size())
            currentOperation = popNext();
        mRequestedOperationsLock.unlock();

        if (currentOperation) {
            currentOperation->run();
        }

        mRequestedOperationsLock.lock();
        if (m_deferredMode && !currentOperation)
            stop = true;
        if (!mRequestedOperations.size()) {
            m_deferredMode = false;
            stop = true;
        }
        mRequestedOperationsLock.unlock();

        if (currentOperation)
            delete currentOperation; // delete outside lock
    }

    return true;
}

} // namespace WebCore

#endif
/// @}
