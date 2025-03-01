/*
 * Copyright (C) 2013-2015 Apple Inc. All rights reserved.
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

#ifndef FTLOSRExit_h
#define FTLOSRExit_h

#if ENABLE(FTL_JIT)

#include "B3ValueRep.h"
#include "CodeOrigin.h"
#include "DFGExitProfile.h"
#include "DFGNodeOrigin.h"
#include "DFGOSRExitBase.h"
#include "FTLAbbreviatedTypes.h"
#include "FTLExitTimeObjectMaterialization.h"
#include "FTLExitValue.h"
#include "FTLFormattedValue.h"
#include "FTLOSRExitHandle.h"
#include "FTLStackMaps.h"
#include "FTLStackmapArgumentList.h"
#include "HandlerInfo.h"
#include "MethodOfGettingAValueProfile.h"
#include "Operands.h"
#include "Reg.h"
#include "ValueProfile.h"
#include "VirtualRegister.h"

namespace JSC {

class TrackedReferences;

namespace B3 {
class StackmapGenerationParams;
namespace Air {
struct GenerationContext;
} // namespace Air
} // namespace B3

namespace DFG {
struct NodeOrigin;
} // namespace DFG;

namespace FTL {

class State;
struct OSRExitDescriptorImpl;
struct OSRExitHandle;

#if !FTL_USES_B3
enum class ExceptionType : uint8_t {
    None,
    CCallException,
    JSCall,
    GetById,
    GetByIdCallOperation,
    PutById,
    PutByIdCallOperation,
    LazySlowPath,
    BinaryOpGenerator,
};
#endif // !FTL_USES_B3

struct OSRExitDescriptor {
    OSRExitDescriptor(
        DataFormat profileDataFormat, MethodOfGettingAValueProfile,
        unsigned numberOfArguments, unsigned numberOfLocals);

    // The first argument to the exit call may be a value we wish to profile.
    // If that's the case, the format will be not Invalid and we'll have a
    // method of getting a value profile. Note that all of the ExitArgument's
    // are already aware of this possible off-by-one, so there is no need to
    // correct them.
    DataFormat m_profileDataFormat;
    MethodOfGettingAValueProfile m_valueProfile;
    
    Operands<ExitValue> m_values;
    Bag<ExitTimeObjectMaterialization> m_materializations;

#if !FTL_USES_B3
    uint32_t m_stackmapID;
    bool m_isInvalidationPoint;
#endif // !FTL_USES_B3
    
    void validateReferences(const TrackedReferences&);

#if FTL_USES_B3
    // Call this once we have a place to emit the OSR exit jump and we have data about how the state
    // should be recovered. This effectively emits code that does the exit, though the code is really a
    // patchable jump and we emit the real code lazily. The description of how to emit the real code is
    // up to the OSRExit object, which this creates. Note that it's OK to drop the OSRExitHandle object
    // on the ground. It contains information that is mostly not useful if you use this API, since after
    // this call, the OSRExit is simply ready to go.
    RefPtr<OSRExitHandle> emitOSRExit(
        State&, ExitKind, const DFG::NodeOrigin&, CCallHelpers&, const B3::StackmapGenerationParams&,
        unsigned offset = 0);

    // In some cases you want an OSRExit to come into existence, but you don't want to emit it right now.
    // This will emit the OSR exit in a late path. You can't be sure exactly when that will happen, but
    // you know that it will be done by the time late path emission is done. So, a linker task will
    // surely happen after that. You can use the OSRExitHandle to retrieve the exit's label.
    //
    // This API is meant to be used for things like exception handling, where some patchpoint wants to
    // have a place to jump to for OSR exit. It doesn't care where that OSR exit is emitted so long as it
    // eventually gets access to its label.
    RefPtr<OSRExitHandle> emitOSRExitLater(
        State&, ExitKind, const DFG::NodeOrigin&, const B3::StackmapGenerationParams&,
        unsigned offset = 0);

private:
    // This is the low-level interface. It will create a handle representing the desire to emit code for
    // an OSR exit. You can call OSRExitHandle::emitExitThunk() once you have a place to emit it. Note
    // that the above two APIs are written in terms of this and OSRExitHandle::emitExitThunk().
    RefPtr<OSRExitHandle> prepareOSRExitHandle(
        State&, ExitKind, const DFG::NodeOrigin&, const B3::StackmapGenerationParams&,
        unsigned offset = 0);
#endif // FTL_USES_B3
};

#if !FTL_USES_B3
struct OSRExitDescriptorImpl {
    OSRExitDescriptorImpl(ExitKind kind, CodeOrigin exitOrigin, CodeOrigin forExitProfile, ExceptionType exceptionType)
        : m_kind(kind)
        , m_codeOrigin(exitOrigin)
        , m_codeOriginForExitProfile(forExitProfile)
        , m_exceptionType(exceptionType)
    {
    }

    ExitKind m_kind;
    CodeOrigin m_codeOrigin;
    CodeOrigin m_codeOriginForExitProfile;
    ExceptionType m_exceptionType;
    CodeOrigin m_semanticCodeOriginForCallFrameHeader;
    HandlerInfo m_baselineExceptionHandler;
};
#endif // !FTL_USES_B3

struct OSRExit : public DFG::OSRExitBase {
    OSRExit(
        OSRExitDescriptor*, ExitKind,
#if FTL_USES_B3
        CodeOrigin, CodeOrigin codeOriginForExitProfile
#else // FTL_USES_B3
        OSRExitDescriptorImpl&, uint32_t stackmapRecordIndex
#endif // FTL_USES_B3
        );

    OSRExitDescriptor* m_descriptor;
    MacroAssemblerCodeRef m_code;
#if FTL_USES_B3
    // This tells us where to place a jump.
    CodeLocationJump m_patchableJump;
    Vector<B3::ValueRep> m_valueReps;
#else // FTL_USES_B3
    // Offset within the exit stubs of the stub for this exit.
    unsigned m_patchableCodeOffset;
    // Offset within Stackmap::records
    uint32_t m_stackmapRecordIndex;
    ExceptionType m_exceptionType;
    RegisterSet registersToPreserveForCallThatMightThrow;
#endif // FTL_USES_B3

    CodeLocationJump codeLocationForRepatch(CodeBlock* ftlCodeBlock) const;
    void considerAddingAsFrequentExitSite(CodeBlock* profiledCodeBlock)
    {
        OSRExitBase::considerAddingAsFrequentExitSite(profiledCodeBlock, ExitFromFTL);
    }

#if !FTL_USES_B3
    void gatherRegistersToSpillForCallIfException(StackMaps&, StackMaps::Record&);
    void spillRegistersToSpillSlot(CCallHelpers&, int32_t stackSpillSlot);
    void recoverRegistersFromSpillSlot(CCallHelpers& jit, int32_t stackSpillSlot);

    bool willArriveAtExitFromIndirectExceptionCheck() const;
    bool willArriveAtOSRExitFromCallOperation() const;
    bool needsRegisterRecoveryOnGenericUnwindOSRExitPath() const;
#endif // !FTL_USES_B3
};


} } // namespace JSC::FTL

#endif // ENABLE(FTL_JIT)

#endif // FTLOSRExit_h

