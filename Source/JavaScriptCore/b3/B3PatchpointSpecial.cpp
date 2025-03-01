/*
 * Copyright (C) 2015-2016 Apple Inc. All rights reserved.
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

#include "config.h"
#include "B3PatchpointSpecial.h"

#if ENABLE(B3_JIT)

#include "AirGenerationContext.h"
#include "B3StackmapGenerationParams.h"
#include "B3ValueInlines.h"

namespace JSC { namespace B3 {

using namespace Air;

PatchpointSpecial::PatchpointSpecial()
{
}

PatchpointSpecial::~PatchpointSpecial()
{
}

void PatchpointSpecial::forEachArg(Inst& inst, const ScopedLambda<Inst::EachArgCallback>& callback)
{
    unsigned argIndex = 1;

    if (inst.origin->type() != Void)
        callback(inst.args[argIndex++], Arg::Def, inst.origin->airType(), inst.origin->airWidth());

    forEachArgImpl(0, argIndex, inst, SameAsRep, callback);
    argIndex += inst.origin->numChildren();

    for (unsigned i = inst.origin->as<PatchpointValue>()->numGPScratchRegisters; i--;)
        callback(inst.args[argIndex++], Arg::Scratch, Arg::GP, Arg::conservativeWidth(Arg::GP));
    for (unsigned i = inst.origin->as<PatchpointValue>()->numFPScratchRegisters; i--;)
        callback(inst.args[argIndex++], Arg::Scratch, Arg::FP, Arg::conservativeWidth(Arg::FP));
}

bool PatchpointSpecial::isValid(Inst& inst)
{
    PatchpointValue* patchpoint = inst.origin->as<PatchpointValue>();
    unsigned argIndex = 1;

    if (inst.origin->type() != Void) {
        if (argIndex >= inst.args.size())
            return false;
        
        if (!isArgValidForValue(inst.args[argIndex], patchpoint))
            return false;
        if (!isArgValidForRep(code(), inst.args[argIndex], patchpoint->resultConstraint))
            return false;
        argIndex++;
    }

    if (!isValidImpl(0, argIndex, inst))
        return false;
    argIndex += patchpoint->numChildren();

    if (argIndex + patchpoint->numGPScratchRegisters + patchpoint->numFPScratchRegisters
        != inst.args.size())
        return false;

    for (unsigned i = patchpoint->numGPScratchRegisters; i--;) {
        Arg arg = inst.args[argIndex++];
        if (!arg.isGPTmp())
            return false;
    }
    for (unsigned i = patchpoint->numFPScratchRegisters; i--;) {
        Arg arg = inst.args[argIndex++];
        if (!arg.isFPTmp())
            return false;
    }

    return true;
}

bool PatchpointSpecial::admitsStack(Inst& inst, unsigned argIndex)
{
    if (inst.origin->type() == Void)
        return admitsStackImpl(0, 1, inst, argIndex);

    if (argIndex == 1) {
        switch (inst.origin->as<PatchpointValue>()->resultConstraint.kind()) {
        case ValueRep::WarmAny:
        case ValueRep::StackArgument:
            return true;
        case ValueRep::SomeRegister:
        case ValueRep::Register:
            return false;
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return false;
        }
    }

    return admitsStackImpl(0, 2, inst, argIndex);
}

CCallHelpers::Jump PatchpointSpecial::generate(
    Inst& inst, CCallHelpers& jit, GenerationContext& context)
{
    PatchpointValue* value = inst.origin->as<PatchpointValue>();
    ASSERT(value);

    Vector<ValueRep> reps;
    unsigned offset = 1;
    if (inst.origin->type() != Void)
        reps.append(repForArg(*context.code, inst.args[offset++]));
    reps.appendVector(repsImpl(context, 0, offset, inst));
    offset += value->numChildren();

    StackmapGenerationParams params(value, reps, context);

    for (unsigned i = value->numGPScratchRegisters; i--;)
        params.m_gpScratch.append(inst.args[offset++].gpr());
    for (unsigned i = value->numFPScratchRegisters; i--;)
        params.m_fpScratch.append(inst.args[offset++].fpr());
    
    value->m_generator->run(jit, params);

    return CCallHelpers::Jump();
}

void PatchpointSpecial::dumpImpl(PrintStream& out) const
{
    out.print("Patchpoint");
}

void PatchpointSpecial::deepDumpImpl(PrintStream& out) const
{
    out.print("Lowered B3::PatchpointValue.");
}

} } // namespace JSC::B3

#endif // ENABLE(B3_JIT)
