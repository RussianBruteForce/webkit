/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "config.h"
#include "DOMWindowIndexedDatabase.h"

#if ENABLE(INDEXED_DATABASE)

#include "DOMWindow.h"
#include "DatabaseProvider.h"
#include "Document.h"
#include "IDBFactoryImpl.h"
#include "LegacyFactory.h"
#include "Page.h"
#include "SecurityOrigin.h"

namespace WebCore {

DOMWindowIndexedDatabase::DOMWindowIndexedDatabase(DOMWindow* window)
    : DOMWindowProperty(window->frame())
    , m_window(window)
{
}

DOMWindowIndexedDatabase::~DOMWindowIndexedDatabase()
{
}

const char* DOMWindowIndexedDatabase::supplementName()
{
    return "DOMWindowIndexedDatabase";
}

DOMWindowIndexedDatabase* DOMWindowIndexedDatabase::from(DOMWindow* window)
{
    DOMWindowIndexedDatabase* supplement = static_cast<DOMWindowIndexedDatabase*>(Supplement<DOMWindow>::from(window, supplementName()));
    if (!supplement) {
        auto newSupplement = std::make_unique<DOMWindowIndexedDatabase>(window);
        supplement = newSupplement.get();
        provideTo(window, supplementName(), WTFMove(newSupplement));
    }
    return supplement;
}

void DOMWindowIndexedDatabase::disconnectFrameForDocumentSuspension()
{
    m_suspendedIDBFactory = m_idbFactory.release();
    DOMWindowProperty::disconnectFrameForDocumentSuspension();
}

void DOMWindowIndexedDatabase::reconnectFrameFromDocumentSuspension(Frame* frame)
{
    DOMWindowProperty::reconnectFrameFromDocumentSuspension(frame);
    m_idbFactory = m_suspendedIDBFactory.release();
}

void DOMWindowIndexedDatabase::willDestroyGlobalObjectInCachedFrame()
{
    m_suspendedIDBFactory = nullptr;
    DOMWindowProperty::willDestroyGlobalObjectInCachedFrame();
}

void DOMWindowIndexedDatabase::willDestroyGlobalObjectInFrame()
{
    m_idbFactory = nullptr;
    DOMWindowProperty::willDestroyGlobalObjectInFrame();
}

void DOMWindowIndexedDatabase::willDetachGlobalObjectFromFrame()
{
    m_idbFactory = nullptr;
    DOMWindowProperty::willDetachGlobalObjectFromFrame();
}

IDBFactory* DOMWindowIndexedDatabase::indexedDB(DOMWindow* window)
{
    return from(window)->indexedDB();
}

IDBFactory* DOMWindowIndexedDatabase::indexedDB()
{
    Document* document = m_window->document();
    if (!document)
        return nullptr;

    Page* page = document->page();
    if (!page)
        return nullptr;

    if (!m_window->isCurrentlyDisplayedInFrame())
        return nullptr;

    if (!m_idbFactory) {
        if (page->databaseProvider().supportsModernIDB())
            m_idbFactory = IDBClient::IDBFactory::create(page->idbConnection());
        else
            m_idbFactory = LegacyFactory::create(page->databaseProvider().idbFactoryBackend());
    }

    return m_idbFactory.get();
}

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
