/*
 * Copyright (C) 2011, 2013-2015 Apple Inc. All rights reserved.
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

#ifndef DFGJITCompiler_h
#define DFGJITCompiler_h

#if ENABLE(DFG_JIT)

#include "CCallHelpers.h"
#include "CodeBlock.h"
#include "DFGDisassembler.h"
#include "DFGGraph.h"
#include "DFGInlineCacheWrapper.h"
#include "DFGJITCode.h"
#include "DFGOSRExitCompilationInfo.h"
#include "DFGRegisterBank.h"
#include "FPRInfo.h"
#include "GPRInfo.h"
#include "HandlerInfo.h"
#include "JITCode.h"
#include "JITInlineCacheGenerator.h"
#include "LinkBuffer.h"
#include "MacroAssembler.h"
#include "PCToCodeOriginMap.h"
#include "TempRegisterSet.h"

namespace JSC {

class AbstractSamplingCounter;
class CodeBlock;
class VM;

namespace DFG {

class JITCodeGenerator;
class NodeToRegisterMap;
class OSRExitJumpPlaceholder;
class SlowPathGenerator;
class SpeculativeJIT;
class SpeculationRecovery;

struct EntryLocation;
struct OSRExit;

// === CallLinkRecord ===
//
// A record of a call out from JIT code that needs linking to a helper function.
// Every CallLinkRecord contains a reference to the call instruction & the function
// that it needs to be linked to.
struct CallLinkRecord {
    CallLinkRecord(MacroAssembler::Call call, FunctionPtr function)
        : m_call(call)
        , m_function(function)
    {
    }

    MacroAssembler::Call m_call;
    FunctionPtr m_function;
};

struct InRecord {
    InRecord(
        MacroAssembler::PatchableJump jump, MacroAssembler::Label done,
        SlowPathGenerator* slowPathGenerator, StructureStubInfo* stubInfo)
        : m_jump(jump)
        , m_done(done)
        , m_slowPathGenerator(slowPathGenerator)
        , m_stubInfo(stubInfo)
    {
    }
    
    MacroAssembler::PatchableJump m_jump;
    MacroAssembler::Label m_done;
    SlowPathGenerator* m_slowPathGenerator;
    StructureStubInfo* m_stubInfo;
};

// === JITCompiler ===
//
// DFG::JITCompiler is responsible for generating JIT code from the dataflow graph.
// It does so by delegating to the speculative & non-speculative JITs, which
// generate to a MacroAssembler (which the JITCompiler owns through an inheritance
// relationship). The JITCompiler holds references to information required during
// compilation, and also records information used in linking (e.g. a list of all
// call to be linked).
class JITCompiler : public CCallHelpers {
public:
    JITCompiler(Graph& dfg);
    ~JITCompiler();
    
    void compile();
    void compileFunction();
    
    // Accessors for properties.
    Graph& graph() { return m_graph; }
    
    // Methods to set labels for the disassembler.
    void setStartOfCode()
    {
        m_pcToCodeOriginMapBuilder.appendItem(labelIgnoringWatchpoints(), CodeOrigin(0, nullptr));
        if (LIKELY(!m_disassembler))
            return;
        m_disassembler->setStartOfCode(labelIgnoringWatchpoints());
    }
    
    void setForBlockIndex(BlockIndex blockIndex)
    {
        if (LIKELY(!m_disassembler))
            return;
        m_disassembler->setForBlockIndex(blockIndex, labelIgnoringWatchpoints());
    }
    
    void setForNode(Node* node)
    {
        if (LIKELY(!m_disassembler))
            return;
        m_disassembler->setForNode(node, labelIgnoringWatchpoints());
    }
    
    void setEndOfMainPath();
    void setEndOfCode();
    
    CallSiteIndex addCallSite(CodeOrigin codeOrigin)
    {
        return m_jitCode->common.addCodeOrigin(codeOrigin);
    }

    void emitStoreCodeOrigin(CodeOrigin codeOrigin)
    {
        CallSiteIndex callSite = addCallSite(codeOrigin);
        emitStoreCallSiteIndex(callSite);
    }

    void emitStoreCallSiteIndex(CallSiteIndex callSite)
    {
        store32(TrustedImm32(callSite.bits()), tagFor(static_cast<VirtualRegister>(JSStack::ArgumentCount)));
    }

    // Add a call out from JIT code, without an exception check.
    Call appendCall(const FunctionPtr& function)
    {
        Call functionCall = call();
        m_calls.append(CallLinkRecord(functionCall, function));
        return functionCall;
    }
    
    void exceptionCheck();

    void exceptionCheckWithCallFrameRollback()
    {
        m_exceptionChecksWithCallFrameRollback.append(emitExceptionCheck());
    }

    // Add a call out from JIT code, with a fast exception check that tests if the return value is zero.
    void fastExceptionCheck()
    {
        callExceptionFuzz();
        m_exceptionChecks.append(branchTestPtr(Zero, GPRInfo::returnValueGPR));
    }
    
    OSRExitCompilationInfo& appendExitInfo(MacroAssembler::JumpList jumpsToFail = MacroAssembler::JumpList())
    {
        OSRExitCompilationInfo info;
        info.m_failureJumps = jumpsToFail;
        m_exitCompilationInfo.append(info);
        return m_exitCompilationInfo.last();
    }

#if USE(JSVALUE32_64)
    void* addressOfDoubleConstant(Node*);
#endif

    void addGetById(const JITGetByIdGenerator& gen, SlowPathGenerator* slowPath)
    {
        m_getByIds.append(InlineCacheWrapper<JITGetByIdGenerator>(gen, slowPath));
    }
    
    void addPutById(const JITPutByIdGenerator& gen, SlowPathGenerator* slowPath)
    {
        m_putByIds.append(InlineCacheWrapper<JITPutByIdGenerator>(gen, slowPath));
    }

    void addIn(const InRecord& record)
    {
        m_ins.append(record);
    }
    
    unsigned currentJSCallIndex() const
    {
        return m_jsCalls.size();
    }

    void addJSCall(Call fastCall, Call slowCall, DataLabelPtr targetToCheck, CallLinkInfo* info)
    {
        m_jsCalls.append(JSCallRecord(fastCall, slowCall, targetToCheck, info));
    }
    
    void addWeakReference(JSCell* target)
    {
        m_graph.m_plan.weakReferences.addLazily(target);
    }
    
    void addWeakReferences(const StructureSet& structureSet)
    {
        for (unsigned i = structureSet.size(); i--;)
            addWeakReference(structureSet[i]);
    }
    
    template<typename T>
    Jump branchWeakPtr(RelationalCondition cond, T left, JSCell* weakPtr)
    {
        Jump result = branchPtr(cond, left, TrustedImmPtr(weakPtr));
        addWeakReference(weakPtr);
        return result;
    }

    template<typename T>
    Jump branchWeakStructure(RelationalCondition cond, T left, Structure* weakStructure)
    {
#if USE(JSVALUE64)
        Jump result = branch32(cond, left, TrustedImm32(weakStructure->id()));
        addWeakReference(weakStructure);
        return result;
#else
        return branchWeakPtr(cond, left, weakStructure);
#endif
    }

    void noticeOSREntry(BasicBlock&, JITCompiler::Label blockHead, LinkBuffer&);
    
    RefPtr<JITCode> jitCode() { return m_jitCode; }
    
    Vector<Label>& blockHeads() { return m_blockHeads; }

    CallSiteIndex recordCallSiteAndGenerateExceptionHandlingOSRExitIfNeeded(const CodeOrigin&, unsigned eventStreamIndex);

    PCToCodeOriginMapBuilder& pcToCodeOriginMapBuilder() { return m_pcToCodeOriginMapBuilder; }

private:
    friend class OSRExitJumpPlaceholder;
    
    // Internal implementation to compile.
    void compileEntry();
    void compileSetupRegistersForEntry();
    void compileBody();
    void link(LinkBuffer&);
    
    void exitSpeculativeWithOSR(const OSRExit&, SpeculationRecovery*);
    void compileExceptionHandlers();
    void linkOSRExits();
    void disassemble(LinkBuffer&);

    void appendExceptionHandlingOSRExit(ExitKind, unsigned eventStreamIndex, CodeOrigin, HandlerInfo* exceptionHandler, CallSiteIndex, MacroAssembler::JumpList jumpsToFail = MacroAssembler::JumpList());

    // The dataflow graph currently being generated.
    Graph& m_graph;

    std::unique_ptr<Disassembler> m_disassembler;
    
    RefPtr<JITCode> m_jitCode;
    
    // Vector of calls out from JIT code, including exception handler information.
    // Count of the number of CallRecords with exception handlers.
    Vector<CallLinkRecord> m_calls;
    JumpList m_exceptionChecks;
    JumpList m_exceptionChecksWithCallFrameRollback;
    
    Vector<Label> m_blockHeads;

    struct JSCallRecord {
        JSCallRecord(Call fastCall, Call slowCall, DataLabelPtr targetToCheck, CallLinkInfo* info)
            : m_fastCall(fastCall)
            , m_slowCall(slowCall)
            , m_targetToCheck(targetToCheck)
            , m_info(info)
        {
        }
        
        Call m_fastCall;
        Call m_slowCall;
        DataLabelPtr m_targetToCheck;
        CallLinkInfo* m_info;
    };
    
    Vector<InlineCacheWrapper<JITGetByIdGenerator>, 4> m_getByIds;
    Vector<InlineCacheWrapper<JITPutByIdGenerator>, 4> m_putByIds;
    Vector<InRecord, 4> m_ins;
    Vector<JSCallRecord, 4> m_jsCalls;
    SegmentedVector<OSRExitCompilationInfo, 4> m_exitCompilationInfo;
    Vector<Vector<Label>> m_exitSiteLabels;
    
    struct ExceptionHandlingOSRExitInfo {
        OSRExitCompilationInfo& exitInfo;
        HandlerInfo baselineExceptionHandler;
        CallSiteIndex callSiteIndex;
    };
    Vector<ExceptionHandlingOSRExitInfo> m_exceptionHandlerOSRExitCallSites;
    
    Call m_callArityFixup;
    Label m_arityCheck;
    std::unique_ptr<SpeculativeJIT> m_speculative;
    PCToCodeOriginMapBuilder m_pcToCodeOriginMapBuilder;
};

} } // namespace JSC::DFG

#endif
#endif

