/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
#include "B3FixSSA.h"

#if ENABLE(B3_JIT)

#include "B3BasicBlockInlines.h"
#include "B3BreakCriticalEdges.h"
#include "B3ControlValue.h"
#include "B3Dominators.h"
#include "B3IndexSet.h"
#include "B3InsertionSetInlines.h"
#include "B3MemoryValue.h"
#include "B3PhaseScope.h"
#include "B3ProcedureInlines.h"
#include "B3SSACalculator.h"
#include "B3SlotBaseValue.h"
#include "B3StackSlot.h"
#include "B3UpsilonValue.h"
#include "B3ValueInlines.h"
#include <wtf/CommaPrinter.h>

namespace JSC { namespace B3 {

namespace {
const bool verbose = false;
} // anonymous namespace

void demoteValues(Procedure& proc, const IndexSet<Value>& values)
{
    HashMap<Value*, SlotBaseValue*> map;
    HashMap<Value*, SlotBaseValue*> phiMap;

    // Create stack slots.
    InsertionSet insertionSet(proc);
    for (Value* value : values.values(proc.values())) {
        SlotBaseValue* stack = insertionSet.insert<SlotBaseValue>(
            0, value->origin(), proc.addAnonymousStackSlot(value->type()));
        map.add(value, stack);

        if (value->opcode() == Phi) {
            SlotBaseValue* phiStack = insertionSet.insert<SlotBaseValue>(
                0, value->origin(), proc.addAnonymousStackSlot(value->type()));
            phiMap.add(value, phiStack);
        }
    }
    insertionSet.execute(proc[0]);

    if (verbose) {
        dataLog("Demoting values as follows:\n");
        dataLog("   map = ");
        CommaPrinter comma;
        for (auto& entry : map)
            dataLog(comma, *entry.key, "=>", *entry.value);
        dataLog("\n");
        dataLog("   phiMap = ");
        comma = CommaPrinter();
        for (auto& entry : phiMap)
            dataLog(comma, *entry.key, "=>", *entry.value);
        dataLog("\n");
    }

    // Change accesses to the values to accesses to the stack slots.
    for (BasicBlock* block : proc) {
        for (unsigned valueIndex = 0; valueIndex < block->size(); ++valueIndex) {
            Value* value = block->at(valueIndex);

            if (value->opcode() == Phi) {
                if (SlotBaseValue* slotBase = phiMap.get(value)) {
                    value->replaceWithIdentity(
                        insertionSet.insert<MemoryValue>(
                            valueIndex, Load, value->type(), value->origin(), slotBase));
                }
            } else {
                for (Value*& child : value->children()) {
                    if (SlotBaseValue* slotBase = map.get(child)) {
                        child = insertionSet.insert<MemoryValue>(
                            valueIndex, Load, child->type(), value->origin(), slotBase);
                    }
                }

                if (UpsilonValue* upsilon = value->as<UpsilonValue>()) {
                    if (SlotBaseValue* slotBase = phiMap.get(upsilon->phi())) {
                        insertionSet.insert<MemoryValue>(
                            valueIndex, Store, upsilon->origin(), upsilon->child(0), slotBase);
                        value->replaceWithNop();
                    }
                }
            }

            if (SlotBaseValue* slotBase = map.get(value)) {
                insertionSet.insert<MemoryValue>(
                    valueIndex + 1, Store, value->origin(), value, slotBase);
            }
        }
        insertionSet.execute(block);
    }
}

bool fixSSA(Procedure& proc)
{
    PhaseScope phaseScope(proc, "fixSSA");
    
    // Collect the stack "variables". If there aren't any, then we don't have anything to do.
    // That's a fairly common case.
    HashMap<StackSlot*, Type> stackVariable;
    for (Value* value : proc.values()) {
        if (SlotBaseValue* slotBase = value->as<SlotBaseValue>()) {
            StackSlot* stack = slotBase->slot();
            if (stack->kind() == StackSlotKind::Anonymous)
                stackVariable.add(stack, Void);
        }
    }

    if (stackVariable.isEmpty())
        return false;

    // Make sure that we know how to optimize all of these. We only know how to handle Load and
    // Store on anonymous variables.
    for (Value* value : proc.values()) {
        auto reject = [&] (Value* value) {
            if (SlotBaseValue* slotBase = value->as<SlotBaseValue>())
                stackVariable.remove(slotBase->slot());
        };
        
        auto handleAccess = [&] (Value* access, Type type) {
            SlotBaseValue* slotBase = access->lastChild()->as<SlotBaseValue>();
            if (!slotBase)
                return;

            StackSlot* stack = slotBase->slot();
            
            if (value->as<MemoryValue>()->offset()) {
                stackVariable.remove(stack);
                return;
            }

            auto result = stackVariable.find(stack);
            if (result == stackVariable.end())
                return;
            if (result->value == Void) {
                result->value = type;
                return;
            }
            if (result->value == type)
                return;
            stackVariable.remove(result);
        };
        
        switch (value->opcode()) {
        case Load:
            // We're OK with loads from stack variables at an offset of zero.
            handleAccess(value, value->type());
            break;
        case Store:
            // We're OK with stores to stack variables, but not storing stack variables.
            reject(value->child(0));
            handleAccess(value, value->child(0)->type());
            break;
        default:
            for (Value* child : value->children())
                reject(child);
            break;
        }
    }

    Vector<StackSlot*> deadSlots;
    for (auto& entry : stackVariable) {
        if (entry.value == Void)
            deadSlots.append(entry.key);
    }

    for (StackSlot* deadSlot : deadSlots)
        stackVariable.remove(deadSlot);

    if (stackVariable.isEmpty())
        return false;

    // We know that we have variables to optimize, so do that now.
    breakCriticalEdges(proc);

    SSACalculator ssa(proc);

    // Create a SSACalculator::Variable for every stack variable.
    Vector<StackSlot*> variableToStack;
    HashMap<StackSlot*, SSACalculator::Variable*> stackToVariable;

    for (auto& entry : stackVariable) {
        StackSlot* stack = entry.key;
        SSACalculator::Variable* variable = ssa.newVariable();
        RELEASE_ASSERT(variable->index() == variableToStack.size());
        variableToStack.append(stack);
        stackToVariable.add(stack, variable);
    }

    // Create Defs for all of the stores to the stack variable.
    for (BasicBlock* block : proc) {
        for (Value* value : *block) {
            if (value->opcode() != Store)
                continue;

            SlotBaseValue* slotBase = value->child(1)->as<SlotBaseValue>();
            if (!slotBase)
                continue;

            StackSlot* stack = slotBase->slot();

            if (SSACalculator::Variable* variable = stackToVariable.get(stack))
                ssa.newDef(variable, block, value->child(0));
        }
    }

    // Decide where Phis are to be inserted. This creates them but does not insert them.
    ssa.computePhis(
        [&] (SSACalculator::Variable* variable, BasicBlock* block) -> Value* {
            StackSlot* stack = variableToStack[variable->index()];
            Value* phi = proc.add<Value>(Phi, stackVariable.get(stack), block->at(0)->origin());
            if (verbose) {
                dataLog(
                    "Adding Phi for ", pointerDump(stack), " at ", *block, ": ",
                    deepDump(proc, phi), "\n");
            }
            return phi;
        });

    // Now perform the conversion.
    InsertionSet insertionSet(proc);
    HashMap<StackSlot*, Value*> mapping;
    for (BasicBlock* block : proc.blocksInPreOrder()) {
        mapping.clear();

        for (auto& entry : stackToVariable) {
            StackSlot* stack = entry.key;
            SSACalculator::Variable* variable = entry.value;

            SSACalculator::Def* def = ssa.reachingDefAtHead(block, variable);
            if (def)
                mapping.set(stack, def->value());
        }

        for (SSACalculator::Def* phiDef : ssa.phisForBlock(block)) {
            StackSlot* stack = variableToStack[phiDef->variable()->index()];

            insertionSet.insertValue(0, phiDef->value());
            mapping.set(stack, phiDef->value());
        }

        for (unsigned valueIndex = 0; valueIndex < block->size(); ++valueIndex) {
            Value* value = block->at(valueIndex);
            value->performSubstitution();

            switch (value->opcode()) {
            case Load: {
                if (SlotBaseValue* slotBase = value->child(0)->as<SlotBaseValue>()) {
                    StackSlot* stack = slotBase->slot();
                    if (Value* replacement = mapping.get(stack))
                        value->replaceWithIdentity(replacement);
                }
                break;
            }
                
            case Store: {
                if (SlotBaseValue* slotBase = value->child(1)->as<SlotBaseValue>()) {
                    StackSlot* stack = slotBase->slot();
                    if (stackToVariable.contains(stack)) {
                        mapping.set(stack, value->child(0));
                        value->replaceWithNop();
                    }
                }
                break;
            }

            default:
                break;
            }
        }

        unsigned upsilonInsertionPoint = block->size() - 1;
        Origin upsilonOrigin = block->last()->origin();
        for (BasicBlock* successorBlock : block->successorBlocks()) {
            for (SSACalculator::Def* phiDef : ssa.phisForBlock(successorBlock)) {
                Value* phi = phiDef->value();
                SSACalculator::Variable* variable = phiDef->variable();
                StackSlot* stack = variableToStack[variable->index()];

                Value* mappedValue = mapping.get(stack);
                if (verbose) {
                    dataLog(
                        "Mapped value for ", *stack, " with successor Phi ", *phi, " at end of ",
                        *block, ": ", pointerDump(mappedValue), "\n");
                }
                
                if (!mappedValue)
                    mappedValue = insertionSet.insertBottom(upsilonInsertionPoint, phi);
                
                insertionSet.insert<UpsilonValue>(
                    upsilonInsertionPoint, upsilonOrigin, mappedValue, phi);
            }
        }

        insertionSet.execute(block);
    }

    if (verbose) {
        dataLog("B3 after SSA conversion:\n");
        dataLog(proc);
    }

    return true;
}

} } // namespace JSC::B3

#endif // ENABLE(B3_JIT)

