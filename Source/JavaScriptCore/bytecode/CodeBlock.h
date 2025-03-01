/*
 * Copyright (C) 2008-2015 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Cameron Zwarich <cwzwarich@uwaterloo.ca>
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
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef CodeBlock_h
#define CodeBlock_h

#include "ArrayProfile.h"
#include "ByValInfo.h"
#include "BytecodeConventions.h"
#include "BytecodeLivenessAnalysis.h"
#include "CallLinkInfo.h"
#include "CallReturnOffsetToBytecodeOffset.h"
#include "CodeBlockHash.h"
#include "CodeBlockSet.h"
#include "CodeOrigin.h"
#include "CodeType.h"
#include "CompactJITCodeMap.h"
#include "ConcurrentJITLock.h"
#include "DFGCommon.h"
#include "DFGExitProfile.h"
#include "DeferredCompilationCallback.h"
#include "EvalCodeCache.h"
#include "ExecutionCounter.h"
#include "ExpressionRangeInfo.h"
#include "HandlerInfo.h"
#include "Instruction.h"
#include "JITCode.h"
#include "JITWriteBarrier.h"
#include "JSCell.h"
#include "JSGlobalObject.h"
#include "JumpTable.h"
#include "LLIntCallLinkInfo.h"
#include "LazyOperandValueProfile.h"
#include "ObjectAllocationProfile.h"
#include "Options.h"
#include "ProfilerCompilation.h"
#include "ProfilerJettisonReason.h"
#include "PutPropertySlot.h"
#include "RegExpObject.h"
#include "StructureStubInfo.h"
#include "UnconditionalFinalizer.h"
#include "ValueProfile.h"
#include "VirtualRegister.h"
#include "Watchpoint.h"
#include <wtf/Bag.h>
#include <wtf/FastBitVector.h>
#include <wtf/FastMalloc.h>
#include <wtf/RefCountedArray.h>
#include <wtf/RefPtr.h>
#include <wtf/SegmentedVector.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace JSC {

class ExecState;
class LLIntOffsetsExtractor;
class RegisterAtOffsetList;
class TypeLocation;
class JSModuleEnvironment;
class PCToCodeOriginMap;

enum ReoptimizationMode { DontCountReoptimization, CountReoptimization };

class CodeBlock : public JSCell {
    typedef JSCell Base;
    friend class BytecodeLivenessAnalysis;
    friend class JIT;
    friend class LLIntOffsetsExtractor;

    class UnconditionalFinalizer : public JSC::UnconditionalFinalizer { 
        virtual void finalizeUnconditionally() override;
    };

    class WeakReferenceHarvester : public JSC::WeakReferenceHarvester {
        virtual void visitWeakReferences(SlotVisitor&) override;
    };

public:
    enum CopyParsedBlockTag { CopyParsedBlock };

    static const unsigned StructureFlags = Base::StructureFlags | StructureIsImmortal;

    DECLARE_INFO;

protected:
    CodeBlock(VM*, Structure*, CopyParsedBlockTag, CodeBlock& other);
    CodeBlock(VM*, Structure*, ScriptExecutable* ownerExecutable, UnlinkedCodeBlock*, JSScope*, PassRefPtr<SourceProvider>, unsigned sourceOffset, unsigned firstLineColumnOffset);
#if ENABLE(WEBASSEMBLY)
    CodeBlock(VM*, Structure*, WebAssemblyExecutable* ownerExecutable, JSGlobalObject*);
#endif

    void finishCreation(VM&, CopyParsedBlockTag, CodeBlock& other);
    void finishCreation(VM&, ScriptExecutable* ownerExecutable, UnlinkedCodeBlock*, JSScope*);
#if ENABLE(WEBASSEMBLY)
    void finishCreation(VM&, WebAssemblyExecutable* ownerExecutable, JSGlobalObject*);
#endif

    WriteBarrier<JSGlobalObject> m_globalObject;

public:
    JS_EXPORT_PRIVATE ~CodeBlock();

    UnlinkedCodeBlock* unlinkedCodeBlock() const { return m_unlinkedCode.get(); }

    CString inferredName() const;
    CodeBlockHash hash() const;
    bool hasHash() const;
    bool isSafeToComputeHash() const;
    CString hashAsStringIfPossible() const;
    CString sourceCodeForTools() const; // Not quite the actual source we parsed; this will do things like prefix the source for a function with a reified signature.
    CString sourceCodeOnOneLine() const; // As sourceCodeForTools(), but replaces all whitespace runs with a single space.
    void dumpAssumingJITType(PrintStream&, JITCode::JITType) const;
    void dump(PrintStream&) const;

    int numParameters() const { return m_numParameters; }
    void setNumParameters(int newValue);

    int numCalleeLocals() const { return m_numCalleeLocals; }

    int* addressOfNumParameters() { return &m_numParameters; }
    static ptrdiff_t offsetOfNumParameters() { return OBJECT_OFFSETOF(CodeBlock, m_numParameters); }

    CodeBlock* alternative() const { return static_cast<CodeBlock*>(m_alternative.get()); }
    void setAlternative(VM&, CodeBlock*);

    template <typename Functor> void forEachRelatedCodeBlock(Functor&& functor)
    {
        Functor f(std::forward<Functor>(functor));
        Vector<CodeBlock*, 4> codeBlocks;
        codeBlocks.append(this);

        while (!codeBlocks.isEmpty()) {
            CodeBlock* currentCodeBlock = codeBlocks.takeLast();
            f(currentCodeBlock);

            if (CodeBlock* alternative = currentCodeBlock->alternative())
                codeBlocks.append(alternative);
            if (CodeBlock* osrEntryBlock = currentCodeBlock->specialOSREntryBlockOrNull())
                codeBlocks.append(osrEntryBlock);
        }
    }
    
    CodeSpecializationKind specializationKind() const
    {
        return specializationFromIsConstruct(m_isConstructor);
    }

    CodeBlock* alternativeForJettison();    
    JS_EXPORT_PRIVATE CodeBlock* baselineAlternative();
    
    // FIXME: Get rid of this.
    // https://bugs.webkit.org/show_bug.cgi?id=123677
    CodeBlock* baselineVersion();

    static void visitChildren(JSCell*, SlotVisitor&);
    void visitChildren(SlotVisitor&);
    void visitWeakly(SlotVisitor&);
    void clearVisitWeaklyHasBeenCalled();

    void dumpSource();
    void dumpSource(PrintStream&);

    void dumpBytecode();
    void dumpBytecode(PrintStream&);
    void dumpBytecode(
        PrintStream&, unsigned bytecodeOffset,
        const StubInfoMap& = StubInfoMap(), const CallLinkInfoMap& = CallLinkInfoMap());
    void dumpExceptionHandlers(PrintStream&);
    void printStructures(PrintStream&, const Instruction*);
    void printStructure(PrintStream&, const char* name, const Instruction*, int operand);

    bool isStrictMode() const { return m_isStrictMode; }
    ECMAMode ecmaMode() const { return isStrictMode() ? StrictMode : NotStrictMode; }

    inline bool isKnownNotImmediate(int index)
    {
        if (index == m_thisRegister.offset() && !m_isStrictMode)
            return true;

        if (isConstantRegisterIndex(index))
            return getConstant(index).isCell();

        return false;
    }

    ALWAYS_INLINE bool isTemporaryRegisterIndex(int index)
    {
        return index >= m_numVars;
    }

    enum class RequiredHandler {
        CatchHandler,
        AnyHandler
    };
    HandlerInfo* handlerForBytecodeOffset(unsigned bytecodeOffset, RequiredHandler = RequiredHandler::AnyHandler);
    HandlerInfo* handlerForIndex(unsigned, RequiredHandler = RequiredHandler::AnyHandler);
    void removeExceptionHandlerForCallSite(CallSiteIndex);
    unsigned lineNumberForBytecodeOffset(unsigned bytecodeOffset);
    unsigned columnNumberForBytecodeOffset(unsigned bytecodeOffset);
    void expressionRangeForBytecodeOffset(unsigned bytecodeOffset, int& divot,
                                          int& startOffset, int& endOffset, unsigned& line, unsigned& column);

    void getStubInfoMap(const ConcurrentJITLocker&, StubInfoMap& result);
    void getStubInfoMap(StubInfoMap& result);
    
    void getCallLinkInfoMap(const ConcurrentJITLocker&, CallLinkInfoMap& result);
    void getCallLinkInfoMap(CallLinkInfoMap& result);

    void getByValInfoMap(const ConcurrentJITLocker&, ByValInfoMap& result);
    void getByValInfoMap(ByValInfoMap& result);
    
#if ENABLE(JIT)
    StructureStubInfo* addStubInfo(AccessType);
    Bag<StructureStubInfo>::iterator stubInfoBegin() { return m_stubInfos.begin(); }
    Bag<StructureStubInfo>::iterator stubInfoEnd() { return m_stubInfos.end(); }
    
    // O(n) operation. Use getStubInfoMap() unless you really only intend to get one
    // stub info.
    StructureStubInfo* findStubInfo(CodeOrigin);

    ByValInfo* addByValInfo();

    CallLinkInfo* addCallLinkInfo();
    Bag<CallLinkInfo>::iterator callLinkInfosBegin() { return m_callLinkInfos.begin(); }
    Bag<CallLinkInfo>::iterator callLinkInfosEnd() { return m_callLinkInfos.end(); }

    // This is a slow function call used primarily for compiling OSR exits in the case
    // that there had been inlining. Chances are if you want to use this, you're really
    // looking for a CallLinkInfoMap to amortize the cost of calling this.
    CallLinkInfo* getCallLinkInfoForBytecodeIndex(unsigned bytecodeIndex);
#endif // ENABLE(JIT)

    void unlinkIncomingCalls();

#if ENABLE(JIT)
    void linkIncomingCall(ExecState* callerFrame, CallLinkInfo*);
    void linkIncomingPolymorphicCall(ExecState* callerFrame, PolymorphicCallNode*);
#endif // ENABLE(JIT)

    void linkIncomingCall(ExecState* callerFrame, LLIntCallLinkInfo*);

    void setJITCodeMap(std::unique_ptr<CompactJITCodeMap> jitCodeMap)
    {
        m_jitCodeMap = WTFMove(jitCodeMap);
    }
    CompactJITCodeMap* jitCodeMap()
    {
        return m_jitCodeMap.get();
    }

    unsigned bytecodeOffset(Instruction* returnAddress)
    {
        RELEASE_ASSERT(returnAddress >= instructions().begin() && returnAddress < instructions().end());
        return static_cast<Instruction*>(returnAddress) - instructions().begin();
    }

    unsigned numberOfInstructions() const { return m_instructions.size(); }
    RefCountedArray<Instruction>& instructions() { return m_instructions; }
    const RefCountedArray<Instruction>& instructions() const { return m_instructions; }

    size_t predictedMachineCodeSize();

    bool usesOpcode(OpcodeID);

    unsigned instructionCount() const { return m_instructions.size(); }

    // Exactly equivalent to codeBlock->ownerExecutable()->newReplacementCodeBlockFor(codeBlock->specializationKind())
    CodeBlock* newReplacement();
    
    void setJITCode(PassRefPtr<JITCode> code)
    {
        ASSERT(heap()->isDeferred());
        heap()->reportExtraMemoryAllocated(code->size());
        ConcurrentJITLocker locker(m_lock);
        WTF::storeStoreFence(); // This is probably not needed because the lock will also do something similar, but it's good to be paranoid.
        m_jitCode = code;
    }
    PassRefPtr<JITCode> jitCode() { return m_jitCode; }
    static ptrdiff_t jitCodeOffset() { return OBJECT_OFFSETOF(CodeBlock, m_jitCode); }
    JITCode::JITType jitType() const
    {
        JITCode* jitCode = m_jitCode.get();
        WTF::loadLoadFence();
        JITCode::JITType result = JITCode::jitTypeFor(jitCode);
        WTF::loadLoadFence(); // This probably isn't needed. Oh well, paranoia is good.
        return result;
    }

    bool hasBaselineJITProfiling() const
    {
        return jitType() == JITCode::BaselineJIT;
    }
    
#if ENABLE(JIT)
    CodeBlock* replacement();

    DFG::CapabilityLevel computeCapabilityLevel();
    DFG::CapabilityLevel capabilityLevel();
    DFG::CapabilityLevel capabilityLevelState() { return static_cast<DFG::CapabilityLevel>(m_capabilityLevelState); }

    bool hasOptimizedReplacement(JITCode::JITType typeToReplace);
    bool hasOptimizedReplacement(); // the typeToReplace is my JITType
#endif

    void jettison(Profiler::JettisonReason, ReoptimizationMode = DontCountReoptimization, const FireDetail* = nullptr);
    
    ExecutableBase* ownerExecutable() const { return m_ownerExecutable.get(); }
    ScriptExecutable* ownerScriptExecutable() const { return jsCast<ScriptExecutable*>(m_ownerExecutable.get()); }

    void setVM(VM* vm) { m_vm = vm; }
    VM* vm() { return m_vm; }

    void setThisRegister(VirtualRegister thisRegister) { m_thisRegister = thisRegister; }
    VirtualRegister thisRegister() const { return m_thisRegister; }

    bool usesEval() const { return m_unlinkedCode->usesEval(); }

    void setScopeRegister(VirtualRegister scopeRegister)
    {
        ASSERT(scopeRegister.isLocal() || !scopeRegister.isValid());
        m_scopeRegister = scopeRegister;
    }

    VirtualRegister scopeRegister() const
    {
        return m_scopeRegister;
    }
    
    CodeType codeType() const
    {
        return static_cast<CodeType>(m_codeType);
    }

    PutPropertySlot::Context putByIdContext() const
    {
        if (codeType() == EvalCode)
            return PutPropertySlot::PutByIdEval;
        return PutPropertySlot::PutById;
    }

    SourceProvider* source() const { return m_source.get(); }
    unsigned sourceOffset() const { return m_sourceOffset; }
    unsigned firstLineColumnOffset() const { return m_firstLineColumnOffset; }

    size_t numberOfJumpTargets() const { return m_unlinkedCode->numberOfJumpTargets(); }
    unsigned jumpTarget(int index) const { return m_unlinkedCode->jumpTarget(index); }

    String nameForRegister(VirtualRegister);

    unsigned numberOfArgumentValueProfiles()
    {
        ASSERT(m_numParameters >= 0);
        ASSERT(m_argumentValueProfiles.size() == static_cast<unsigned>(m_numParameters));
        return m_argumentValueProfiles.size();
    }
    ValueProfile* valueProfileForArgument(unsigned argumentIndex)
    {
        ValueProfile* result = &m_argumentValueProfiles[argumentIndex];
        ASSERT(result->m_bytecodeOffset == -1);
        return result;
    }

    unsigned numberOfValueProfiles() { return m_valueProfiles.size(); }
    ValueProfile* valueProfile(int index) { return &m_valueProfiles[index]; }
    ValueProfile* valueProfileForBytecodeOffset(int bytecodeOffset);
    SpeculatedType valueProfilePredictionForBytecodeOffset(const ConcurrentJITLocker& locker, int bytecodeOffset)
    {
        return valueProfileForBytecodeOffset(bytecodeOffset)->computeUpdatedPrediction(locker);
    }

    unsigned totalNumberOfValueProfiles()
    {
        return numberOfArgumentValueProfiles() + numberOfValueProfiles();
    }
    ValueProfile* getFromAllValueProfiles(unsigned index)
    {
        if (index < numberOfArgumentValueProfiles())
            return valueProfileForArgument(index);
        return valueProfile(index - numberOfArgumentValueProfiles());
    }

    RareCaseProfile* addRareCaseProfile(int bytecodeOffset)
    {
        m_rareCaseProfiles.append(RareCaseProfile(bytecodeOffset));
        return &m_rareCaseProfiles.last();
    }
    unsigned numberOfRareCaseProfiles() { return m_rareCaseProfiles.size(); }
    RareCaseProfile* rareCaseProfileForBytecodeOffset(int bytecodeOffset);
    unsigned rareCaseProfileCountForBytecodeOffset(int bytecodeOffset);

    bool likelyToTakeSlowCase(int bytecodeOffset)
    {
        if (!hasBaselineJITProfiling())
            return false;
        unsigned value = rareCaseProfileCountForBytecodeOffset(bytecodeOffset);
        return value >= Options::likelyToTakeSlowCaseMinimumCount();
    }

    bool couldTakeSlowCase(int bytecodeOffset)
    {
        if (!hasBaselineJITProfiling())
            return false;
        unsigned value = rareCaseProfileCountForBytecodeOffset(bytecodeOffset);
        return value >= Options::couldTakeSlowCaseMinimumCount();
    }

    ResultProfile* ensureResultProfile(int bytecodeOffset)
    {
        ResultProfile* profile = resultProfileForBytecodeOffset(bytecodeOffset);
        if (!profile) {
            m_resultProfiles.append(ResultProfile(bytecodeOffset));
            profile = &m_resultProfiles.last();
            ASSERT(&m_resultProfiles.last() == &m_resultProfiles[m_resultProfiles.size() - 1]);
            if (!m_bytecodeOffsetToResultProfileIndexMap)
                m_bytecodeOffsetToResultProfileIndexMap = std::make_unique<BytecodeOffsetToResultProfileIndexMap>();
            m_bytecodeOffsetToResultProfileIndexMap->add(bytecodeOffset, m_resultProfiles.size() - 1);
        }
        return profile;
    }
    unsigned numberOfResultProfiles() { return m_resultProfiles.size(); }
    ResultProfile* resultProfileForBytecodeOffset(int bytecodeOffset);

    unsigned specialFastCaseProfileCountForBytecodeOffset(int bytecodeOffset)
    {
        ResultProfile* profile = resultProfileForBytecodeOffset(bytecodeOffset);
        if (!profile)
            return 0;
        return profile->specialFastPathCount();
    }

    bool couldTakeSpecialFastCase(int bytecodeOffset)
    {
        if (!hasBaselineJITProfiling())
            return false;
        unsigned specialFastCaseCount = specialFastCaseProfileCountForBytecodeOffset(bytecodeOffset);
        return specialFastCaseCount >= Options::couldTakeSlowCaseMinimumCount();
    }

    unsigned numberOfArrayProfiles() const { return m_arrayProfiles.size(); }
    const ArrayProfileVector& arrayProfiles() { return m_arrayProfiles; }
    ArrayProfile* addArrayProfile(unsigned bytecodeOffset)
    {
        m_arrayProfiles.append(ArrayProfile(bytecodeOffset));
        return &m_arrayProfiles.last();
    }
    ArrayProfile* getArrayProfile(unsigned bytecodeOffset);
    ArrayProfile* getOrAddArrayProfile(unsigned bytecodeOffset);

    // Exception handling support

    size_t numberOfExceptionHandlers() const { return m_rareData ? m_rareData->m_exceptionHandlers.size() : 0; }
    HandlerInfo& exceptionHandler(int index) { RELEASE_ASSERT(m_rareData); return m_rareData->m_exceptionHandlers[index]; }

    bool hasExpressionInfo() { return m_unlinkedCode->hasExpressionInfo(); }

#if ENABLE(DFG_JIT)
    Vector<CodeOrigin, 0, UnsafeVectorOverflow>& codeOrigins();
    
    // Having code origins implies that there has been some inlining.
    bool hasCodeOrigins()
    {
        return JITCode::isOptimizingJIT(jitType());
    }
        
    bool canGetCodeOrigin(CallSiteIndex index)
    {
        if (!hasCodeOrigins())
            return false;
        return index.bits() < codeOrigins().size();
    }

    CodeOrigin codeOrigin(CallSiteIndex index)
    {
        return codeOrigins()[index.bits()];
    }

    bool addFrequentExitSite(const DFG::FrequentExitSite& site)
    {
        ASSERT(JITCode::isBaselineCode(jitType()));
        ConcurrentJITLocker locker(m_lock);
        return m_exitProfile.add(locker, site);
    }

    bool hasExitSite(const ConcurrentJITLocker& locker, const DFG::FrequentExitSite& site) const
    {
        return m_exitProfile.hasExitSite(locker, site);
    }
    bool hasExitSite(const DFG::FrequentExitSite& site) const
    {
        ConcurrentJITLocker locker(m_lock);
        return hasExitSite(locker, site);
    }

    DFG::ExitProfile& exitProfile() { return m_exitProfile; }

    CompressedLazyOperandValueProfileHolder& lazyOperandValueProfiles()
    {
        return m_lazyOperandValueProfiles;
    }
#endif // ENABLE(DFG_JIT)

    // Constant Pool
#if ENABLE(DFG_JIT)
    size_t numberOfIdentifiers() const { return m_unlinkedCode->numberOfIdentifiers() + numberOfDFGIdentifiers(); }
    size_t numberOfDFGIdentifiers() const;
    const Identifier& identifier(int index) const;
#else
    size_t numberOfIdentifiers() const { return m_unlinkedCode->numberOfIdentifiers(); }
    const Identifier& identifier(int index) const { return m_unlinkedCode->identifier(index); }
#endif

    Vector<WriteBarrier<Unknown>>& constants() { return m_constantRegisters; }
    Vector<SourceCodeRepresentation>& constantsSourceCodeRepresentation() { return m_constantsSourceCodeRepresentation; }
    unsigned addConstant(JSValue v)
    {
        unsigned result = m_constantRegisters.size();
        m_constantRegisters.append(WriteBarrier<Unknown>());
        m_constantRegisters.last().set(m_globalObject->vm(), this, v);
        m_constantsSourceCodeRepresentation.append(SourceCodeRepresentation::Other);
        return result;
    }

    unsigned addConstantLazily()
    {
        unsigned result = m_constantRegisters.size();
        m_constantRegisters.append(WriteBarrier<Unknown>());
        m_constantsSourceCodeRepresentation.append(SourceCodeRepresentation::Other);
        return result;
    }

    WriteBarrier<Unknown>& constantRegister(int index) { return m_constantRegisters[index - FirstConstantRegisterIndex]; }
    ALWAYS_INLINE bool isConstantRegisterIndex(int index) const { return index >= FirstConstantRegisterIndex; }
    ALWAYS_INLINE JSValue getConstant(int index) const { return m_constantRegisters[index - FirstConstantRegisterIndex].get(); }
    ALWAYS_INLINE SourceCodeRepresentation constantSourceCodeRepresentation(int index) const { return m_constantsSourceCodeRepresentation[index - FirstConstantRegisterIndex]; }

    FunctionExecutable* functionDecl(int index) { return m_functionDecls[index].get(); }
    int numberOfFunctionDecls() { return m_functionDecls.size(); }
    FunctionExecutable* functionExpr(int index) { return m_functionExprs[index].get(); }
    
    RegExp* regexp(int index) const { return m_unlinkedCode->regexp(index); }

    unsigned numberOfConstantBuffers() const
    {
        if (!m_rareData)
            return 0;
        return m_rareData->m_constantBuffers.size();
    }
    unsigned addConstantBuffer(const Vector<JSValue>& buffer)
    {
        createRareDataIfNecessary();
        unsigned size = m_rareData->m_constantBuffers.size();
        m_rareData->m_constantBuffers.append(buffer);
        return size;
    }

    Vector<JSValue>& constantBufferAsVector(unsigned index)
    {
        ASSERT(m_rareData);
        return m_rareData->m_constantBuffers[index];
    }
    JSValue* constantBuffer(unsigned index)
    {
        return constantBufferAsVector(index).data();
    }

    Heap* heap() const { return &m_vm->heap; }
    JSGlobalObject* globalObject() { return m_globalObject.get(); }

    JSGlobalObject* globalObjectFor(CodeOrigin);

    BytecodeLivenessAnalysis& livenessAnalysis()
    {
        {
            ConcurrentJITLocker locker(m_lock);
            if (!!m_livenessAnalysis)
                return *m_livenessAnalysis;
        }
        std::unique_ptr<BytecodeLivenessAnalysis> analysis =
            std::make_unique<BytecodeLivenessAnalysis>(this);
        {
            ConcurrentJITLocker locker(m_lock);
            if (!m_livenessAnalysis)
                m_livenessAnalysis = WTFMove(analysis);
            return *m_livenessAnalysis;
        }
    }
    
    void validate();

    // Jump Tables

    size_t numberOfSwitchJumpTables() const { return m_rareData ? m_rareData->m_switchJumpTables.size() : 0; }
    SimpleJumpTable& addSwitchJumpTable() { createRareDataIfNecessary(); m_rareData->m_switchJumpTables.append(SimpleJumpTable()); return m_rareData->m_switchJumpTables.last(); }
    SimpleJumpTable& switchJumpTable(int tableIndex) { RELEASE_ASSERT(m_rareData); return m_rareData->m_switchJumpTables[tableIndex]; }
    void clearSwitchJumpTables()
    {
        if (!m_rareData)
            return;
        m_rareData->m_switchJumpTables.clear();
    }

    size_t numberOfStringSwitchJumpTables() const { return m_rareData ? m_rareData->m_stringSwitchJumpTables.size() : 0; }
    StringJumpTable& addStringSwitchJumpTable() { createRareDataIfNecessary(); m_rareData->m_stringSwitchJumpTables.append(StringJumpTable()); return m_rareData->m_stringSwitchJumpTables.last(); }
    StringJumpTable& stringSwitchJumpTable(int tableIndex) { RELEASE_ASSERT(m_rareData); return m_rareData->m_stringSwitchJumpTables[tableIndex]; }

    // Live callee registers at yield points.
    const FastBitVector& liveCalleeLocalsAtYield(unsigned index) const
    {
        RELEASE_ASSERT(m_rareData);
        return m_rareData->m_liveCalleeLocalsAtYield[index];
    }
    FastBitVector& liveCalleeLocalsAtYield(unsigned index)
    {
        RELEASE_ASSERT(m_rareData);
        return m_rareData->m_liveCalleeLocalsAtYield[index];
    }

    EvalCodeCache& evalCodeCache() { createRareDataIfNecessary(); return m_rareData->m_evalCodeCache; }

    enum ShrinkMode {
        // Shrink prior to generating machine code that may point directly into vectors.
        EarlyShrink,

        // Shrink after generating machine code, and after possibly creating new vectors
        // and appending to others. At this time it is not safe to shrink certain vectors
        // because we would have generated machine code that references them directly.
        LateShrink
    };
    void shrinkToFit(ShrinkMode);

    // Functions for controlling when JITting kicks in, in a mixed mode
    // execution world.

    bool checkIfJITThresholdReached()
    {
        return m_llintExecuteCounter.checkIfThresholdCrossedAndSet(this);
    }

    void dontJITAnytimeSoon()
    {
        m_llintExecuteCounter.deferIndefinitely();
    }

    void jitAfterWarmUp()
    {
        m_llintExecuteCounter.setNewThreshold(Options::thresholdForJITAfterWarmUp(), this);
    }

    void jitSoon()
    {
        m_llintExecuteCounter.setNewThreshold(Options::thresholdForJITSoon(), this);
    }

    const BaselineExecutionCounter& llintExecuteCounter() const
    {
        return m_llintExecuteCounter;
    }

    // Functions for controlling when tiered compilation kicks in. This
    // controls both when the optimizing compiler is invoked and when OSR
    // entry happens. Two triggers exist: the loop trigger and the return
    // trigger. In either case, when an addition to m_jitExecuteCounter
    // causes it to become non-negative, the optimizing compiler is
    // invoked. This includes a fast check to see if this CodeBlock has
    // already been optimized (i.e. replacement() returns a CodeBlock
    // that was optimized with a higher tier JIT than this one). In the
    // case of the loop trigger, if the optimized compilation succeeds
    // (or has already succeeded in the past) then OSR is attempted to
    // redirect program flow into the optimized code.

    // These functions are called from within the optimization triggers,
    // and are used as a single point at which we define the heuristics
    // for how much warm-up is mandated before the next optimization
    // trigger files. All CodeBlocks start out with optimizeAfterWarmUp(),
    // as this is called from the CodeBlock constructor.

    // When we observe a lot of speculation failures, we trigger a
    // reoptimization. But each time, we increase the optimization trigger
    // to avoid thrashing.
    JS_EXPORT_PRIVATE unsigned reoptimizationRetryCounter() const;
    void countReoptimization();
#if ENABLE(JIT)
    static unsigned numberOfLLIntBaselineCalleeSaveRegisters() { return RegisterSet::llintBaselineCalleeSaveRegisters().numberOfSetRegisters(); }
    static size_t llintBaselineCalleeSaveSpaceAsVirtualRegisters();
    size_t calleeSaveSpaceAsVirtualRegisters();

    unsigned numberOfDFGCompiles();

    int32_t codeTypeThresholdMultiplier() const;

    int32_t adjustedCounterValue(int32_t desiredThreshold);

    int32_t* addressOfJITExecuteCounter()
    {
        return &m_jitExecuteCounter.m_counter;
    }

    static ptrdiff_t offsetOfJITExecuteCounter() { return OBJECT_OFFSETOF(CodeBlock, m_jitExecuteCounter) + OBJECT_OFFSETOF(BaselineExecutionCounter, m_counter); }
    static ptrdiff_t offsetOfJITExecutionActiveThreshold() { return OBJECT_OFFSETOF(CodeBlock, m_jitExecuteCounter) + OBJECT_OFFSETOF(BaselineExecutionCounter, m_activeThreshold); }
    static ptrdiff_t offsetOfJITExecutionTotalCount() { return OBJECT_OFFSETOF(CodeBlock, m_jitExecuteCounter) + OBJECT_OFFSETOF(BaselineExecutionCounter, m_totalCount); }

    const BaselineExecutionCounter& jitExecuteCounter() const { return m_jitExecuteCounter; }

    unsigned optimizationDelayCounter() const { return m_optimizationDelayCounter; }

    // Check if the optimization threshold has been reached, and if not,
    // adjust the heuristics accordingly. Returns true if the threshold has
    // been reached.
    bool checkIfOptimizationThresholdReached();

    // Call this to force the next optimization trigger to fire. This is
    // rarely wise, since optimization triggers are typically more
    // expensive than executing baseline code.
    void optimizeNextInvocation();

    // Call this to prevent optimization from happening again. Note that
    // optimization will still happen after roughly 2^29 invocations,
    // so this is really meant to delay that as much as possible. This
    // is called if optimization failed, and we expect it to fail in
    // the future as well.
    void dontOptimizeAnytimeSoon();

    // Call this to reinitialize the counter to its starting state,
    // forcing a warm-up to happen before the next optimization trigger
    // fires. This is called in the CodeBlock constructor. It also
    // makes sense to call this if an OSR exit occurred. Note that
    // OSR exit code is code generated, so the value of the execute
    // counter that this corresponds to is also available directly.
    void optimizeAfterWarmUp();

    // Call this to force an optimization trigger to fire only after
    // a lot of warm-up.
    void optimizeAfterLongWarmUp();

    // Call this to cause an optimization trigger to fire soon, but
    // not necessarily the next one. This makes sense if optimization
    // succeeds. Successfuly optimization means that all calls are
    // relinked to the optimized code, so this only affects call
    // frames that are still executing this CodeBlock. The value here
    // is tuned to strike a balance between the cost of OSR entry
    // (which is too high to warrant making every loop back edge to
    // trigger OSR immediately) and the cost of executing baseline
    // code (which is high enough that we don't necessarily want to
    // have a full warm-up). The intuition for calling this instead of
    // optimizeNextInvocation() is for the case of recursive functions
    // with loops. Consider that there may be N call frames of some
    // recursive function, for a reasonably large value of N. The top
    // one triggers optimization, and then returns, and then all of
    // the others return. We don't want optimization to be triggered on
    // each return, as that would be superfluous. It only makes sense
    // to trigger optimization if one of those functions becomes hot
    // in the baseline code.
    void optimizeSoon();

    void forceOptimizationSlowPathConcurrently();

    void setOptimizationThresholdBasedOnCompilationResult(CompilationResult);
    
    uint32_t osrExitCounter() const { return m_osrExitCounter; }

    void countOSRExit() { m_osrExitCounter++; }

    uint32_t* addressOfOSRExitCounter() { return &m_osrExitCounter; }

    static ptrdiff_t offsetOfOSRExitCounter() { return OBJECT_OFFSETOF(CodeBlock, m_osrExitCounter); }

    uint32_t adjustedExitCountThreshold(uint32_t desiredThreshold);
    uint32_t exitCountThresholdForReoptimization();
    uint32_t exitCountThresholdForReoptimizationFromLoop();
    bool shouldReoptimizeNow();
    bool shouldReoptimizeFromLoopNow();

    void setCalleeSaveRegisters(RegisterSet);
    void setCalleeSaveRegisters(std::unique_ptr<RegisterAtOffsetList>);
    
    RegisterAtOffsetList* calleeSaveRegisters() const { return m_calleeSaveRegisters.get(); }
#else // No JIT
    static unsigned numberOfLLIntBaselineCalleeSaveRegisters() { return 0; }
    static size_t llintBaselineCalleeSaveSpaceAsVirtualRegisters() { return 0; };
    void optimizeAfterWarmUp() { }
    unsigned numberOfDFGCompiles() { return 0; }
#endif

    bool shouldOptimizeNow();
    void updateAllValueProfilePredictions();
    void updateAllArrayPredictions();
    void updateAllPredictions();

    unsigned frameRegisterCount();
    int stackPointerOffset();

    bool hasOpDebugForLineAndColumn(unsigned line, unsigned column);

    bool hasDebuggerRequests() const { return m_debuggerRequests; }
    void* debuggerRequestsAddress() { return &m_debuggerRequests; }

    void addBreakpoint(unsigned numBreakpoints);
    void removeBreakpoint(unsigned numBreakpoints)
    {
        ASSERT(m_numBreakpoints >= numBreakpoints);
        m_numBreakpoints -= numBreakpoints;
    }

    enum SteppingMode {
        SteppingModeDisabled,
        SteppingModeEnabled
    };
    void setSteppingMode(SteppingMode);

    void clearDebuggerRequests()
    {
        m_steppingMode = SteppingModeDisabled;
        m_numBreakpoints = 0;
    }
    
    // FIXME: Make these remaining members private.

    int m_numCalleeLocals;
    int m_numVars;
    
    // This is intentionally public; it's the responsibility of anyone doing any
    // of the following to hold the lock:
    //
    // - Modifying any inline cache in this code block.
    //
    // - Quering any inline cache in this code block, from a thread other than
    //   the main thread.
    //
    // Additionally, it's only legal to modify the inline cache on the main
    // thread. This means that the main thread can query the inline cache without
    // locking. This is crucial since executing the inline cache is effectively
    // "querying" it.
    //
    // Another exception to the rules is that the GC can do whatever it wants
    // without holding any locks, because the GC is guaranteed to wait until any
    // concurrent compilation threads finish what they're doing.
    mutable ConcurrentJITLock m_lock;

    Atomic<bool> m_visitWeaklyHasBeenCalled;

    bool m_shouldAlwaysBeInlined; // Not a bitfield because the JIT wants to store to it.

#if ENABLE(JIT)
    unsigned m_capabilityLevelState : 2; // DFG::CapabilityLevel
#endif

    bool m_allTransitionsHaveBeenMarked : 1; // Initialized and used on every GC.

    bool m_didFailFTLCompilation : 1;
    bool m_hasBeenCompiledWithFTL : 1;
    bool m_isConstructor : 1;
    bool m_isStrictMode : 1;
    unsigned m_codeType : 2; // CodeType

    // Internal methods for use by validation code. It would be private if it wasn't
    // for the fact that we use it from anonymous namespaces.
    void beginValidationDidFail();
    NO_RETURN_DUE_TO_CRASH void endValidationDidFail();

    struct RareData {
        WTF_MAKE_FAST_ALLOCATED;
    public:
        Vector<HandlerInfo> m_exceptionHandlers;

        // Buffers used for large array literals
        Vector<Vector<JSValue>> m_constantBuffers;

        // Jump Tables
        Vector<SimpleJumpTable> m_switchJumpTables;
        Vector<StringJumpTable> m_stringSwitchJumpTables;

        Vector<FastBitVector> m_liveCalleeLocalsAtYield;

        EvalCodeCache m_evalCodeCache;
    };

    void clearExceptionHandlers()
    {
        if (m_rareData)
            m_rareData->m_exceptionHandlers.clear();
    }

    void appendExceptionHandler(const HandlerInfo& handler)
    {
        createRareDataIfNecessary(); // We may be handling the exception of an inlined call frame.
        m_rareData->m_exceptionHandlers.append(handler);
    }

    CallSiteIndex newExceptionHandlingCallSiteIndex(CallSiteIndex originalCallSite);

#if ENABLE(JIT)
    void setPCToCodeOriginMap(std::unique_ptr<PCToCodeOriginMap>&&);
    Optional<CodeOrigin> findPC(void* pc);
#endif

protected:
    void finalizeLLIntInlineCaches();
    void finalizeBaselineJITInlineCaches();

#if ENABLE(DFG_JIT)
    void tallyFrequentExitSites();
#else
    void tallyFrequentExitSites() { }
#endif

private:
    friend class CodeBlockSet;
    
    CodeBlock* specialOSREntryBlockOrNull();
    
    void noticeIncomingCall(ExecState* callerFrame);
    
    double optimizationThresholdScalingFactor();

    void updateAllPredictionsAndCountLiveness(unsigned& numberOfLiveNonArgumentValueProfiles, unsigned& numberOfSamplesInProfiles);

    void setConstantRegisters(const Vector<WriteBarrier<Unknown>>& constants, const Vector<SourceCodeRepresentation>& constantsSourceCodeRepresentation)
    {
        ASSERT(constants.size() == constantsSourceCodeRepresentation.size());
        size_t count = constants.size();
        m_constantRegisters.resizeToFit(count);
        for (size_t i = 0; i < count; i++)
            m_constantRegisters[i].set(*m_vm, this, constants[i].get());
        m_constantsSourceCodeRepresentation = constantsSourceCodeRepresentation;
    }

    void replaceConstant(int index, JSValue value)
    {
        ASSERT(isConstantRegisterIndex(index) && static_cast<size_t>(index - FirstConstantRegisterIndex) < m_constantRegisters.size());
        m_constantRegisters[index - FirstConstantRegisterIndex].set(m_globalObject->vm(), this, value);
    }

    void dumpBytecode(
        PrintStream&, ExecState*, const Instruction* begin, const Instruction*&,
        const StubInfoMap& = StubInfoMap(), const CallLinkInfoMap& = CallLinkInfoMap());

    CString registerName(int r) const;
    CString constantName(int index) const;
    void printUnaryOp(PrintStream&, ExecState*, int location, const Instruction*&, const char* op);
    void printBinaryOp(PrintStream&, ExecState*, int location, const Instruction*&, const char* op);
    void printConditionalJump(PrintStream&, ExecState*, const Instruction*, const Instruction*&, int location, const char* op);
    void printGetByIdOp(PrintStream&, ExecState*, int location, const Instruction*&);
    void printGetByIdCacheStatus(PrintStream&, ExecState*, int location, const StubInfoMap&);
    enum CacheDumpMode { DumpCaches, DontDumpCaches };
    void printCallOp(PrintStream&, ExecState*, int location, const Instruction*&, const char* op, CacheDumpMode, bool& hasPrintedProfiling, const CallLinkInfoMap&);
    void printPutByIdOp(PrintStream&, ExecState*, int location, const Instruction*&, const char* op);
    void printPutByIdCacheStatus(PrintStream&, int location, const StubInfoMap&);
    void printLocationAndOp(PrintStream&, ExecState*, int location, const Instruction*&, const char* op);
    void printLocationOpAndRegisterOperand(PrintStream&, ExecState*, int location, const Instruction*& it, const char* op, int operand);

    void beginDumpProfiling(PrintStream&, bool& hasPrintedProfiling);
    void dumpValueProfiling(PrintStream&, const Instruction*&, bool& hasPrintedProfiling);
    void dumpArrayProfiling(PrintStream&, const Instruction*&, bool& hasPrintedProfiling);
    void dumpRareCaseProfile(PrintStream&, const char* name, RareCaseProfile*, bool& hasPrintedProfiling);
    void dumpResultProfile(PrintStream&, ResultProfile*, bool& hasPrintedProfiling);

    bool shouldVisitStrongly();
    bool shouldJettisonDueToWeakReference();
    bool shouldJettisonDueToOldAge();
    
    void propagateTransitions(SlotVisitor&);
    void determineLiveness(SlotVisitor&);
        
    void stronglyVisitStrongReferences(SlotVisitor&);
    void stronglyVisitWeakReferences(SlotVisitor&);
    void visitOSRExitTargets(SlotVisitor&);

    std::chrono::milliseconds timeSinceCreation()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_creationTime);
    }

    void createRareDataIfNecessary()
    {
        if (!m_rareData)
            m_rareData = std::make_unique<RareData>();
    }

    void insertBasicBlockBoundariesForControlFlowProfiler(RefCountedArray<Instruction>&);

    WriteBarrier<UnlinkedCodeBlock> m_unlinkedCode;
    int m_numParameters;
    union {
        unsigned m_debuggerRequests;
        struct {
            unsigned m_hasDebuggerStatement : 1;
            unsigned m_steppingMode : 1;
            unsigned m_numBreakpoints : 30;
        };
    };
    WriteBarrier<ExecutableBase> m_ownerExecutable;
    VM* m_vm;

    RefCountedArray<Instruction> m_instructions;
    VirtualRegister m_thisRegister;
    VirtualRegister m_scopeRegister;
    mutable CodeBlockHash m_hash;

    RefPtr<SourceProvider> m_source;
    unsigned m_sourceOffset;
    unsigned m_firstLineColumnOffset;

    RefCountedArray<LLIntCallLinkInfo> m_llintCallLinkInfos;
    SentinelLinkedList<LLIntCallLinkInfo, BasicRawSentinelNode<LLIntCallLinkInfo>> m_incomingLLIntCalls;
    RefPtr<JITCode> m_jitCode;
#if ENABLE(JIT)
    std::unique_ptr<RegisterAtOffsetList> m_calleeSaveRegisters;
    Bag<StructureStubInfo> m_stubInfos;
    Bag<ByValInfo> m_byValInfos;
    Bag<CallLinkInfo> m_callLinkInfos;
    SentinelLinkedList<CallLinkInfo, BasicRawSentinelNode<CallLinkInfo>> m_incomingCalls;
    SentinelLinkedList<PolymorphicCallNode, BasicRawSentinelNode<PolymorphicCallNode>> m_incomingPolymorphicCalls;
    std::unique_ptr<PCToCodeOriginMap> m_pcToCodeOriginMap;
#endif
    std::unique_ptr<CompactJITCodeMap> m_jitCodeMap;
#if ENABLE(DFG_JIT)
    // This is relevant to non-DFG code blocks that serve as the profiled code block
    // for DFG code blocks.
    DFG::ExitProfile m_exitProfile;
    CompressedLazyOperandValueProfileHolder m_lazyOperandValueProfiles;
#endif
    RefCountedArray<ValueProfile> m_argumentValueProfiles;
    RefCountedArray<ValueProfile> m_valueProfiles;
    SegmentedVector<RareCaseProfile, 8> m_rareCaseProfiles;
    SegmentedVector<ResultProfile, 8> m_resultProfiles;
    typedef HashMap<unsigned, unsigned, IntHash<unsigned>, WTF::UnsignedWithZeroKeyHashTraits<unsigned>> BytecodeOffsetToResultProfileIndexMap;
    std::unique_ptr<BytecodeOffsetToResultProfileIndexMap> m_bytecodeOffsetToResultProfileIndexMap;
    RefCountedArray<ArrayAllocationProfile> m_arrayAllocationProfiles;
    ArrayProfileVector m_arrayProfiles;
    RefCountedArray<ObjectAllocationProfile> m_objectAllocationProfiles;

    // Constant Pool
    COMPILE_ASSERT(sizeof(Register) == sizeof(WriteBarrier<Unknown>), Register_must_be_same_size_as_WriteBarrier_Unknown);
    // TODO: This could just be a pointer to m_unlinkedCodeBlock's data, but the DFG mutates
    // it, so we're stuck with it for now.
    Vector<WriteBarrier<Unknown>> m_constantRegisters;
    Vector<SourceCodeRepresentation> m_constantsSourceCodeRepresentation;
    RefCountedArray<WriteBarrier<FunctionExecutable>> m_functionDecls;
    RefCountedArray<WriteBarrier<FunctionExecutable>> m_functionExprs;

    WriteBarrier<CodeBlock> m_alternative;
    
    BaselineExecutionCounter m_llintExecuteCounter;

    BaselineExecutionCounter m_jitExecuteCounter;
    uint32_t m_osrExitCounter;
    uint16_t m_optimizationDelayCounter;
    uint16_t m_reoptimizationRetryCounter;

    std::chrono::steady_clock::time_point m_creationTime;

    std::unique_ptr<BytecodeLivenessAnalysis> m_livenessAnalysis;

    std::unique_ptr<RareData> m_rareData;

    UnconditionalFinalizer m_unconditionalFinalizer;
    WeakReferenceHarvester m_weakReferenceHarvester;
};

// Program code is not marked by any function, so we make the global object
// responsible for marking it.

class GlobalCodeBlock : public CodeBlock {
    typedef CodeBlock Base;
    DECLARE_INFO;

protected:
    GlobalCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, GlobalCodeBlock& other)
        : CodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }

    GlobalCodeBlock(VM* vm, Structure* structure, ScriptExecutable* ownerExecutable, UnlinkedCodeBlock* unlinkedCodeBlock, JSScope* scope, PassRefPtr<SourceProvider> sourceProvider, unsigned sourceOffset, unsigned firstLineColumnOffset)
        : CodeBlock(vm, structure, ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, sourceOffset, firstLineColumnOffset)
    {
    }
};

class ProgramCodeBlock : public GlobalCodeBlock {
public:
    typedef GlobalCodeBlock Base;
    DECLARE_INFO;

    static ProgramCodeBlock* create(VM* vm, CopyParsedBlockTag, ProgramCodeBlock& other)
    {
        ProgramCodeBlock* instance = new (NotNull, allocateCell<ProgramCodeBlock>(vm->heap))
            ProgramCodeBlock(vm, vm->programCodeBlockStructure.get(), CopyParsedBlock, other);
        instance->finishCreation(*vm, CopyParsedBlock, other);
        return instance;
    }

    static ProgramCodeBlock* create(VM* vm, ProgramExecutable* ownerExecutable, UnlinkedProgramCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider, unsigned firstLineColumnOffset)
    {
        ProgramCodeBlock* instance = new (NotNull, allocateCell<ProgramCodeBlock>(vm->heap))
            ProgramCodeBlock(vm, vm->programCodeBlockStructure.get(), ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, firstLineColumnOffset);
        instance->finishCreation(*vm, ownerExecutable, unlinkedCodeBlock, scope);
        return instance;
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(CellType, StructureFlags), info());
    }

private:
    ProgramCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, ProgramCodeBlock& other)
        : GlobalCodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }

    ProgramCodeBlock(VM* vm, Structure* structure, ProgramExecutable* ownerExecutable, UnlinkedProgramCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider, unsigned firstLineColumnOffset)
        : GlobalCodeBlock(vm, structure, ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, 0, firstLineColumnOffset)
    {
    }

    static void destroy(JSCell*);
};

class ModuleProgramCodeBlock : public GlobalCodeBlock {
public:
    typedef GlobalCodeBlock Base;
    DECLARE_INFO;

    static ModuleProgramCodeBlock* create(VM* vm, CopyParsedBlockTag, ModuleProgramCodeBlock& other)
    {
        ModuleProgramCodeBlock* instance = new (NotNull, allocateCell<ModuleProgramCodeBlock>(vm->heap))
            ModuleProgramCodeBlock(vm, vm->moduleProgramCodeBlockStructure.get(), CopyParsedBlock, other);
        instance->finishCreation(*vm, CopyParsedBlock, other);
        return instance;
    }

    static ModuleProgramCodeBlock* create(VM* vm, ModuleProgramExecutable* ownerExecutable, UnlinkedModuleProgramCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider, unsigned firstLineColumnOffset)
    {
        ModuleProgramCodeBlock* instance = new (NotNull, allocateCell<ModuleProgramCodeBlock>(vm->heap))
            ModuleProgramCodeBlock(vm, vm->moduleProgramCodeBlockStructure.get(), ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, firstLineColumnOffset);
        instance->finishCreation(*vm, ownerExecutable, unlinkedCodeBlock, scope);
        return instance;
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(CellType, StructureFlags), info());
    }

private:
    ModuleProgramCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, ModuleProgramCodeBlock& other)
        : GlobalCodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }

    ModuleProgramCodeBlock(VM* vm, Structure* structure, ModuleProgramExecutable* ownerExecutable, UnlinkedModuleProgramCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider, unsigned firstLineColumnOffset)
        : GlobalCodeBlock(vm, structure, ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, 0, firstLineColumnOffset)
    {
    }

    static void destroy(JSCell*);
};

class EvalCodeBlock : public GlobalCodeBlock {
public:
    typedef GlobalCodeBlock Base;
    DECLARE_INFO;

    static EvalCodeBlock* create(VM* vm, CopyParsedBlockTag, EvalCodeBlock& other)
    {
        EvalCodeBlock* instance = new (NotNull, allocateCell<EvalCodeBlock>(vm->heap))
            EvalCodeBlock(vm, vm->evalCodeBlockStructure.get(), CopyParsedBlock, other);
        instance->finishCreation(*vm, CopyParsedBlock, other);
        return instance;
    }

    static EvalCodeBlock* create(VM* vm, EvalExecutable* ownerExecutable, UnlinkedEvalCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider)
    {
        EvalCodeBlock* instance = new (NotNull, allocateCell<EvalCodeBlock>(vm->heap))
            EvalCodeBlock(vm, vm->evalCodeBlockStructure.get(), ownerExecutable, unlinkedCodeBlock, scope, sourceProvider);
        instance->finishCreation(*vm, ownerExecutable, unlinkedCodeBlock, scope);
        return instance;
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(CellType, StructureFlags), info());
    }

    const Identifier& variable(unsigned index) { return unlinkedEvalCodeBlock()->variable(index); }
    unsigned numVariables() { return unlinkedEvalCodeBlock()->numVariables(); }
    
private:
    EvalCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, EvalCodeBlock& other)
        : GlobalCodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }
        
    EvalCodeBlock(VM* vm, Structure* structure, EvalExecutable* ownerExecutable, UnlinkedEvalCodeBlock* unlinkedCodeBlock,
        JSScope* scope, PassRefPtr<SourceProvider> sourceProvider)
        : GlobalCodeBlock(vm, structure, ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, 0, 1)
    {
    }
    
    static void destroy(JSCell*);

private:
    UnlinkedEvalCodeBlock* unlinkedEvalCodeBlock() const { return jsCast<UnlinkedEvalCodeBlock*>(unlinkedCodeBlock()); }
};

class FunctionCodeBlock : public CodeBlock {
public:
    typedef CodeBlock Base;
    DECLARE_INFO;

    static FunctionCodeBlock* create(VM* vm, CopyParsedBlockTag, FunctionCodeBlock& other)
    {
        FunctionCodeBlock* instance = new (NotNull, allocateCell<FunctionCodeBlock>(vm->heap))
            FunctionCodeBlock(vm, vm->functionCodeBlockStructure.get(), CopyParsedBlock, other);
        instance->finishCreation(*vm, CopyParsedBlock, other);
        return instance;
    }

    static FunctionCodeBlock* create(VM* vm, FunctionExecutable* ownerExecutable, UnlinkedFunctionCodeBlock* unlinkedCodeBlock, JSScope* scope,
        PassRefPtr<SourceProvider> sourceProvider, unsigned sourceOffset, unsigned firstLineColumnOffset)
    {
        FunctionCodeBlock* instance = new (NotNull, allocateCell<FunctionCodeBlock>(vm->heap))
            FunctionCodeBlock(vm, vm->functionCodeBlockStructure.get(), ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, sourceOffset, firstLineColumnOffset);
        instance->finishCreation(*vm, ownerExecutable, unlinkedCodeBlock, scope);
        return instance;
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(CellType, StructureFlags), info());
    }

private:
    FunctionCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, FunctionCodeBlock& other)
        : CodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }

    FunctionCodeBlock(VM* vm, Structure* structure, FunctionExecutable* ownerExecutable, UnlinkedFunctionCodeBlock* unlinkedCodeBlock, JSScope* scope,
        PassRefPtr<SourceProvider> sourceProvider, unsigned sourceOffset, unsigned firstLineColumnOffset)
        : CodeBlock(vm, structure, ownerExecutable, unlinkedCodeBlock, scope, sourceProvider, sourceOffset, firstLineColumnOffset)
    {
    }
    
    static void destroy(JSCell*);
};

#if ENABLE(WEBASSEMBLY)
class WebAssemblyCodeBlock : public CodeBlock {
public:
    typedef CodeBlock Base;
    DECLARE_INFO;

    static WebAssemblyCodeBlock* create(VM* vm, CopyParsedBlockTag, WebAssemblyCodeBlock& other)
    {
        WebAssemblyCodeBlock* instance = new (NotNull, allocateCell<WebAssemblyCodeBlock>(vm->heap))
            WebAssemblyCodeBlock(vm, vm->webAssemblyCodeBlockStructure.get(), CopyParsedBlock, other);
        instance->finishCreation(*vm, CopyParsedBlock, other);
        return instance;
    }

    static WebAssemblyCodeBlock* create(VM* vm, WebAssemblyExecutable* ownerExecutable, JSGlobalObject* globalObject)
    {
        WebAssemblyCodeBlock* instance = new (NotNull, allocateCell<WebAssemblyCodeBlock>(vm->heap))
            WebAssemblyCodeBlock(vm, vm->webAssemblyCodeBlockStructure.get(), ownerExecutable, globalObject);
        instance->finishCreation(*vm, ownerExecutable, globalObject);
        return instance;
    }

    static Structure* createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
    {
        return Structure::create(vm, globalObject, prototype, TypeInfo(CellType, StructureFlags), info());
    }

private:
    WebAssemblyCodeBlock(VM* vm, Structure* structure, CopyParsedBlockTag, WebAssemblyCodeBlock& other)
        : CodeBlock(vm, structure, CopyParsedBlock, other)
    {
    }

    WebAssemblyCodeBlock(VM* vm, Structure* structure, WebAssemblyExecutable* ownerExecutable, JSGlobalObject* globalObject)
        : CodeBlock(vm, structure, ownerExecutable, globalObject)
    {
    }

    static void destroy(JSCell*);
};
#endif

inline Register& ExecState::r(int index)
{
    CodeBlock* codeBlock = this->codeBlock();
    if (codeBlock->isConstantRegisterIndex(index))
        return *reinterpret_cast<Register*>(&codeBlock->constantRegister(index));
    return this[index];
}

inline Register& ExecState::r(VirtualRegister reg)
{
    return r(reg.offset());
}

inline Register& ExecState::uncheckedR(int index)
{
    RELEASE_ASSERT(index < FirstConstantRegisterIndex);
    return this[index];
}

inline Register& ExecState::uncheckedR(VirtualRegister reg)
{
    return uncheckedR(reg.offset());
}

inline void CodeBlock::clearVisitWeaklyHasBeenCalled()
{
    m_visitWeaklyHasBeenCalled.store(false, std::memory_order_relaxed);
}

inline void CodeBlockSet::mark(const LockHolder& locker, void* candidateCodeBlock)
{
    ASSERT(m_lock.isLocked());
    // We have to check for 0 and -1 because those are used by the HashMap as markers.
    uintptr_t value = reinterpret_cast<uintptr_t>(candidateCodeBlock);
    
    // This checks for both of those nasty cases in one go.
    // 0 + 1 = 1
    // -1 + 1 = 0
    if (value + 1 <= 1)
        return;

    CodeBlock* codeBlock = static_cast<CodeBlock*>(candidateCodeBlock); 
    if (!m_oldCodeBlocks.contains(codeBlock) && !m_newCodeBlocks.contains(codeBlock))
        return;

    mark(locker, codeBlock);
}

inline void CodeBlockSet::mark(const LockHolder&, CodeBlock* codeBlock)
{
    if (!codeBlock)
        return;

    // Try to recover gracefully if we forget to execute a barrier for a
    // CodeBlock that does value profiling. This is probably overkill, but we
    // have always done it.
    Heap::heap(codeBlock)->writeBarrier(codeBlock);

    m_currentlyExecuting.add(codeBlock);
}

template <typename Functor> inline void ScriptExecutable::forEachCodeBlock(Functor&& functor)
{
    switch (type()) {
    case ProgramExecutableType: {
        if (CodeBlock* codeBlock = static_cast<CodeBlock*>(jsCast<ProgramExecutable*>(this)->m_programCodeBlock.get()))
            codeBlock->forEachRelatedCodeBlock(std::forward<Functor>(functor));
        break;
    }

    case EvalExecutableType: {
        if (CodeBlock* codeBlock = static_cast<CodeBlock*>(jsCast<EvalExecutable*>(this)->m_evalCodeBlock.get()))
            codeBlock->forEachRelatedCodeBlock(std::forward<Functor>(functor));
        break;
    }

    case FunctionExecutableType: {
        Functor f(std::forward<Functor>(functor));
        FunctionExecutable* executable = jsCast<FunctionExecutable*>(this);
        if (CodeBlock* codeBlock = static_cast<CodeBlock*>(executable->m_codeBlockForCall.get()))
            codeBlock->forEachRelatedCodeBlock(f);
        if (CodeBlock* codeBlock = static_cast<CodeBlock*>(executable->m_codeBlockForConstruct.get()))
            codeBlock->forEachRelatedCodeBlock(f);
        break;
    }

    case ModuleProgramExecutableType: {
        if (CodeBlock* codeBlock = static_cast<CodeBlock*>(jsCast<ModuleProgramExecutable*>(this)->m_moduleProgramCodeBlock.get()))
            codeBlock->forEachRelatedCodeBlock(std::forward<Functor>(functor));
        break;
    }

    default:
        RELEASE_ASSERT_NOT_REACHED();
    }
}

} // namespace JSC

#endif // CodeBlock_h
