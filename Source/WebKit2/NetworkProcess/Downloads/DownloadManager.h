/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#ifndef DownloadManager_h
#define DownloadManager_h

#include "DownloadID.h"
#include "PendingDownload.h"
#include "SandboxExtension.h"
#include <WebCore/NotImplemented.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Noncopyable.h>

namespace WebCore {
#if !USE(NETWORK_SESSION)
class ResourceHandle;
#endif
class ResourceRequest;
class ResourceResponse;
class SessionID;
}

namespace IPC {
class Connection;
class DataReference;
}

namespace WebKit {

class AuthenticationManager;
class Download;
class PendingDownload;

class DownloadManager {
    WTF_MAKE_NONCOPYABLE(DownloadManager);

public:
    class Client {
    public:
        virtual ~Client() { }

        virtual void didCreateDownload() = 0;
        virtual void didDestroyDownload() = 0;
        virtual IPC::Connection* downloadProxyConnection() = 0;
        virtual AuthenticationManager& downloadsAuthenticationManager() = 0;
    };

    explicit DownloadManager(Client&);

    void startDownload(WebCore::SessionID, DownloadID, const WebCore::ResourceRequest&);
#if USE(NETWORK_SESSION)
    std::unique_ptr<PendingDownload> dataTaskBecameDownloadTask(DownloadID, std::unique_ptr<Download>&&);
    void continueCanAuthenticateAgainstProtectionSpace(DownloadID, bool canAuthenticate);
    void continueWillSendRequest(DownloadID, const WebCore::ResourceRequest&);
#else
    void convertHandleToDownload(DownloadID, WebCore::ResourceHandle*, const WebCore::ResourceRequest&, const WebCore::ResourceResponse&);
#endif

    void resumeDownload(WebCore::SessionID, DownloadID, const IPC::DataReference& resumeData, const String& path, const SandboxExtension::Handle&);

    void cancelDownload(DownloadID);
    
    Download* download(DownloadID downloadID) { return m_downloads.get(downloadID); }

    void downloadFinished(Download*);
    bool isDownloading() const { return !m_downloads.isEmpty(); }
    uint64_t activeDownloadCount() const { return m_downloads.size(); }

    void didCreateDownload();
    void didDestroyDownload();

    IPC::Connection* downloadProxyConnection();
    AuthenticationManager& downloadsAuthenticationManager();

private:
    Client& m_client;
#if USE(NETWORK_SESSION)
    HashMap<DownloadID, std::unique_ptr<PendingDownload>> m_pendingDownloads;
#endif
    HashMap<DownloadID, std::unique_ptr<Download>> m_downloads;
};

} // namespace WebKit

#endif // DownloadManager_h
