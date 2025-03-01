/*
 *  Copyright (C) 1999-2000 Harri Porten (porten@kde.org)
 *  Copyright (C) 2003, 2008 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include "ErrorInstance.h"

#include "CodeBlock.h"
#include "JSScope.h"
#include "JSCInlines.h"
#include "JSGlobalObjectFunctions.h"
#include <wtf/Vector.h>

namespace JSC {

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(ErrorInstance);

const ClassInfo ErrorInstance::s_info = { "Error", &JSNonFinalObject::s_info, 0, CREATE_METHOD_TABLE(ErrorInstance) };

ErrorInstance::ErrorInstance(VM& vm, Structure* structure)
    : JSNonFinalObject(vm, structure)
{
}

static void appendSourceToError(CallFrame* callFrame, ErrorInstance* exception, unsigned bytecodeOffset)
{
    ErrorInstance::SourceAppender appender = exception->sourceAppender();
    exception->clearSourceAppender();
    RuntimeType type = exception->runtimeTypeForCause();
    exception->clearRuntimeTypeForCause();

    if (!callFrame->codeBlock()->hasExpressionInfo())
        return;
    
    int startOffset = 0;
    int endOffset = 0;
    int divotPoint = 0;
    unsigned line = 0;
    unsigned column = 0;
    
    CodeBlock* codeBlock = callFrame->codeBlock();
    codeBlock->expressionRangeForBytecodeOffset(bytecodeOffset, divotPoint, startOffset, endOffset, line, column);
    
    int expressionStart = divotPoint - startOffset;
    int expressionStop = divotPoint + endOffset;

    StringView sourceString = codeBlock->source()->source();
    if (!expressionStop || expressionStart > static_cast<int>(sourceString.length()))
        return;
    
    VM* vm = &callFrame->vm();
    JSValue jsMessage = exception->getDirect(*vm, vm->propertyNames->message);
    if (!jsMessage || !jsMessage.isString())
        return;
    
    String message = asString(jsMessage)->value(callFrame);
    if (expressionStart < expressionStop)
        message = appender(message, codeBlock->source()->getRange(expressionStart, expressionStop).toString(), type, ErrorInstance::FoundExactSource);
    else {
        // No range information, so give a few characters of context.
        int dataLength = sourceString.length();
        int start = expressionStart;
        int stop = expressionStart;
        // Get up to 20 characters of context to the left and right of the divot, clamping to the line.
        // Then strip whitespace.
        while (start > 0 && (expressionStart - start < 20) && sourceString[start - 1] != '\n')
            start--;
        while (start < (expressionStart - 1) && isStrWhiteSpace(sourceString[start]))
            start++;
        while (stop < dataLength && (stop - expressionStart < 20) && sourceString[stop] != '\n')
            stop++;
        while (stop > expressionStart && isStrWhiteSpace(sourceString[stop - 1]))
            stop--;
        message = appender(message, codeBlock->source()->getRange(start, stop).toString(), type, ErrorInstance::FoundApproximateSource);
    }
    exception->putDirect(*vm, vm->propertyNames->message, jsString(vm, message));

}

class FindFirstCallerFrameWithCodeblockFunctor {
public:
    FindFirstCallerFrameWithCodeblockFunctor(CallFrame* startCallFrame)
        : m_startCallFrame(startCallFrame)
        , m_foundCallFrame(nullptr)
        , m_foundStartCallFrame(false)
        , m_index(0)
    { }

    StackVisitor::Status operator()(StackVisitor& visitor)
    {
        if (!m_foundStartCallFrame && (visitor->callFrame() == m_startCallFrame))
            m_foundStartCallFrame = true;

        if (m_foundStartCallFrame) {
            if (visitor->callFrame()->codeBlock()) {
                m_foundCallFrame = visitor->callFrame();
                return StackVisitor::Done;
            }
            m_index++;
        }

        return StackVisitor::Continue;
    }

    CallFrame* foundCallFrame() const { return m_foundCallFrame; }
    unsigned index() const { return m_index; }

private:
    CallFrame* m_startCallFrame;
    CallFrame* m_foundCallFrame;
    bool m_foundStartCallFrame;
    unsigned m_index;
};

void ErrorInstance::finishCreation(ExecState* exec, VM& vm, const String& message, bool useCurrentFrame)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));
    if (!message.isNull())
        putDirect(vm, vm.propertyNames->message, jsString(&vm, message), DontEnum);

    unsigned bytecodeOffset = hasSourceAppender();
    CallFrame* callFrame = nullptr;
    bool hasTrace = addErrorInfoAndGetBytecodeOffset(exec, vm, this, useCurrentFrame, callFrame, bytecodeOffset);

    if (hasTrace && callFrame && hasSourceAppender()) {
        if (callFrame && callFrame->codeBlock()) 
            appendSourceToError(callFrame, this, bytecodeOffset);
    }
}
    
} // namespace JSC
