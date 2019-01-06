/*
 * Copyright 2006, The Android Open Source Project
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

#ifndef AnimationDetector_DEFINED
#define AnimationDetector_DEFINED

#include <wtf/CurrentTime.h>

class AnimationDetector : public SkRefCnt {
public:
    AnimationDetector(int threshold = 5) :
        m_animationThreshold(threshold),
        m_startTime(0),
        m_recordTime(0),
        m_counter(0)
    {

    }

    ~AnimationDetector()
    {
    }

    bool isAnimating() const
    {
        if (m_startTime && (m_startTime != m_recordTime)) {
            int fps = (int)m_counter / (m_recordTime - m_startTime);
            if (fps >= m_animationThreshold)
                return true;
        }
        return false;
    }

    void count()
    {
        double current = currentTime();
        if (!m_startTime)
            m_startTime = current;
        m_recordTime = current;
        m_counter ++;
    }
private:
    int m_animationThreshold;
    double m_startTime;
    double m_recordTime;
    int m_counter;
};

#endif
