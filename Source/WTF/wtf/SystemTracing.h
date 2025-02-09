/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef SystemTracing_h
#define SystemTracing_h

#if defined(__has_include)
#if __has_include(<System/sys/kdebug.h>)
#include <System/sys/kdebug.h>
#define HAVE_KDEBUG_H 1
#endif
#endif

// No namespaces because this file has to be includable from C and Objective-C.

// Reserved component code. Do not change this.
#define WEBKIT_COMPONENT 47

// Trace point codes can be up to 14 bits (0-16383).
// When adding or changing these codes, update Tools/Tracing/SystemTracePoints.plist to match.
enum TracePointCode {
    WTFRange = 0,

    JavaScriptRange = 2500,

    WebCoreRange = 5000,
    StyleRecalcStart,
    StyleRecalcEnd,
    LayoutStart,
    LayoutEnd,
    PaintViewStart,
    PaintViewEnd,
    PaintLayerStart,
    PaintLayerEnd,

    WebKitRange = 10000,
    WebKit2Range = 12000,
};

#ifdef __cplusplus

namespace WTF {

class TraceScope {
public:

#if HAVE(KDEBUG_H)
    TraceScope(TracePointCode entryCode, TracePointCode exitCode)
        : m_exitCode(exitCode)
    {
        kdebug_trace(ARIADNEDBG_CODE(WEBKIT_COMPONENT, entryCode), 0, 0, 0, 0);
    }
#else
    TraceScope(TracePointCode, TracePointCode)
    {
    }
#endif
    
    ~TraceScope()
    {
#if HAVE(KDEBUG_H)
        kdebug_trace(ARIADNEDBG_CODE(WEBKIT_COMPONENT, m_exitCode), 0, 0, 0, 0);
#endif
    }

private:
#if HAVE(KDEBUG_H)
    TracePointCode m_exitCode;
#endif
};

} // namespace WTF

using WTF::TraceScope;

#endif // __cplusplus

#endif // SystemTracing_h

