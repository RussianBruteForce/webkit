/*
 * Copyright (C) 2011 Google, Inc. All rights reserved.
 * Copyright (C) 2013, 2016 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
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

#include "config.h"
#include "ContentSecurityPolicy.h"

#include "DOMStringList.h"
#include "Document.h"
#include "FormData.h"
#include "FormDataList.h"
#include "Frame.h"
#include "InspectorInstrumentation.h"
#include "JSMainThreadExecState.h"
#include "PingLoader.h"
#include "RuntimeEnabledFeatures.h"
#include "SchemeRegistry.h"
#include "ScriptController.h"
#include "ScriptState.h"
#include "SecurityOrigin.h"
#include "SecurityPolicyViolationEvent.h"
#include "TextEncoding.h"
#include "URL.h"
#include <inspector/InspectorValues.h>
#include <inspector/ScriptCallStack.h>
#include <inspector/ScriptCallStackFactory.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/TextPosition.h>

using namespace Inspector;

namespace WebCore {

// Normally WebKit uses "static" for internal linkage, but using "static" for
// these functions causes a compile error because these functions are used as
// template parameters.
namespace {

bool isDirectiveNameCharacter(UChar c)
{
    return isASCIIAlphanumeric(c) || c == '-';
}

bool isDirectiveValueCharacter(UChar c)
{
    return isASCIISpace(c) || (c >= 0x21 && c <= 0x7e); // Whitespace + VCHAR
}

bool isSourceCharacter(UChar c)
{
    return !isASCIISpace(c);
}

bool isPathComponentCharacter(UChar c)
{
    return c != '?' && c != '#';
}

bool isHostCharacter(UChar c)
{
    return isASCIIAlphanumeric(c) || c == '-';
}

bool isSchemeContinuationCharacter(UChar c)
{
    return isASCIIAlphanumeric(c) || c == '+' || c == '-' || c == '.';
}

bool isNotASCIISpace(UChar c)
{
    return !isASCIISpace(c);
}

bool isNotColonOrSlash(UChar c)
{
    return c != ':' && c != '/';
}

bool isMediaTypeCharacter(UChar c)
{
    return !isASCIISpace(c) && c != '/';
}

// CSP 1.0 Directives
static const char connectSrc[] = "connect-src";
static const char defaultSrc[] = "default-src";
static const char fontSrc[] = "font-src";
static const char frameSrc[] = "frame-src";
static const char imgSrc[] = "img-src";
static const char mediaSrc[] = "media-src";
static const char objectSrc[] = "object-src";
static const char reportURI[] = "report-uri";
static const char sandbox[] = "sandbox";
static const char scriptSrc[] = "script-src";
static const char styleSrc[] = "style-src";

// CSP 1.1 Directives
static const char baseURI[] = "base-uri";
static const char formAction[] = "form-action";
static const char pluginTypes[] = "plugin-types";
#if ENABLE(CSP_NEXT)
static const char reflectedXSS[] = "reflected-xss";
#endif

#if ENABLE(CSP_NEXT)

static inline bool isExperimentalDirectiveName(const String& name)
{
    return equalLettersIgnoringASCIICase(name, baseURI)
        || equalLettersIgnoringASCIICase(name, formAction)
        || equalLettersIgnoringASCIICase(name, pluginTypes)
        || equalLettersIgnoringASCIICase(name, reflectedXSS);
}

#else

static inline bool isExperimentalDirectiveName(const String&)
{
    return false;
}

#endif

bool isDirectiveName(const String& name)
{
    return equalLettersIgnoringASCIICase(name, connectSrc)
        || equalLettersIgnoringASCIICase(name, defaultSrc)
        || equalLettersIgnoringASCIICase(name, fontSrc)
        || equalLettersIgnoringASCIICase(name, frameSrc)
        || equalLettersIgnoringASCIICase(name, imgSrc)
        || equalLettersIgnoringASCIICase(name, mediaSrc)
        || equalLettersIgnoringASCIICase(name, objectSrc)
        || equalLettersIgnoringASCIICase(name, reportURI)
        || equalLettersIgnoringASCIICase(name, sandbox)
        || equalLettersIgnoringASCIICase(name, scriptSrc)
        || equalLettersIgnoringASCIICase(name, styleSrc)
        || isExperimentalDirectiveName(name);
}

} // namespace

static bool skipExactly(const UChar*& position, const UChar* end, UChar delimiter)
{
    if (position < end && *position == delimiter) {
        ++position;
        return true;
    }
    return false;
}

template<bool characterPredicate(UChar)>
static bool skipExactly(const UChar*& position, const UChar* end)
{
    if (position < end && characterPredicate(*position)) {
        ++position;
        return true;
    }
    return false;
}

static void skipUntil(const UChar*& position, const UChar* end, UChar delimiter)
{
    while (position < end && *position != delimiter)
        ++position;
}

template<bool characterPredicate(UChar)>
static void skipWhile(const UChar*& position, const UChar* end)
{
    while (position < end && characterPredicate(*position))
        ++position;
}

static bool isSourceListNone(const String& value)
{
    auto characters = StringView(value).upconvertedCharacters();
    const UChar* begin = characters;
    const UChar* end = characters + value.length();
    skipWhile<isASCIISpace>(begin, end);

    const UChar* position = begin;
    skipWhile<isSourceCharacter>(position, end);
    if (!equalLettersIgnoringASCIICase(begin, position - begin, "'none'"))
        return false;

    skipWhile<isASCIISpace>(position, end);
    if (position != end)
        return false;

    return true;
}

class CSPSource {
public:
    CSPSource(ContentSecurityPolicy* policy, const String& scheme, const String& host, int port, const String& path, bool hostHasWildcard, bool portHasWildcard)
        : m_policy(policy)
        , m_scheme(scheme)
        , m_host(host)
        , m_port(port)
        , m_path(path)
        , m_hostHasWildcard(hostHasWildcard)
        , m_portHasWildcard(portHasWildcard)
    {
    }

    bool matches(const URL& url) const
    {
        if (!schemeMatches(url))
            return false;
        if (isSchemeOnly())
            return true;
        return hostMatches(url) && portMatches(url) && pathMatches(url);
    }

private:
    bool schemeMatches(const URL& url) const
    {
        if (m_scheme.isEmpty()) {
            String protectedResourceScheme(m_policy->securityOrigin()->protocol());
#if ENABLE(CSP_NEXT)
            if (equalLettersIgnoringASCIICase(protectedResourceScheme, "http"))
                return url.protocolIsInHTTPFamily();
#endif
            return equalIgnoringASCIICase(url.protocol(), protectedResourceScheme);
        }
        return equalIgnoringASCIICase(url.protocol(), m_scheme);
    }

    bool hostMatches(const URL& url) const
    {
        const String& host = url.host();
        if (equalIgnoringASCIICase(host, m_host))
            return true;
        return m_hostHasWildcard && host.endsWith("." + m_host, false);

    }

    bool pathMatches(const URL& url) const
    {
        if (m_path.isEmpty())
            return true;

        String path = decodeURLEscapeSequences(url.path());

        if (m_path.endsWith("/"))
            return path.startsWith(m_path, false);

        return path == m_path;
    }

    bool portMatches(const URL& url) const
    {
        if (m_portHasWildcard)
            return true;

        int port = url.port();

        if (port == m_port)
            return true;

        if (!port)
            return isDefaultPortForProtocol(m_port, url.protocol());

        if (!m_port)
            return isDefaultPortForProtocol(port, url.protocol());

        return false;
    }

    bool isSchemeOnly() const { return m_host.isEmpty(); }

    ContentSecurityPolicy* m_policy;
    String m_scheme;
    String m_host;
    int m_port;
    String m_path;

    bool m_hostHasWildcard;
    bool m_portHasWildcard;
};

class CSPSourceList {
public:
    CSPSourceList(ContentSecurityPolicy*, const String& directiveName);

    void parse(const String&);
    bool matches(const URL&);
    bool allowInline() const { return m_allowInline; }
    bool allowEval() const { return m_allowEval; }

private:
    void parse(const UChar* begin, const UChar* end);

    bool parseSource(const UChar* begin, const UChar* end, String& scheme, String& host, int& port, String& path, bool& hostHasWildcard, bool& portHasWildcard);
    bool parseScheme(const UChar* begin, const UChar* end, String& scheme);
    bool parseHost(const UChar* begin, const UChar* end, String& host, bool& hostHasWildcard);
    bool parsePort(const UChar* begin, const UChar* end, int& port, bool& portHasWildcard);
    bool parsePath(const UChar* begin, const UChar* end, String& path);

    void addSourceSelf();
    void addSourceStar();
    void addSourceUnsafeInline();
    void addSourceUnsafeEval();

    ContentSecurityPolicy* m_policy;
    Vector<CSPSource> m_list;
    String m_directiveName;
    bool m_allowStar;
    bool m_allowInline;
    bool m_allowEval;
};

CSPSourceList::CSPSourceList(ContentSecurityPolicy* policy, const String& directiveName)
    : m_policy(policy)
    , m_directiveName(directiveName)
    , m_allowStar(false)
    , m_allowInline(false)
    , m_allowEval(false)
{
}

void CSPSourceList::parse(const String& value)
{
    // We represent 'none' as an empty m_list.
    if (isSourceListNone(value))
        return;
    auto characters = StringView(value).upconvertedCharacters();
    parse(characters, characters + value.length());
}

bool CSPSourceList::matches(const URL& url)
{
    if (m_allowStar)
        return true;

    URL effectiveURL = SecurityOrigin::shouldUseInnerURL(url) ? SecurityOrigin::extractInnerURL(url) : url;

    for (auto& entry : m_list) {
        if (entry.matches(effectiveURL))
            return true;
    }

    return false;
}

// source-list       = *WSP [ source *( 1*WSP source ) *WSP ]
//                   / *WSP "'none'" *WSP
//
void CSPSourceList::parse(const UChar* begin, const UChar* end)
{
    const UChar* position = begin;

    while (position < end) {
        skipWhile<isASCIISpace>(position, end);
        if (position == end)
            return;

        const UChar* beginSource = position;
        skipWhile<isSourceCharacter>(position, end);

        String scheme, host, path;
        int port = 0;
        bool hostHasWildcard = false;
        bool portHasWildcard = false;

        if (parseSource(beginSource, position, scheme, host, port, path, hostHasWildcard, portHasWildcard)) {
            // Wildcard hosts and keyword sources ('self', 'unsafe-inline',
            // etc.) aren't stored in m_list, but as attributes on the source
            // list itself.
            if (scheme.isEmpty() && host.isEmpty())
                continue;
            if (isDirectiveName(host))
                m_policy->reportDirectiveAsSourceExpression(m_directiveName, host);
            m_list.append(CSPSource(m_policy, scheme, host, port, path, hostHasWildcard, portHasWildcard));
        } else
            m_policy->reportInvalidSourceExpression(m_directiveName, String(beginSource, position - beginSource));

        ASSERT(position == end || isASCIISpace(*position));
     }
}

// source            = scheme ":"
//                   / ( [ scheme "://" ] host [ port ] [ path ] )
//                   / "'self'"
//
bool CSPSourceList::parseSource(const UChar* begin, const UChar* end, String& scheme, String& host, int& port, String& path, bool& hostHasWildcard, bool& portHasWildcard)
{
    if (begin == end)
        return false;

    if (equalLettersIgnoringASCIICase(begin, end - begin, "'none'"))
        return false;

    if (end - begin == 1 && *begin == '*') {
        addSourceStar();
        return true;
    }

    if (equalLettersIgnoringASCIICase(begin, end - begin, "'self'")) {
        addSourceSelf();
        return true;
    }

    if (equalLettersIgnoringASCIICase(begin, end - begin, "'unsafe-inline'")) {
        addSourceUnsafeInline();
        return true;
    }

    if (equalLettersIgnoringASCIICase(begin, end - begin, "'unsafe-eval'")) {
        addSourceUnsafeEval();
        return true;
    }

    const UChar* position = begin;
    const UChar* beginHost = begin;
    const UChar* beginPath = end;
    const UChar* beginPort = nullptr;

    skipWhile<isNotColonOrSlash>(position, end);

    if (position == end) {
        // host
        //     ^
        return parseHost(beginHost, position, host, hostHasWildcard);
    }

    if (position < end && *position == '/') {
        // host/path || host/ || /
        //     ^            ^    ^
        if (!parseHost(beginHost, position, host, hostHasWildcard)
            || !parsePath(position, end, path)
            || position != end)
            return false;
        return true;
    }

    if (position < end && *position == ':') {
        if (end - position == 1) {
            // scheme:
            //       ^
            return parseScheme(begin, position, scheme);
        }

        if (position[1] == '/') {
            // scheme://host || scheme://
            //       ^                ^
            if (!parseScheme(begin, position, scheme)
                || !skipExactly(position, end, ':')
                || !skipExactly(position, end, '/')
                || !skipExactly(position, end, '/'))
                return false;
            if (position == end)
                return true;
            beginHost = position;
            skipWhile<isNotColonOrSlash>(position, end);
        }

        if (position < end && *position == ':') {
            // host:port || scheme://host:port
            //     ^                     ^
            beginPort = position;
            skipUntil(position, end, '/');
        }
    }

    if (position < end && *position == '/') {
        // scheme://host/path || scheme://host:port/path
        //              ^                          ^
        if (position == beginHost)
            return false;

        beginPath = position;
    }

    if (!parseHost(beginHost, beginPort ? beginPort : beginPath, host, hostHasWildcard))
        return false;

    if (beginPort) {
        if (!parsePort(beginPort, beginPath, port, portHasWildcard))
            return false;
    } else {
        port = 0;
    }

    if (beginPath != end) {
        if (!parsePath(beginPath, end, path))
            return false;
    }

    return true;
}

//                     ; <scheme> production from RFC 3986
// scheme      = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
//
bool CSPSourceList::parseScheme(const UChar* begin, const UChar* end, String& scheme)
{
    ASSERT(begin <= end);
    ASSERT(scheme.isEmpty());

    if (begin == end)
        return false;

    const UChar* position = begin;

    if (!skipExactly<isASCIIAlpha>(position, end))
        return false;

    skipWhile<isSchemeContinuationCharacter>(position, end);

    if (position != end)
        return false;

    scheme = String(begin, end - begin);
    return true;
}

// host              = [ "*." ] 1*host-char *( "." 1*host-char )
//                   / "*"
// host-char         = ALPHA / DIGIT / "-"
//
bool CSPSourceList::parseHost(const UChar* begin, const UChar* end, String& host, bool& hostHasWildcard)
{
    ASSERT(begin <= end);
    ASSERT(host.isEmpty());
    ASSERT(!hostHasWildcard);

    if (begin == end)
        return false;

    const UChar* position = begin;

    if (skipExactly(position, end, '*')) {
        hostHasWildcard = true;

        if (position == end)
            return true;

        if (!skipExactly(position, end, '.'))
            return false;
    }

    const UChar* hostBegin = position;

    while (position < end) {
        if (!skipExactly<isHostCharacter>(position, end))
            return false;

        skipWhile<isHostCharacter>(position, end);

        if (position < end && !skipExactly(position, end, '.'))
            return false;
    }

    ASSERT(position == end);
    host = String(hostBegin, end - hostBegin);
    return true;
}

bool CSPSourceList::parsePath(const UChar* begin, const UChar* end, String& path)
{
    ASSERT(begin <= end);
    ASSERT(path.isEmpty());

    const UChar* position = begin;
    skipWhile<isPathComponentCharacter>(position, end);
    // path/to/file.js?query=string || path/to/file.js#anchor
    //                ^                               ^
    if (position < end)
        m_policy->reportInvalidPathCharacter(m_directiveName, String(begin, end - begin), *position);

    path = decodeURLEscapeSequences(String(begin, position - begin));

    ASSERT(position <= end);
    ASSERT(position == end || (*position == '#' || *position == '?'));
    return true;
}

// port              = ":" ( 1*DIGIT / "*" )
//
bool CSPSourceList::parsePort(const UChar* begin, const UChar* end, int& port, bool& portHasWildcard)
{
    ASSERT(begin <= end);
    ASSERT(!port);
    ASSERT(!portHasWildcard);

    if (!skipExactly(begin, end, ':'))
        ASSERT_NOT_REACHED();

    if (begin == end)
        return false;

    if (end - begin == 1 && *begin == '*') {
        port = 0;
        portHasWildcard = true;
        return true;
    }

    const UChar* position = begin;
    skipWhile<isASCIIDigit>(position, end);

    if (position != end)
        return false;

    bool ok;
    port = charactersToIntStrict(begin, end - begin, &ok);
    return ok;
}

void CSPSourceList::addSourceSelf()
{
    m_list.append(CSPSource(m_policy, m_policy->securityOrigin()->protocol(), m_policy->securityOrigin()->host(), m_policy->securityOrigin()->port(), String(), false, false));
}

void CSPSourceList::addSourceStar()
{
    m_allowStar = true;
}

void CSPSourceList::addSourceUnsafeInline()
{
    m_allowInline = true;
}

void CSPSourceList::addSourceUnsafeEval()
{
    m_allowEval = true;
}

class CSPDirective {
public:
    CSPDirective(const String& name, const String& value, ContentSecurityPolicy* policy)
        : m_name(name)
        , m_text(name + ' ' + value)
        , m_policy(policy)
    {
    }

    const String& text() const { return m_text; }

protected:
    const ContentSecurityPolicy* policy() const { return m_policy; }

private:
    String m_name;
    String m_text;
    ContentSecurityPolicy* m_policy;
};

class MediaListDirective : public CSPDirective {
public:
    MediaListDirective(const String& name, const String& value, ContentSecurityPolicy* policy)
        : CSPDirective(name, value, policy)
    {
        parse(value);
    }

    bool allows(const String& type)
    {
        return m_pluginTypes.contains(type);
    }

private:
    void parse(const String& value)
    {
        auto characters = StringView(value).upconvertedCharacters();
        const UChar* begin = characters;
        const UChar* position = begin;
        const UChar* end = begin + value.length();

        // 'plugin-types ____;' OR 'plugin-types;'
        if (value.isEmpty()) {
            policy()->reportInvalidPluginTypes(value);
            return;
        }

        while (position < end) {
            // _____ OR _____mime1/mime1
            // ^        ^
            skipWhile<isASCIISpace>(position, end);
            if (position == end)
                return;

            // mime1/mime1 mime2/mime2
            // ^
            begin = position;
            if (!skipExactly<isMediaTypeCharacter>(position, end)) {
                skipWhile<isNotASCIISpace>(position, end);
                policy()->reportInvalidPluginTypes(String(begin, position - begin));
                continue;
            }
            skipWhile<isMediaTypeCharacter>(position, end);

            // mime1/mime1 mime2/mime2
            //      ^
            if (!skipExactly(position, end, '/')) {
                skipWhile<isNotASCIISpace>(position, end);
                policy()->reportInvalidPluginTypes(String(begin, position - begin));
                continue;
            }

            // mime1/mime1 mime2/mime2
            //       ^
            if (!skipExactly<isMediaTypeCharacter>(position, end)) {
                skipWhile<isNotASCIISpace>(position, end);
                policy()->reportInvalidPluginTypes(String(begin, position - begin));
                continue;
            }
            skipWhile<isMediaTypeCharacter>(position, end);

            // mime1/mime1 mime2/mime2 OR mime1/mime1  OR mime1/mime1/error
            //            ^                          ^               ^
            if (position < end && isNotASCIISpace(*position)) {
                skipWhile<isNotASCIISpace>(position, end);
                policy()->reportInvalidPluginTypes(String(begin, position - begin));
                continue;
            }
            m_pluginTypes.add(String(begin, position - begin));

            ASSERT(position == end || isASCIISpace(*position));
        }
    }

    HashSet<String> m_pluginTypes;
};

class SourceListDirective : public CSPDirective {
public:
    SourceListDirective(const String& name, const String& value, ContentSecurityPolicy* policy)
        : CSPDirective(name, value, policy)
        , m_sourceList(policy, name)
    {
        m_sourceList.parse(value);
    }

    bool allows(const URL& url)
    {
        return m_sourceList.matches(url.isEmpty() ? policy()->url() : url);
    }

    bool allowInline() const { return m_sourceList.allowInline(); }
    bool allowEval() const { return m_sourceList.allowEval(); }

private:
    CSPSourceList m_sourceList;
};

class CSPDirectiveList {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static std::unique_ptr<CSPDirectiveList> create(ContentSecurityPolicy*, const String&, ContentSecurityPolicy::HeaderType);
    CSPDirectiveList(ContentSecurityPolicy*, ContentSecurityPolicy::HeaderType);

    const String& header() const { return m_header; }
    ContentSecurityPolicy::HeaderType headerType() const { return m_headerType; }

    bool allowJavaScriptURLs(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus) const;
    bool allowInlineEventHandlers(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus) const;
    bool allowInlineScript(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus) const;
    bool allowInlineStyle(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus) const;
    bool allowEval(JSC::ExecState*, ContentSecurityPolicy::ReportingStatus) const;
    bool allowPluginType(const String& type, const String& typeAttribute, const URL&, ContentSecurityPolicy::ReportingStatus) const;

    bool allowScriptFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowObjectFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowChildFrameFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowImageFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowStyleFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowFontFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowMediaFromSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowConnectToSource(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowFormAction(const URL&, ContentSecurityPolicy::ReportingStatus) const;
    bool allowBaseURI(const URL&, ContentSecurityPolicy::ReportingStatus) const;

    void gatherReportURIs(DOMStringList&) const;
    const String& evalDisabledErrorMessage() const { return m_evalDisabledErrorMessage; }
    ContentSecurityPolicy::ReflectedXSSDisposition reflectedXSSDisposition() const { return m_reflectedXSSDisposition; }
    bool isReportOnly() const { return m_reportOnly; }
    const Vector<URL>& reportURIs() const { return m_reportURIs; }

private:
    void parse(const String&);

    bool parseDirective(const UChar* begin, const UChar* end, String& name, String& value);
    void parseReportURI(const String& name, const String& value);
    void parsePluginTypes(const String& name, const String& value);
    void parseReflectedXSS(const String& name, const String& value);
    void addDirective(const String& name, const String& value);
    void applySandboxPolicy(const String& name, const String& sandboxPolicy);

    template <class CSPDirectiveType>
    void setCSPDirective(const String& name, const String& value, std::unique_ptr<CSPDirectiveType>&);

    SourceListDirective* operativeDirective(SourceListDirective*) const;
    void reportViolation(const String& directiveText, const String& effectiveDirective, const String& consoleMessage, const URL& blockedURL = URL(), const String& contextURL = String(), const WTF::OrdinalNumber& contextLine = WTF::OrdinalNumber::beforeFirst(), JSC::ExecState* = nullptr) const;

    bool checkEval(SourceListDirective*) const;
    bool checkInline(SourceListDirective*) const;
    bool checkSource(SourceListDirective*, const URL&) const;
    bool checkMediaType(MediaListDirective*, const String& type, const String& typeAttribute) const;

    void setEvalDisabledErrorMessage(const String& errorMessage) { m_evalDisabledErrorMessage = errorMessage; }

    bool checkEvalAndReportViolation(SourceListDirective*, const String& consoleMessage, const String& contextURL = String(), const WTF::OrdinalNumber& contextLine = WTF::OrdinalNumber::beforeFirst(), JSC::ExecState* = nullptr) const;
    bool checkInlineAndReportViolation(SourceListDirective*, const String& consoleMessage, const String& contextURL, const WTF::OrdinalNumber& contextLine, bool isScript) const;

    bool checkSourceAndReportViolation(SourceListDirective*, const URL&, const String& effectiveDirective) const;
    bool checkMediaTypeAndReportViolation(MediaListDirective*, const String& type, const String& typeAttribute, const String& consoleMessage) const;

    bool denyIfEnforcingPolicy() const { return m_reportOnly; }

    ContentSecurityPolicy* m_policy;

    String m_header;
    ContentSecurityPolicy::HeaderType m_headerType;

    bool m_reportOnly;
    bool m_haveSandboxPolicy;
    ContentSecurityPolicy::ReflectedXSSDisposition m_reflectedXSSDisposition;

    std::unique_ptr<MediaListDirective> m_pluginTypes;
    std::unique_ptr<SourceListDirective> m_baseURI;
    std::unique_ptr<SourceListDirective> m_connectSrc;
    std::unique_ptr<SourceListDirective> m_defaultSrc;
    std::unique_ptr<SourceListDirective> m_fontSrc;
    std::unique_ptr<SourceListDirective> m_formAction;
    std::unique_ptr<SourceListDirective> m_frameSrc;
    std::unique_ptr<SourceListDirective> m_imgSrc;
    std::unique_ptr<SourceListDirective> m_mediaSrc;
    std::unique_ptr<SourceListDirective> m_objectSrc;
    std::unique_ptr<SourceListDirective> m_scriptSrc;
    std::unique_ptr<SourceListDirective> m_styleSrc;

    Vector<URL> m_reportURIs;

    String m_evalDisabledErrorMessage;
};

CSPDirectiveList::CSPDirectiveList(ContentSecurityPolicy* policy, ContentSecurityPolicy::HeaderType type)
    : m_policy(policy)
    , m_headerType(type)
    , m_reportOnly(false)
    , m_haveSandboxPolicy(false)
    , m_reflectedXSSDisposition(ContentSecurityPolicy::ReflectedXSSUnset)
{
    m_reportOnly = (type == ContentSecurityPolicy::Report || type == ContentSecurityPolicy::PrefixedReport);
}

std::unique_ptr<CSPDirectiveList> CSPDirectiveList::create(ContentSecurityPolicy* policy, const String& header, ContentSecurityPolicy::HeaderType type)
{
    auto directives = std::make_unique<CSPDirectiveList>(policy, type);
    directives->parse(header);

    if (!directives->checkEval(directives->operativeDirective(directives->m_scriptSrc.get()))) {
        String message = makeString("Refused to evaluate a string as JavaScript because 'unsafe-eval' is not an allowed source of script in the following Content Security Policy directive: \"", directives->operativeDirective(directives->m_scriptSrc.get())->text(), "\".\n");
        directives->setEvalDisabledErrorMessage(message);
    }

    if (directives->isReportOnly() && directives->reportURIs().isEmpty())
        policy->reportMissingReportURI(header);

    return directives;
}

void CSPDirectiveList::reportViolation(const String& directiveText, const String& effectiveDirective, const String& consoleMessage, const URL& blockedURL, const String& contextURL, const WTF::OrdinalNumber& contextLine, JSC::ExecState* state) const
{
    String message = m_reportOnly ? "[Report Only] " + consoleMessage : consoleMessage;
    m_policy->reportViolation(directiveText, effectiveDirective, message, blockedURL, m_reportURIs, m_header, contextURL, contextLine, state);
}

bool CSPDirectiveList::checkEval(SourceListDirective* directive) const
{
    return !directive || directive->allowEval();
}

bool CSPDirectiveList::checkInline(SourceListDirective* directive) const
{
    return !directive || directive->allowInline();
}

bool CSPDirectiveList::checkSource(SourceListDirective* directive, const URL& url) const
{
    return !directive || directive->allows(url);
}

bool CSPDirectiveList::checkMediaType(MediaListDirective* directive, const String& type, const String& typeAttribute) const
{
    if (!directive)
        return true;
    if (typeAttribute.isEmpty() || typeAttribute.stripWhiteSpace() != type)
        return false;
    return directive->allows(type);
}

SourceListDirective* CSPDirectiveList::operativeDirective(SourceListDirective* directive) const
{
    return directive ? directive : m_defaultSrc.get();
}

bool CSPDirectiveList::checkEvalAndReportViolation(SourceListDirective* directive, const String& consoleMessage, const String& contextURL, const WTF::OrdinalNumber& contextLine, JSC::ExecState* state) const
{
    if (checkEval(directive))
        return true;

    String suffix = String();
    if (directive == m_defaultSrc.get())
        suffix = " Note that 'script-src' was not explicitly set, so 'default-src' is used as a fallback.";

    reportViolation(directive->text(), scriptSrc, consoleMessage + "\"" + directive->text() + "\"." + suffix + "\n", URL(), contextURL, contextLine, state);
    if (!m_reportOnly) {
        m_policy->reportBlockedScriptExecutionToInspector(directive->text());
        return false;
    }
    return true;
}

bool CSPDirectiveList::checkMediaTypeAndReportViolation(MediaListDirective* directive, const String& type, const String& typeAttribute, const String& consoleMessage) const
{
    if (checkMediaType(directive, type, typeAttribute))
        return true;

    String message = makeString(consoleMessage, '\'', directive->text(), "\'.");
    if (typeAttribute.isEmpty())
        message = message + " When enforcing the 'plugin-types' directive, the plugin's media type must be explicitly declared with a 'type' attribute on the containing element (e.g. '<object type=\"[TYPE GOES HERE]\" ...>').";

    reportViolation(directive->text(), pluginTypes, message + "\n", URL());
    return denyIfEnforcingPolicy();
}

bool CSPDirectiveList::checkInlineAndReportViolation(SourceListDirective* directive, const String& consoleMessage, const String& contextURL, const WTF::OrdinalNumber& contextLine, bool isScript) const
{
    if (checkInline(directive))
        return true;

    String suffix = String();
    if (directive == m_defaultSrc.get())
        suffix = makeString(" Note that '", (isScript ? "script" : "style"), "-src' was not explicitly set, so 'default-src' is used as a fallback.");

    reportViolation(directive->text(), isScript ? scriptSrc : styleSrc, consoleMessage + "\"" + directive->text() + "\"." + suffix + "\n", URL(), contextURL, contextLine);

    if (!m_reportOnly) {
        if (isScript)
            m_policy->reportBlockedScriptExecutionToInspector(directive->text());
        return false;
    }
    return true;
}

bool CSPDirectiveList::checkSourceAndReportViolation(SourceListDirective* directive, const URL& url, const String& effectiveDirective) const
{
    if (checkSource(directive, url))
        return true;

    const char* prefix;
    if (baseURI == effectiveDirective)
        prefix = "Refused to set the document's base URI to '";
    else if (connectSrc == effectiveDirective)
        prefix = "Refused to connect to '";
    else if (fontSrc == effectiveDirective)
        prefix = "Refused to load the font '";
    else if (formAction == effectiveDirective)
        prefix = "Refused to send form data to '";
    else if (frameSrc == effectiveDirective)
        prefix = "Refused to load frame '";
    else if (imgSrc == effectiveDirective)
        prefix = "Refused to load the image '";
    else if (mediaSrc == effectiveDirective)
        prefix = "Refused to load media from '";
    else if (objectSrc == effectiveDirective)
        prefix = "Refused to load plugin data from '";
    else if (scriptSrc == effectiveDirective)
        prefix = "Refused to load the script '";
    else if (styleSrc == effectiveDirective)
        prefix = "Refused to load the stylesheet '";
    else
        prefix = "";

    String suffix;
    if (directive == m_defaultSrc.get())
        suffix = " Note that '" + effectiveDirective + "' was not explicitly set, so 'default-src' is used as a fallback.";

    reportViolation(directive->text(), effectiveDirective, makeString(prefix, url.stringCenterEllipsizedToLength(), "' because it violates the following Content Security Policy directive: \"", directive->text(), "\".", suffix, '\n'), url);
    return denyIfEnforcingPolicy();
}

bool CSPDirectiveList::allowJavaScriptURLs(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    static NeverDestroyed<String> consoleMessage(ASCIILiteral("Refused to execute JavaScript URL because it violates the following Content Security Policy directive: "));
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkInlineAndReportViolation(operativeDirective(m_scriptSrc.get()), consoleMessage, contextURL, contextLine, true)
        : (m_reportOnly || checkInline(operativeDirective(m_scriptSrc.get())));
}

bool CSPDirectiveList::allowInlineEventHandlers(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    static NeverDestroyed<String> consoleMessage(ASCIILiteral("Refused to execute inline event handler because it violates the following Content Security Policy directive: "));
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkInlineAndReportViolation(operativeDirective(m_scriptSrc.get()), consoleMessage, contextURL, contextLine, true)
        : (m_reportOnly || checkInline(operativeDirective(m_scriptSrc.get())));
}

bool CSPDirectiveList::allowInlineScript(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    static NeverDestroyed<String> consoleMessage(ASCIILiteral("Refused to execute inline script because it violates the following Content Security Policy directive: "));
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkInlineAndReportViolation(operativeDirective(m_scriptSrc.get()), consoleMessage, contextURL, contextLine, true) :
        (m_reportOnly || checkInline(operativeDirective(m_scriptSrc.get())));
}

bool CSPDirectiveList::allowInlineStyle(const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    static NeverDestroyed<String> consoleMessage(ASCIILiteral("Refused to apply inline style because it violates the following Content Security Policy directive: "));
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkInlineAndReportViolation(operativeDirective(m_styleSrc.get()), consoleMessage, contextURL, contextLine, false) :
        (m_reportOnly || checkInline(operativeDirective(m_styleSrc.get())));
}

bool CSPDirectiveList::allowEval(JSC::ExecState* state, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    static NeverDestroyed<String> consoleMessage(ASCIILiteral("Refused to evaluate script because it violates the following Content Security Policy directive: "));
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkEvalAndReportViolation(operativeDirective(m_scriptSrc.get()), consoleMessage, String(), WTF::OrdinalNumber::beforeFirst(), state) :
        (m_reportOnly || checkEval(operativeDirective(m_scriptSrc.get())));
}

bool CSPDirectiveList::allowPluginType(const String& type, const String& typeAttribute, const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkMediaTypeAndReportViolation(m_pluginTypes.get(), type, typeAttribute, "Refused to load '" + url.stringCenterEllipsizedToLength() + "' (MIME type '" + typeAttribute + "') because it violates the following Content Security Policy Directive: ") :
        (m_reportOnly || checkMediaType(m_pluginTypes.get(), type, typeAttribute));
}

bool CSPDirectiveList::allowScriptFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_scriptSrc.get()), url, scriptSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_scriptSrc.get()), url));
}

bool CSPDirectiveList::allowObjectFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    if (url.isBlankURL())
        return true;
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_objectSrc.get()), url, objectSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_objectSrc.get()), url));
}

bool CSPDirectiveList::allowChildFrameFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    if (url.isBlankURL())
        return true;
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_frameSrc.get()), url, frameSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_frameSrc.get()), url));
}

bool CSPDirectiveList::allowImageFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_imgSrc.get()), url, imgSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_imgSrc.get()), url));
}

bool CSPDirectiveList::allowStyleFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_styleSrc.get()), url, styleSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_styleSrc.get()), url));
}

bool CSPDirectiveList::allowFontFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_fontSrc.get()), url, fontSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_fontSrc.get()), url));
}

bool CSPDirectiveList::allowMediaFromSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_mediaSrc.get()), url, mediaSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_mediaSrc.get()), url));
}

bool CSPDirectiveList::allowConnectToSource(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(operativeDirective(m_connectSrc.get()), url, connectSrc) :
        (m_reportOnly || checkSource(operativeDirective(m_connectSrc.get()), url));
}

void CSPDirectiveList::gatherReportURIs(DOMStringList& list) const
{
    for (auto& uri : m_reportURIs)
        list.append(uri.string());
}

bool CSPDirectiveList::allowFormAction(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(m_formAction.get(), url, formAction) :
        (m_reportOnly || checkSource(m_formAction.get(), url));
}

bool CSPDirectiveList::allowBaseURI(const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return reportingStatus == ContentSecurityPolicy::ReportingStatus::SendReport ?
        checkSourceAndReportViolation(m_baseURI.get(), url, baseURI) :
        (m_reportOnly || checkSource(m_baseURI.get(), url));
}

// policy            = directive-list
// directive-list    = [ directive *( ";" [ directive ] ) ]
//
void CSPDirectiveList::parse(const String& policy)
{
    m_header = policy;
    if (policy.isEmpty())
        return;

    auto characters = StringView(policy).upconvertedCharacters();
    const UChar* position = characters;
    const UChar* end = position + policy.length();

    while (position < end) {
        const UChar* directiveBegin = position;
        skipUntil(position, end, ';');

        String name, value;
        if (parseDirective(directiveBegin, position, name, value)) {
            ASSERT(!name.isEmpty());
            addDirective(name, value);
        }

        ASSERT(position == end || *position == ';');
        skipExactly(position, end, ';');
    }
}

// directive         = *WSP [ directive-name [ WSP directive-value ] ]
// directive-name    = 1*( ALPHA / DIGIT / "-" )
// directive-value   = *( WSP / <VCHAR except ";"> )
//
bool CSPDirectiveList::parseDirective(const UChar* begin, const UChar* end, String& name, String& value)
{
    ASSERT(name.isEmpty());
    ASSERT(value.isEmpty());

    const UChar* position = begin;
    skipWhile<isASCIISpace>(position, end);

    // Empty directive (e.g. ";;;"). Exit early.
    if (position == end)
        return false;

    const UChar* nameBegin = position;
    skipWhile<isDirectiveNameCharacter>(position, end);

    // The directive-name must be non-empty.
    if (nameBegin == position) {
        skipWhile<isNotASCIISpace>(position, end);
        m_policy->reportUnsupportedDirective(String(nameBegin, position - nameBegin));
        return false;
    }

    name = String(nameBegin, position - nameBegin);

    if (position == end)
        return true;

    if (!skipExactly<isASCIISpace>(position, end)) {
        skipWhile<isNotASCIISpace>(position, end);
        m_policy->reportUnsupportedDirective(String(nameBegin, position - nameBegin));
        return false;
    }

    skipWhile<isASCIISpace>(position, end);

    const UChar* valueBegin = position;
    skipWhile<isDirectiveValueCharacter>(position, end);

    if (position != end) {
        m_policy->reportInvalidDirectiveValueCharacter(name, String(valueBegin, end - valueBegin));
        return false;
    }

    // The directive-value may be empty.
    if (valueBegin == position)
        return true;

    value = String(valueBegin, position - valueBegin);
    return true;
}

void CSPDirectiveList::parseReportURI(const String& name, const String& value)
{
    if (!m_reportURIs.isEmpty()) {
        m_policy->reportDuplicateDirective(name);
        return;
    }

    auto characters = StringView(value).upconvertedCharacters();
    const UChar* position = characters;
    const UChar* end = position + value.length();

    while (position < end) {
        skipWhile<isASCIISpace>(position, end);

        const UChar* urlBegin = position;
        skipWhile<isNotASCIISpace>(position, end);

        if (urlBegin < position) {
            String url = String(urlBegin, position - urlBegin);
            m_reportURIs.append(m_policy->completeURL(url));
        }
    }
}


template<class CSPDirectiveType>
void CSPDirectiveList::setCSPDirective(const String& name, const String& value, std::unique_ptr<CSPDirectiveType>& directive)
{
    if (directive) {
        m_policy->reportDuplicateDirective(name);
        return;
    }
    directive = std::make_unique<CSPDirectiveType>(name, value, m_policy);
}

void CSPDirectiveList::applySandboxPolicy(const String& name, const String& sandboxPolicy)
{
    if (m_haveSandboxPolicy) {
        m_policy->reportDuplicateDirective(name);
        return;
    }
    m_haveSandboxPolicy = true;
    String invalidTokens;
    m_policy->enforceSandboxFlags(SecurityContext::parseSandboxPolicy(sandboxPolicy, invalidTokens));
    if (!invalidTokens.isNull())
        m_policy->reportInvalidSandboxFlags(invalidTokens);
}

void CSPDirectiveList::parseReflectedXSS(const String& name, const String& value)
{
    if (m_reflectedXSSDisposition != ContentSecurityPolicy::ReflectedXSSUnset) {
        m_policy->reportDuplicateDirective(name);
        m_reflectedXSSDisposition = ContentSecurityPolicy::ReflectedXSSInvalid;
        return;
    }

    if (value.isEmpty()) {
        m_reflectedXSSDisposition = ContentSecurityPolicy::ReflectedXSSInvalid;
        m_policy->reportInvalidReflectedXSS(value);
        return;
    }

    auto characters = StringView(value).upconvertedCharacters();
    const UChar* position = characters;
    const UChar* end = position + value.length();

    skipWhile<isASCIISpace>(position, end);
    const UChar* begin = position;
    skipWhile<isNotASCIISpace>(position, end);

    // value1
    //       ^
    if (equalLettersIgnoringASCIICase(begin, position - begin, "allow"))
        m_reflectedXSSDisposition = ContentSecurityPolicy::AllowReflectedXSS;
    else if (equalLettersIgnoringASCIICase(begin, position - begin, "filter"))
        m_reflectedXSSDisposition = ContentSecurityPolicy::FilterReflectedXSS;
    else if (equalLettersIgnoringASCIICase(begin, position - begin, "block"))
        m_reflectedXSSDisposition = ContentSecurityPolicy::BlockReflectedXSS;
    else {
        m_reflectedXSSDisposition = ContentSecurityPolicy::ReflectedXSSInvalid;
        m_policy->reportInvalidReflectedXSS(value);
        return;
    }

    skipWhile<isASCIISpace>(position, end);
    if (position == end && m_reflectedXSSDisposition != ContentSecurityPolicy::ReflectedXSSUnset)
        return;

    // value1 value2
    //        ^
    m_reflectedXSSDisposition = ContentSecurityPolicy::ReflectedXSSInvalid;
    m_policy->reportInvalidReflectedXSS(value);
}

void CSPDirectiveList::addDirective(const String& name, const String& value)
{
    ASSERT(!name.isEmpty());

    if (equalLettersIgnoringASCIICase(name, defaultSrc))
        setCSPDirective<SourceListDirective>(name, value, m_defaultSrc);
    else if (equalLettersIgnoringASCIICase(name, scriptSrc))
        setCSPDirective<SourceListDirective>(name, value, m_scriptSrc);
    else if (equalLettersIgnoringASCIICase(name, objectSrc))
        setCSPDirective<SourceListDirective>(name, value, m_objectSrc);
    else if (equalLettersIgnoringASCIICase(name, frameSrc))
        setCSPDirective<SourceListDirective>(name, value, m_frameSrc);
    else if (equalLettersIgnoringASCIICase(name, imgSrc))
        setCSPDirective<SourceListDirective>(name, value, m_imgSrc);
    else if (equalLettersIgnoringASCIICase(name, styleSrc))
        setCSPDirective<SourceListDirective>(name, value, m_styleSrc);
    else if (equalLettersIgnoringASCIICase(name, fontSrc))
        setCSPDirective<SourceListDirective>(name, value, m_fontSrc);
    else if (equalLettersIgnoringASCIICase(name, mediaSrc))
        setCSPDirective<SourceListDirective>(name, value, m_mediaSrc);
    else if (equalLettersIgnoringASCIICase(name, connectSrc))
        setCSPDirective<SourceListDirective>(name, value, m_connectSrc);
    else if (equalLettersIgnoringASCIICase(name, sandbox))
        applySandboxPolicy(name, value);
    else if (equalLettersIgnoringASCIICase(name, reportURI))
        parseReportURI(name, value);
#if ENABLE(CSP_NEXT)
    else if (m_policy->experimentalFeaturesEnabled()) {
        if (equalLettersIgnoringASCIICase(name, baseURI))
            setCSPDirective<SourceListDirective>(name, value, m_baseURI);
        else if (equalLettersIgnoringASCIICase(name, formAction))
            setCSPDirective<SourceListDirective>(name, value, m_formAction);
        else if (equalLettersIgnoringASCIICase(name, pluginTypes))
            setCSPDirective<MediaListDirective>(name, value, m_pluginTypes);
        else if (equalLettersIgnoringASCIICase(name, reflectedXSS))
            parseReflectedXSS(name, value);
        else
            m_policy->reportUnsupportedDirective(name);
    }
#endif
    else
        m_policy->reportUnsupportedDirective(name);
}

ContentSecurityPolicy::ContentSecurityPolicy(ScriptExecutionContext* scriptExecutionContext)
    : m_scriptExecutionContext(scriptExecutionContext)
    , m_overrideInlineStyleAllowed(false)
{
}

ContentSecurityPolicy::~ContentSecurityPolicy()
{
}

void ContentSecurityPolicy::copyStateFrom(const ContentSecurityPolicy* other) 
{
    ASSERT(m_policies.isEmpty());
    for (auto& policy : other->m_policies)
        didReceiveHeader(policy->header(), policy->headerType());
}

void ContentSecurityPolicy::didReceiveHeader(const String& header, HeaderType type)
{
    // RFC2616, section 4.2 specifies that headers appearing multiple times can
    // be combined with a comma. Walk the header string, and parse each comma
    // separated chunk as a separate header.
    auto characters = StringView(header).upconvertedCharacters();
    const UChar* begin = characters;
    const UChar* position = begin;
    const UChar* end = begin + header.length();
    while (position < end) {
        skipUntil(position, end, ',');

        // header1,header2 OR header1
        //        ^                  ^
        std::unique_ptr<CSPDirectiveList> policy = CSPDirectiveList::create(this, String(begin, position - begin), type);
        if (!policy->allowEval(0, ContentSecurityPolicy::ReportingStatus::SuppressReport))
            m_scriptExecutionContext->disableEval(policy->evalDisabledErrorMessage());

        m_policies.append(policy.release());

        // Skip the comma, and begin the next header from the current position.
        ASSERT(position == end || *position == ',');
        skipExactly(position, end, ',');
        begin = position;
    }
}

void ContentSecurityPolicy::setOverrideAllowInlineStyle(bool value)
{
    m_overrideInlineStyleAllowed = value;
}

const String& ContentSecurityPolicy::deprecatedHeader() const
{
    return m_policies.isEmpty() ? emptyString() : m_policies[0]->header();
}

ContentSecurityPolicy::HeaderType ContentSecurityPolicy::deprecatedHeaderType() const
{
    return m_policies.isEmpty() ? Enforce : m_policies[0]->headerType();
}

template<bool (CSPDirectiveList::*allowed)(ContentSecurityPolicy::ReportingStatus) const>
bool isAllowedByAll(const CSPDirectiveListVector& policies, ContentSecurityPolicy::ReportingStatus reportingStatus)
{
    for (auto& policy : policies) {
        if (!(policy.get()->*allowed)(reportingStatus))
            return false;
    }
    return true;
}

template<bool (CSPDirectiveList::*allowed)(JSC::ExecState* state, ContentSecurityPolicy::ReportingStatus) const>
bool isAllowedByAllWithState(const CSPDirectiveListVector& policies, JSC::ExecState* state, ContentSecurityPolicy::ReportingStatus reportingStatus)
{
    for (auto& policy : policies) {
        if (!(policy.get()->*allowed)(state, reportingStatus))
            return false;
    }
    return true;
}

template<bool (CSPDirectiveList::*allowed)(const String&, const WTF::OrdinalNumber&, ContentSecurityPolicy::ReportingStatus) const>
bool isAllowedByAllWithContext(const CSPDirectiveListVector& policies, const String& contextURL, const WTF::OrdinalNumber& contextLine, ContentSecurityPolicy::ReportingStatus reportingStatus)
{
    for (auto& policy : policies) {
        if (!(policy.get()->*allowed)(contextURL, contextLine, reportingStatus))
            return false;
    }
    return true;
}

template<bool (CSPDirectiveList::*allowFromURL)(const URL&, ContentSecurityPolicy::ReportingStatus) const>
bool isAllowedByAllWithURL(const CSPDirectiveListVector& policies, const URL& url, ContentSecurityPolicy::ReportingStatus reportingStatus)
{
    if (SchemeRegistry::schemeShouldBypassContentSecurityPolicy(url.protocol()))
        return true;

    for (auto& policy : policies) {
        if (!(policy.get()->*allowFromURL)(url, reportingStatus))
            return false;
    }
    return true;
}

bool ContentSecurityPolicy::allowJavaScriptURLs(const String& contextURL, const WTF::OrdinalNumber& contextLine, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithContext<&CSPDirectiveList::allowJavaScriptURLs>(m_policies, contextURL, contextLine, reportingStatus);
}

bool ContentSecurityPolicy::allowInlineEventHandlers(const String& contextURL, const WTF::OrdinalNumber& contextLine, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithContext<&CSPDirectiveList::allowInlineEventHandlers>(m_policies, contextURL, contextLine, reportingStatus);
}

bool ContentSecurityPolicy::allowInlineScript(const String& contextURL, const WTF::OrdinalNumber& contextLine, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithContext<&CSPDirectiveList::allowInlineScript>(m_policies, contextURL, contextLine, reportingStatus);
}

bool ContentSecurityPolicy::allowInlineStyle(const String& contextURL, const WTF::OrdinalNumber& contextLine, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || m_overrideInlineStyleAllowed || isAllowedByAllWithContext<&CSPDirectiveList::allowInlineStyle>(m_policies, contextURL, contextLine, reportingStatus);
}

bool ContentSecurityPolicy::allowEval(JSC::ExecState* state, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithState<&CSPDirectiveList::allowEval>(m_policies, state, reportingStatus);
}

String ContentSecurityPolicy::evalDisabledErrorMessage() const
{
    for (auto& policy : m_policies) {
        if (!policy->allowEval(0, ContentSecurityPolicy::ReportingStatus::SuppressReport))
            return policy->evalDisabledErrorMessage();
    }
    return String();
}

bool ContentSecurityPolicy::allowPluginType(const String& type, const String& typeAttribute, const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    if (overrideContentSecurityPolicy)
        return true;
    for (auto& policy : m_policies) {
        if (!policy->allowPluginType(type, typeAttribute, url, reportingStatus))
            return false;
    }
    return true;
}

bool ContentSecurityPolicy::allowScriptFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowScriptFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowObjectFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowObjectFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowChildFrameFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowChildFrameFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowImageFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowImageFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowStyleFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowStyleFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowFontFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowFontFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowMediaFromSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowMediaFromSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowConnectToSource(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowConnectToSource>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowFormAction(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowFormAction>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::allowBaseURI(const URL& url, bool overrideContentSecurityPolicy, ContentSecurityPolicy::ReportingStatus reportingStatus) const
{
    return overrideContentSecurityPolicy || isAllowedByAllWithURL<&CSPDirectiveList::allowBaseURI>(m_policies, url, reportingStatus);
}

bool ContentSecurityPolicy::isActive() const
{
    return !m_policies.isEmpty();
}

ContentSecurityPolicy::ReflectedXSSDisposition ContentSecurityPolicy::reflectedXSSDisposition() const
{
    ReflectedXSSDisposition disposition = ReflectedXSSUnset;
    for (auto& policy : m_policies) {
        if (policy->reflectedXSSDisposition() > disposition)
            disposition = std::max(disposition, policy->reflectedXSSDisposition());
    }
    return disposition;
}

void ContentSecurityPolicy::gatherReportURIs(DOMStringList& list) const
{
    for (auto& policy : m_policies)
        policy->gatherReportURIs(list);
}

SecurityOrigin* ContentSecurityPolicy::securityOrigin() const
{
    return m_scriptExecutionContext->securityOrigin();
}

const URL& ContentSecurityPolicy::url() const
{
    return m_scriptExecutionContext->url();
}

URL ContentSecurityPolicy::completeURL(const String& url) const
{
    return m_scriptExecutionContext->completeURL(url);
}

void ContentSecurityPolicy::enforceSandboxFlags(SandboxFlags mask) const
{
    m_scriptExecutionContext->enforceSandboxFlags(mask);
}

static String stripURLForUseInReport(Document& document, const URL& url)
{
    if (!url.isValid())
        return String();
    if (!url.isHierarchical() || url.protocolIs("file"))
        return url.protocol();
    return document.securityOrigin()->canRequest(url) ? url.strippedForUseAsReferrer() : SecurityOrigin::create(url).get().toString();
}

#if ENABLE(CSP_NEXT)
static void gatherSecurityPolicyViolationEventData(SecurityPolicyViolationEventInit& init, Document& document, const String& directiveText, const String& effectiveDirective, const URL& blockedURL, const String& header)
{
    init.documentURI = document.url().string();
    init.referrer = document.referrer();
    init.blockedURI = stripURLForUseInReport(document, blockedURL);
    init.violatedDirective = directiveText;
    init.effectiveDirective = effectiveDirective;
    init.originalPolicy = header;
    init.sourceFile = String();
    init.lineNumber = 0;

    RefPtr<ScriptCallStack> stack = createScriptCallStack(JSMainThreadExecState::currentState(), 2);
    const ScriptCallFrame* callFrame = stack->firstNonNativeCallFrame();
    if (callFrame && callFrame->lineNumber()) {
        URL source = URL(URL(), callFrame->sourceURL());
        init.sourceFile = stripURLForUseInReport(document, source);
        init.lineNumber = callFrame->lineNumber();
    }
}
#endif

void ContentSecurityPolicy::reportViolation(const String& directiveText, const String& effectiveDirective, const String& consoleMessage, const URL& blockedURL, const Vector<URL>& reportURIs, const String& header, const String& contextURL, const WTF::OrdinalNumber& contextLine, JSC::ExecState* state) const
{
    logToConsole(consoleMessage, contextURL, contextLine, state);

    // FIXME: Support sending reports from worker.
    if (!is<Document>(*m_scriptExecutionContext))
        return;

    Document& document = downcast<Document>(*m_scriptExecutionContext);
    Frame* frame = document.frame();
    if (!frame)
        return;

#if ENABLE(CSP_NEXT)
    if (experimentalFeaturesEnabled()) {
        // FIXME: This code means that we're gathering information like line numbers twice. Once we can bring this out from behind the flag, we should reuse the data gathered here when generating the JSON report below.
        SecurityPolicyViolationEventInit init;
        gatherSecurityPolicyViolationEventData(init, document, directiveText, effectiveDirective, blockedURL, header);
        document.enqueueDocumentEvent(SecurityPolicyViolationEvent::create(eventNames().securitypolicyviolationEvent, init));
    }
#endif

    if (reportURIs.isEmpty())
        return;

    // We need to be careful here when deciding what information to send to the
    // report-uri. Currently, we send only the current document's URL and the
    // directive that was violated. The document's URL is safe to send because
    // it's the document itself that's requesting that it be sent. You could
    // make an argument that we shouldn't send HTTPS document URLs to HTTP
    // report-uris (for the same reasons that we suppress the Referer in that
    // case), but the Referer is sent implicitly whereas this request is only
    // sent explicitly. As for which directive was violated, that's pretty
    // harmless information.

    RefPtr<InspectorObject> cspReport = InspectorObject::create();
    cspReport->setString(ASCIILiteral("document-uri"), document.url().strippedForUseAsReferrer());
    cspReport->setString(ASCIILiteral("referrer"), document.referrer());
    cspReport->setString(ASCIILiteral("violated-directive"), directiveText);
#if ENABLE(CSP_NEXT)
    if (experimentalFeaturesEnabled())
        cspReport->setString(ASCIILiteral("effective-directive"), effectiveDirective);
#else
    UNUSED_PARAM(effectiveDirective);
#endif
    cspReport->setString(ASCIILiteral("original-policy"), header);
    cspReport->setString(ASCIILiteral("blocked-uri"), stripURLForUseInReport(document, blockedURL));

    RefPtr<ScriptCallStack> stack = createScriptCallStack(JSMainThreadExecState::currentState(), 2);
    const ScriptCallFrame* callFrame = stack->firstNonNativeCallFrame();
    if (callFrame && callFrame->lineNumber()) {
        URL source = URL(URL(), callFrame->sourceURL());
        cspReport->setString(ASCIILiteral("source-file"), stripURLForUseInReport(document, source));
        cspReport->setInteger(ASCIILiteral("line-number"), callFrame->lineNumber());
    }

    RefPtr<InspectorObject> reportObject = InspectorObject::create();
    reportObject->setObject(ASCIILiteral("csp-report"), cspReport.release());

    RefPtr<FormData> report = FormData::create(reportObject->toJSONString().utf8());

    for (const auto& url : reportURIs)
        PingLoader::sendViolationReport(*frame, url, report.copyRef());
}

void ContentSecurityPolicy::reportUnsupportedDirective(const String& name) const
{
    String message;
    if (equalLettersIgnoringASCIICase(name, "allow"))
        message = ASCIILiteral("The 'allow' directive has been replaced with 'default-src'. Please use that directive instead, as 'allow' has no effect.");
    else if (equalLettersIgnoringASCIICase(name, "options"))
        message = ASCIILiteral("The 'options' directive has been replaced with 'unsafe-inline' and 'unsafe-eval' source expressions for the 'script-src' and 'style-src' directives. Please use those directives instead, as 'options' has no effect.");
    else if (equalLettersIgnoringASCIICase(name, "policy-uri"))
        message = ASCIILiteral("The 'policy-uri' directive has been removed from the specification. Please specify a complete policy via the Content-Security-Policy header.");
    else
        message = makeString("Unrecognized Content-Security-Policy directive '", name, "'.\n"); // FIXME: Why does this include a newline?

    logToConsole(message);
}

void ContentSecurityPolicy::reportDirectiveAsSourceExpression(const String& directiveName, const String& sourceExpression) const
{
    logToConsole("The Content Security Policy directive '" + directiveName + "' contains '" + sourceExpression + "' as a source expression. Did you mean '" + directiveName + " ...; " + sourceExpression + "...' (note the semicolon)?");
}

void ContentSecurityPolicy::reportDuplicateDirective(const String& name) const
{
    logToConsole(makeString("Ignoring duplicate Content-Security-Policy directive '", name, "'.\n"));
}

void ContentSecurityPolicy::reportInvalidPluginTypes(const String& pluginType) const
{
    String message;
    if (pluginType.isNull())
        message = "'plugin-types' Content Security Policy directive is empty; all plugins will be blocked.\n";
    else
        message = makeString("Invalid plugin type in 'plugin-types' Content Security Policy directive: '", pluginType, "'.\n");
    logToConsole(message);
}

void ContentSecurityPolicy::reportInvalidSandboxFlags(const String& invalidFlags) const
{
    logToConsole("Error while parsing the 'sandbox' Content Security Policy directive: " + invalidFlags);
}

void ContentSecurityPolicy::reportInvalidReflectedXSS(const String& invalidValue) const
{
    logToConsole("The 'reflected-xss' Content Security Policy directive has the invalid value \"" + invalidValue + "\". Value values are \"allow\", \"filter\", and \"block\".");
}

void ContentSecurityPolicy::reportInvalidDirectiveValueCharacter(const String& directiveName, const String& value) const
{
    String message = makeString("The value for Content Security Policy directive '", directiveName, "' contains an invalid character: '", value, "'. Non-whitespace characters outside ASCII 0x21-0x7E must be percent-encoded, as described in RFC 3986, section 2.1: http://tools.ietf.org/html/rfc3986#section-2.1.");
    logToConsole(message);
}

void ContentSecurityPolicy::reportInvalidPathCharacter(const String& directiveName, const String& value, const char invalidChar) const
{
    ASSERT(invalidChar == '#' || invalidChar == '?');

    String ignoring;
    if (invalidChar == '?')
        ignoring = "The query component, including the '?', will be ignored.";
    else
        ignoring = "The fragment identifier, including the '#', will be ignored.";

    String message = makeString("The source list for Content Security Policy directive '", directiveName, "' contains a source with an invalid path: '", value, "'. ", ignoring);
    logToConsole(message);
}

void ContentSecurityPolicy::reportInvalidSourceExpression(const String& directiveName, const String& source) const
{
    String message = makeString("The source list for Content Security Policy directive '", directiveName, "' contains an invalid source: '", source, "'. It will be ignored.");
    if (equalLettersIgnoringASCIICase(source, "'none'"))
        message = makeString(message, " Note that 'none' has no effect unless it is the only expression in the source list.");
    logToConsole(message);
}

void ContentSecurityPolicy::reportMissingReportURI(const String& policy) const
{
    logToConsole("The Content Security Policy '" + policy + "' was delivered in report-only mode, but does not specify a 'report-uri'; the policy will have no effect. Please either add a 'report-uri' directive, or deliver the policy via the 'Content-Security-Policy' header.");
}

void ContentSecurityPolicy::logToConsole(const String& message, const String& contextURL, const WTF::OrdinalNumber& contextLine, JSC::ExecState* state) const
{
    // FIXME: <http://webkit.org/b/114317> ContentSecurityPolicy::logToConsole should include a column number
    m_scriptExecutionContext->addConsoleMessage(MessageSource::Security, MessageLevel::Error, message, contextURL, contextLine.oneBasedInt(), 0, state);
}

void ContentSecurityPolicy::reportBlockedScriptExecutionToInspector(const String& directiveText) const
{
    InspectorInstrumentation::scriptExecutionBlockedByCSP(m_scriptExecutionContext, directiveText);
}

bool ContentSecurityPolicy::experimentalFeaturesEnabled() const
{
#if ENABLE(CSP_NEXT)
    return RuntimeEnabledFeatures::sharedFeatures().experimentalContentSecurityPolicyFeaturesEnabled();
#else
    return false;
#endif
}

bool ContentSecurityPolicy::shouldBypassMainWorldContentSecurityPolicy(ScriptExecutionContext& context)
{
    if (is<Document>(context)) {
        auto& document = downcast<Document>(context);
        return document.frame() && document.frame()->script().shouldBypassMainWorldContentSecurityPolicy();
    }
    
    return false;
}
    
}
