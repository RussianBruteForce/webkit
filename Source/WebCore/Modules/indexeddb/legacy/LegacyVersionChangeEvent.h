/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LegacyVersionChangeEvent_h
#define LegacyVersionChangeEvent_h

#if ENABLE(INDEXED_DATABASE)

#include "IDBVersionChangeEvent.h"
#include "IndexedDB.h"

namespace WebCore {

class LegacyVersionChangeEvent final : public IDBVersionChangeEvent {
public:
    static Ref<LegacyVersionChangeEvent> create(unsigned long long oldVersion, unsigned long long newVersion, const AtomicString& eventType)
    {
        return adoptRef(*new LegacyVersionChangeEvent(oldVersion, newVersion, eventType));
    }

private:
    LegacyVersionChangeEvent(unsigned long long oldVersion, unsigned long long newVersion, const AtomicString& eventType);

    virtual uint64_t oldVersion() const override { return m_oldVersion; }
    virtual Optional<uint64_t> newVersion() const override;
    virtual EventInterface eventInterface() const override;

    uint64_t m_oldVersion;
    uint64_t m_newVersion;
};

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)

#endif // LegacyVersionChangeEvent_h
