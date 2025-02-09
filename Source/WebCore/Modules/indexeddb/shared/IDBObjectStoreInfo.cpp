/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "IDBObjectStoreInfo.h"

#if ENABLE(INDEXED_DATABASE)

namespace WebCore {

IDBObjectStoreInfo::IDBObjectStoreInfo()
{
}

IDBObjectStoreInfo::IDBObjectStoreInfo(uint64_t identifier, const String& name, const IDBKeyPath& keyPath, bool autoIncrement)
    : m_identifier(identifier)
    , m_name(name)
    , m_keyPath(keyPath)
    , m_autoIncrement(autoIncrement)
{
}

IDBIndexInfo IDBObjectStoreInfo::createNewIndex(const String& name, const IDBKeyPath& keyPath, bool unique, bool multiEntry)
{
    IDBIndexInfo info(++m_maxIndexID, m_identifier, name, keyPath, unique, multiEntry);
    m_indexMap.set(info.identifier(), info);
    return info;
}

void IDBObjectStoreInfo::addExistingIndex(const IDBIndexInfo& info)
{
    ASSERT(!m_indexMap.contains(info.identifier()));

    if (info.identifier() > m_maxIndexID)
        m_maxIndexID = info.identifier();

    m_indexMap.set(info.identifier(), info);
}

bool IDBObjectStoreInfo::hasIndex(const String& name) const
{
    for (auto& index : m_indexMap.values()) {
        if (index.name() == name)
            return true;
    }

    return false;
}

bool IDBObjectStoreInfo::hasIndex(uint64_t indexIdentifier) const
{
    return m_indexMap.contains(indexIdentifier);
}

IDBIndexInfo* IDBObjectStoreInfo::infoForExistingIndex(const String& name)
{
    for (auto& index : m_indexMap.values()) {
        if (index.name() == name)
            return &index;
    }

    return nullptr;
}

IDBObjectStoreInfo IDBObjectStoreInfo::isolatedCopy() const
{
    IDBObjectStoreInfo result = { m_identifier, m_name.isolatedCopy(), m_keyPath.isolatedCopy(), m_autoIncrement };

    for (auto& iterator : m_indexMap)
        result.m_indexMap.set(iterator.key, iterator.value.isolatedCopy());

    return result;
}

Vector<String> IDBObjectStoreInfo::indexNames() const
{
    Vector<String> names;
    names.reserveCapacity(m_indexMap.size());
    for (auto& index : m_indexMap.values())
        names.uncheckedAppend(index.name());

    return names;
}

void IDBObjectStoreInfo::deleteIndex(const String& indexName)
{
    auto* info = infoForExistingIndex(indexName);
    if (!info)
        return;

    m_indexMap.remove(info->identifier());
}

void IDBObjectStoreInfo::deleteIndex(uint64_t indexIdentifier)
{
    m_indexMap.remove(indexIdentifier);
}

#ifndef NDEBUG
String IDBObjectStoreInfo::loggingString(int indent) const
{
    String indentString;
    for (int i = 0; i < indent; ++i)
        indentString.append(" ");

    String top = makeString(indentString, "Object store: ", m_name, String::format(" (%" PRIu64 ") \n", m_identifier));
    for (auto index : m_indexMap.values())
        top.append(makeString(index.loggingString(indent + 1), "\n"));

    return top; 
}
#endif

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
