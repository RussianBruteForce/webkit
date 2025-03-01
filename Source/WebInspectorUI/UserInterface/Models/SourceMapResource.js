/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

WebInspector.SourceMapResource = class SourceMapResource extends WebInspector.Resource
{
    constructor(url, sourceMap)
    {
        super(url, null);

        console.assert(url);
        console.assert(sourceMap);

        this._sourceMap = sourceMap;

        var inheritedMIMEType = this._sourceMap.originalSourceCode instanceof WebInspector.Resource ? this._sourceMap.originalSourceCode.syntheticMIMEType : null;

        var fileExtension = WebInspector.fileExtensionForURL(url);
        var fileExtensionMIMEType = WebInspector.mimeTypeForFileExtension(fileExtension, true);

        // FIXME: This is a layering violation. It should use a helper function on the
        // Resource base-class to set _mimeType and _type.
        this._mimeType = fileExtensionMIMEType || inheritedMIMEType || "text/javascript";
        this._type = WebInspector.Resource.typeFromMIMEType(this._mimeType);

        // Mark the resource as loaded so it does not show a spinner in the sidebar.
        // We will really load the resource the first time content is requested.
        this.markAsFinished();
    }

    // Public

    get sourceMap()
    {
        return this._sourceMap;
    }

    get sourceMapDisplaySubpath()
    {
        var sourceMappingBasePathURLComponents = this._sourceMap.sourceMappingBasePathURLComponents;
        var resourceURLComponents = this.urlComponents;

        // Different schemes / hosts. Return the host + path of this resource.
        if (resourceURLComponents.scheme !== sourceMappingBasePathURLComponents.scheme || resourceURLComponents.host !== sourceMappingBasePathURLComponents.host)
            return resourceURLComponents.host + (resourceURLComponents.port ? (":" + resourceURLComponents.port) : "") + resourceURLComponents.path;

        // Same host, but not a subpath of the base. This implies a ".." in the relative path.
        if (!resourceURLComponents.path.startsWith(sourceMappingBasePathURLComponents.path))
            return relativePath(resourceURLComponents.path, sourceMappingBasePathURLComponents.path);

        // Same host. Just a subpath of the base.
        return resourceURLComponents.path.substring(sourceMappingBasePathURLComponents.path.length, resourceURLComponents.length);
    }

    requestContentFromBackend(callback)
    {
        // Revert the markAsFinished that was done in the constructor.
        this.revertMarkAsFinished();

        var inlineContent = this._sourceMap.sourceContent(this.url);
        if (inlineContent) {
            // Force inline content to be asynchronous to match the expected load pattern.
            // FIXME: We don't know the MIME-type for inline content. Guess by analyzing the content?
            // Returns a promise.
            return sourceMapResourceLoaded.call(this, {content: inlineContent, mimeType: this.mimeType, statusCode: 200});
        }

        function sourceMapResourceNotAvailable(error, content, mimeType, statusCode)
        {
            this.markAsFailed();
            return Promise.resolve({
                error: WebInspector.UIString("An error occurred trying to load the resource."),
                content,
                mimeType,
                statusCode
            });
        }

        function sourceMapResourceLoadError(error)
        {
            // There was an error calling NetworkAgent.loadResource.
            console.error(error || "There was an unknown error calling NetworkAgent.loadResource.");
            this.markAsFailed();
            return Promise.resolve({error: WebInspector.UIString("An error occurred trying to load the resource.")});
        }

        function sourceMapResourceLoaded(parameters)
        {
            var {error, content, mimeType, statusCode} = parameters;

            var base64encoded = false;

            if (statusCode >= 400 || error)
                return sourceMapResourceNotAvailable(error, content, mimeType, statusCode);

            // FIXME: Add support for picking the best MIME-type. Right now the file extension is the best bet.
            // The constructor set MIME-type based on the file extension and we ignore mimeType here.

            this.markAsFinished();

            return Promise.resolve({
                content,
                mimeType,
                base64encoded,
                statusCode
            });
        }

        // COMPATIBILITY (iOS 7): Network.loadResource did not exist.
        if (!NetworkAgent.loadResource)
            return sourceMapResourceLoadError.call(this);

        var frameIdentifier = null;
        if (this._sourceMap.originalSourceCode instanceof WebInspector.Resource && this._sourceMap.originalSourceCode.parentFrame)
            frameIdentifier = this._sourceMap.originalSourceCode.parentFrame.id;

        if (!frameIdentifier)
            frameIdentifier = WebInspector.frameResourceManager.mainFrame.id;

        return NetworkAgent.loadResource(frameIdentifier, this.url).then(sourceMapResourceLoaded.bind(this)).catch(sourceMapResourceLoadError.bind(this));
    }

    createSourceCodeLocation(lineNumber, columnNumber)
    {
        // SourceCodeLocations are always constructed with raw resources and raw locations. Lookup the raw location.
        var entry = this._sourceMap.findEntryReversed(this.url, lineNumber);
        var rawLineNumber = entry[0];
        var rawColumnNumber = entry[1];

        // If the raw location is an inline script we need to include that offset.
        var originalSourceCode = this._sourceMap.originalSourceCode;
        if (originalSourceCode instanceof WebInspector.Script) {
            if (rawLineNumber === 0)
                rawColumnNumber += originalSourceCode.range.startColumn;
            rawLineNumber += originalSourceCode.range.startLine;
        }

        // Create the SourceCodeLocation and since we already know the the mapped location set it directly.
        var location = originalSourceCode.createSourceCodeLocation(rawLineNumber, rawColumnNumber);
        location._setMappedLocation(this, lineNumber, columnNumber);
        return location;
    }

    createSourceCodeTextRange(textRange)
    {
        // SourceCodeTextRanges are always constructed with raw resources and raw locations.
        // However, we can provide the most accurate mapped locations in construction.
        var startSourceCodeLocation = this.createSourceCodeLocation(textRange.startLine, textRange.startColumn);
        var endSourceCodeLocation = this.createSourceCodeLocation(textRange.endLine, textRange.endColumn);
        return new WebInspector.SourceCodeTextRange(this._sourceMap.originalSourceCode, startSourceCodeLocation, endSourceCodeLocation);
    }
};
