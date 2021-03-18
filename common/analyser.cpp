//  Copyright 2018-2020 Lexis Nexis Risk Solutions
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
//  Created by Caleb Moore on 15/6/20.
//
//  This file provides methods to analyse syntax in AST form in order to optimise it

#include "analyser.hpp"
#include "functiondispatch.hpp"
#include "conversioncontext.hpp"
#include <algorithm>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
static inline int strcasecmp(const char *s1, const char *s2)
{
    return lstrcmpiA(s1, s2);
}
#else
#include <strings.h>
#endif

namespace Analyser
{
namespace
{
    class MightBeMissing
    {
    public:
        static bool process(Function::BooleanAndOr, const AstNode & node, AnalyserContext & context)
        {
            for (ChildAssertionIterator iter(context, node); iter.valid(); ++iter)
            {
                // This is very similar to the logic that works out whether an and/or statement is trivial
                // We assume that all the other statements evaluated to false for OR, or true for AND,
                // Otherwise, the end result would still be a known value.
                NonNoneAssertionStackGuard temporaryAssertions(context);
                for (size_t j = iter.index() + 1; j < node.children.size(); j++)
                {
                    temporaryAssertions.addAssertionsForCheck(node.children[j], node.function().functionType == Function::BOOLEAN_AND ? ASSUME_NOT_FALSE : ASSUME_NOT_TRUE);
                }
                if (context.mightBeMissing(*iter))
                {
                    return true;
                }
            }
            return false;
        }
        
        static bool process(Function::SurrogateMacro, const AstNode & node, AnalyserContext & context)
        {
            return std::all_of(node.children.begin(), node.children.end(), [&context](const AstNode & child)
                               {
                                   return context.mightBeMissing(child);
                               });
        }
        
        static bool process(Function::FieldRef, const AstNode & node, AnalyserContext & context)
        {
            if (!node.children.empty())
            {
                // Don't even bother trying to infer anything from indirect references.
                return true;
            }
            return context.mightVariableBeMissing(*node.fieldDescription);
        }
        
        static bool process(Function::BoundMacro, const AstNode & node, AnalyserContext & context)
        {
            assert(node.children.size() >= 2);
            ChildAssertionIterator iter(context, node);
            if (context.checkIfTrivial(*iter) != ALWAYS_TRUE)
            {
                return true;
            }
            return context.mightBeMissing(*(++iter));
        }
        
        static bool process(Function::TernaryMacro, const AstNode & node, AnalyserContext & context)
        {
            // Super tricky logic to check ternaries. Ternaries are frequently used to perform complex unknown value substitution so
            // we do this anway.
            assert(node.children.size() == 3);
            ChildAssertionIterator iter(context, node);
            // If predicate is trivial, just worry about one side
            TrivialValue trivial = context.checkIfTrivial(*iter);
            if (trivial == ALWAYS_TRUE)
            {
                return context.mightBeMissing(*(++iter));
            }
            else if (trivial == ALWAYS_FALSE)
            {
                ++iter;
                return context.mightBeMissing(*(++iter));
            }
            
            // Otherwise, check if anything may be missing.
            for (; iter.valid(); ++iter)
            {
                if (context.mightBeMissing(*iter))
                {
                    return true;
                }
            }
            return false;
        }
        
        static bool process(Function::RunLambda, const AstNode & node, AnalyserContext & context)
        {
            assert(!node.children.empty());
            // We can't figure out what this is in this context.
            if (node.children.back().function().functionType != Function::LAMBDA)
            {
                return process(Function::FunctionTypeBase(), node, context);
            }
            else
            {
                // Iterate to the lambda bit
                ChildAssertionIterator outerIter(context, node);
                std::advance(outerIter, node.children.size() - 1);
                // Find the last part of the lambda inside (skip past parameters)
                ChildAssertionIterator innerIter(context, outerIter->children.back());
                std::advance(innerIter, node.children.size() - 1);
                return context.mightBeMissing(*innerIter);
            }
        }
        
        static bool process(Function::FunctionTypeBase, const AstNode & node, AnalyserContext & context)
        {
            switch(node.function().missingValueRule)
            {
                case Function::MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING:
                case Function::MISSING_IF_ANY_ARGUMENT_IS_MISSING:
                    return std::any_of(node.children.begin(), node.children.end(), [&context](const AstNode & child)
                                       {
                                           return context.mightBeMissing(child);
                                       });
                    
                case Function::NEVER_MISSING:
                    return false;
                    
                case Function::MAYBE_MISSING:
                    return true;
                    
                default:
                    return true;
            }
        }
    };
    
    class AddAssertionsForCheck
    {
    public:
        static void process(Function::NotOperator, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            assert(!node.children.empty());
            if (assumption == ASSUME_FALSE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_TRUE);
            }
            else if (assumption == ASSUME_TRUE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_FALSE);
            }
            else if (assumption == ASSUME_NOT_FALSE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_NOT_TRUE);
            }
            else if (assumption == ASSUME_NOT_TRUE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_NOT_FALSE);
            }
        }
        
        static void process(Function::IsMissing, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            assert(!node.children.empty());
            if (assumption == ASSUME_FALSE || assumption == ASSUME_NOT_TRUE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_NOT_MISSING);
            }
        }
        
        static void process(Function::IsNotMissing, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            assert(!node.children.empty());
            if (assumption == ASSUME_TRUE || assumption == ASSUME_NOT_FALSE)
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_NOT_MISSING);
            }
        }
        
        static void process(Function::BooleanAndOr, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            if (assumption == ASSUME_TRUE || assumption == ASSUME_NOT_FALSE || assumption == ASSUME_FALSE || assumption == ASSUME_NOT_TRUE)
            {
                const bool trueish = (assumption == ASSUME_TRUE || assumption == ASSUME_NOT_FALSE);
                if ((node.function().functionType == Function::BOOLEAN_AND &&  trueish) ||
                    (node.function().functionType == Function::BOOLEAN_OR  && !trueish))
                {
                    // This is the kind of obvious condition. Where all the node.children must evaluate to the same value.
                    for (const AstNode & childNode : node.children)
                    {
                        assertions.addAssertionsForCheck(childNode, assumption);
                    }
                }
                else
                {
                    // This is the harder case where we only know one child value must match our asserted value
                    // take the intersection.
                    NonNoneAssertionStackGuard::AssertionIntersection intersection;
                    bool first = true;
                    for (const AstNode & childNode : node.children)
                    {
                        NonNoneAssertionStackGuard localAssertions(assertions.context());
                        localAssertions.addAssertionsForCheck(childNode, assumption);
                        if (first)
                        {
                            intersection.add(localAssertions);
                            first = false;
                        }
                        else
                        {
                            intersection.intersect(localAssertions);
                        }
                    }
                    intersection.apply(assertions);
                }
            }
        }
        
        static void process(Function::DeclartionOrAssignment, const AstNode & node, Assumption, NonNoneAssertionStackGuard & assertions)
        {
            if ((!node.children.empty() && !assertions.context().mightBeMissing(node.children.front())) ||
                // Tables can never be null, so we can make the assertion right away.
                node.fieldDescription->field.dataType == PMMLDocument::TYPE_TABLE ||
                node.fieldDescription->field.dataType == PMMLDocument::TYPE_STRING_TABLE)
            {
                assertions.addVariableAssertion(*node.fieldDescription);
            }
        }
        
        static void process(Function::Block, const AstNode & node, Assumption, NonNoneAssertionStackGuard & assertions)
        {
            for (const AstNode & childNode : node.children)
            {
                assertions.addAssertionsForCheck(childNode, NO_ASSUMPTIONS);
            }
        }
        
        static void process(Function::IfChain, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            // Essentially, the assertions drawn from an if chain is the intersection of the paths leading to this point.
            NonNoneAssertionStackGuard::AssertionIntersection intersection;
            ChildAssertionIterator iter(assertions.context(), node);
            bool startedIntersection = false;
            bool implicitElse = true;
            for (;iter.valid(); ++iter)
            {
                NonNoneAssertionStackGuard localAssertions(assertions.context());
                localAssertions.addAssertionsForCheck(*iter, assumption);
                
                if (!startedIntersection)
                {
                    intersection.add(iter);
                    intersection.add(localAssertions);
                    startedIntersection = true;
                }
                else
                {
                    intersection.intersect(iter, localAssertions);
                }
                
                // If the last time we increment this, it's still valid, it means that there is no explicit else clause
                // If the last predicate always evaluates to true, that's as good as an else though.
                if (!(++iter).valid() ||
                    assertions.context().checkIfTrivial(*iter) == ALWAYS_TRUE)
                {
                    implicitElse = false;
                    break;
                }
            }
            // no else clause? Essentially make a blank else clause with JUST the false assertions of the if statements.
            if (implicitElse)
            {
                intersection.intersect(iter);
            }
            intersection.apply(assertions);
        }
        
        static void process(Function::TernaryMacro, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            if (!(assumption == NO_ASSUMPTIONS || assumption == ASSUME_MISSING))
            {
                ChildAssertionIterator iter(assertions.context(), node);
                if (assumption == ASSUME_NOT_MISSING || assumption == ASSUME_TRUE || assumption == ASSUME_FALSE)
                {
                    assertions.addAssertionsForCheck(*iter, ASSUME_NOT_MISSING);
                }
                
                NonNoneAssertionStackGuard::AssertionIntersection intersection;
                {
                    // Add the condition leading to the true clause and the clause itself
                    ++iter;
                    intersection.add(iter);
                    NonNoneAssertionStackGuard localAssertions(assertions.context());
                    localAssertions.addAssertionsForCheck(*iter, assumption);
                    intersection.add(localAssertions);
                }
                {
                    ++iter;
                    // Intersect with the same for the false clause
                    NonNoneAssertionStackGuard localAssertions(assertions.context());
                    localAssertions.addAssertionsForCheck(*iter, assumption);
                    intersection.intersect(iter, localAssertions);
                }
                
                intersection.apply(assertions);
            }
        }
        
        static void process(Function::DefaultMacro, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            assert(!node.children.empty());
            // If we are asserting something contrary to the default value for missing, then whatever the value underneath is, it must be known.
            if (node.content == "false" && (assumption == ASSUME_TRUE || assumption == ASSUME_NOT_FALSE))
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_TRUE);
            }
            if (node.content == "true" && (assumption == ASSUME_FALSE || assumption == ASSUME_NOT_TRUE))
            {
                assertions.addAssertionsForCheck(node.children.front(), ASSUME_FALSE);
            }
        }
        
        static void process(Function::BoundMacro, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            assert(!node.children.empty());
            assertions.addAssertionsForCheck(node.children.front(), ASSUME_TRUE);
            assertions.addAssertionsForCheck(node.children.back(), assumption);
        }
        
        static void process(Function::FieldRef, const AstNode & node, Assumption, NonNoneAssertionStackGuard & assertions)
        {
            assertions.addVariableAssertion(*node.fieldDescription);
        }
        
        // Catch all
        static void process(Function::FunctionTypeBase, const AstNode & node, Assumption assumption, NonNoneAssertionStackGuard & assertions)
        {
            // Unknown values may be treated as false in if statements, so we cannot assume known as well.
            if (assumption == ASSUME_NOT_MISSING || assumption == ASSUME_TRUE || assumption == ASSUME_FALSE)
            {
                if (node.function().missingValueRule == Function::MISSING_IF_ANY_ARGUMENT_IS_MISSING)
                {
                    for (const AstNode & child : node.children)
                    {
                        assertions.addAssertionsForCheck(child, ASSUME_NOT_MISSING);
                    }
                }
            }
        }
    };
    
    class CheckIfTrivial
    {
    public:
        static TrivialValue process(Function::IsMissing, const AstNode & node, AnalyserContext & context)
        {
            assert(node.children.size() == 1);
            if (context.mightBeMissing(node.children.front()))
            {
                return RUNTIME_EVALUATION_NEEDED;
            }
            return ALWAYS_FALSE;
        }
        
        static TrivialValue process(Function::IsNotMissing, const AstNode & node, AnalyserContext & context)
        {
            assert(node.children.size() == 1);
            if (context.mightBeMissing(node.children.front()))
            {
                return RUNTIME_EVALUATION_NEEDED;
            }
            return ALWAYS_TRUE;
        }
        
        static TrivialValue process(Function::Constant, const AstNode & node, AnalyserContext &)
        {
            return (node.type == PMMLDocument::TYPE_BOOL && PMMLDocument::strcasecmp(node.content.c_str(), "false") == 0) ? ALWAYS_FALSE : ALWAYS_TRUE;
        }
        
        static TrivialValue process(Function::BooleanAndOr, const AstNode & node, AnalyserContext & context)
        {
            const bool isAnd = node.function().functionType == Function::BOOLEAN_AND;
            TrivialValue out = isAnd ? ALWAYS_TRUE : ALWAYS_FALSE;
            const TrivialValue shortCircuitVal = isAnd ? ALWAYS_FALSE : ALWAYS_TRUE;
            for (ChildAssertionIterator iter(context, node); iter.valid(); ++iter)
            {
                {
                    // When working out if trivial, we can assert that the other conditions must all be true when making the determination
                    // This is to catch contraditions in null checks, for instance a == null and a != null.
                    // We don't do any other contradiction checking, although in theory we could, this is because most
                    // of the contradictions are rubbish thrown in related to default values etc.
                    NonNoneAssertionStackGuard temporaryAssertions(context);
                    for (size_t j = iter.index() + 1; j < node.children.size(); j++)
                    {
                        temporaryAssertions.addAssertionsForCheck(node.children[j], isAnd ? ASSUME_TRUE : ASSUME_FALSE);
                    }
                    if (context.checkIfTrivial(*iter) == shortCircuitVal)
                    {
                        return shortCircuitVal;
                    }
                }
                
                if (out != RUNTIME_EVALUATION_NEEDED && context.checkIfTrivial(*iter) == RUNTIME_EVALUATION_NEEDED)
                {
                    out = RUNTIME_EVALUATION_NEEDED;
                }
            }
            return out;
        }
        
        static TrivialValue process(Function::BooleanXor, const AstNode & node, AnalyserContext & context)
        {
            TrivialValue out = ALWAYS_FALSE;
            for (ChildAssertionIterator iter(context, node); iter.valid(); ++iter)
            {
                const TrivialValue value = context.checkIfTrivial(*iter);
                if (value == RUNTIME_EVALUATION_NEEDED)
                {
                    out = value;
                }
                else
                {
                    out = (out == value) ? ALWAYS_FALSE : ALWAYS_TRUE;
                }
            }
            return out;
        }
        
        static TrivialValue process(Function::IsIn, const AstNode & node, AnalyserContext &)
        {
            // Only trivial for empty set.
            if (node.children.size() > 1)
            {
                return RUNTIME_EVALUATION_NEEDED;
            }
            // Never in an empty set.
            return node.pFunction == &Function::functionTable.names.isIn ? ALWAYS_FALSE : ALWAYS_TRUE;
        }
        
        static TrivialValue process(Function::FunctionTypeBase, const AstNode &, AnalyserContext &)
        {
            return RUNTIME_EVALUATION_NEEDED;
        }
    };
    
    class FixAssertions
    {
    public:
        static void process(Function::TernaryMacro, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard & blockAssertions,
                     NonNoneAssertionStackGuard &)
        {
            if (i == 1)
            {
                blockAssertions.addAssertionsForCheck(node.children[0], ASSUME_TRUE);
            }
            else if (i == 2)
            {
                // Unlike an if statement, the predicate here must be both KNOWN and FALSE, rather than just false.
                blockAssertions.addAssertionsForCheck(node.children[0], ASSUME_FALSE);
            }
        }
        
        static void process(Function::BoundMacro, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard & blockAssertions,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            process(Function::TernaryMacro(), node, i, blockAssertions, runningAssertions);
        }
        
        static void process(Function::SurrogateMacro, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard &,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            if (i > 0)
            {
                runningAssertions.addAssertionsForCheck(node.children[i-1], ASSUME_MISSING);
            }
        }
        
        static void process(Function::BooleanAnd, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard &,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            if (i > 0)
            {
                runningAssertions.addAssertionsForCheck(node.children[i-1], ASSUME_NOT_FALSE);
            }
        }
        
        static void process(Function::BooleanOr, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard &,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            if (i > 0)
            {
                runningAssertions.addAssertionsForCheck(node.children[i-1], ASSUME_NOT_TRUE);
            }
        }
        
        static void process(Function::IfChain, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard & blockAssertions,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            if (i % 2 == 0)
            {
                // Previous if clauses all evaluated to false.
                if (i > 0)
                    runningAssertions.addAssertionsForCheck(node.children[i-1], ASSUME_NOT_TRUE);
                
                // Current if clause evaluate to true
                if (i + 1 < node.children.size())
                    blockAssertions.addAssertionsForCheck(node.children[i+1], ASSUME_TRUE);
            }
        }
        
        static void process(Function::RunLambda, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard & blockAssertions,
                     NonNoneAssertionStackGuard &)
        {
            if (i == (node.children.size() - 1) && node.children[i].function().functionType == Function::LAMBDA)
            {
                const AstNode & lambda = node.children[i];
                // Same number of arguments as parameters
                // The arguments are the last n-1 children of the RUN_LAMBDA
                // The parameters are the first n-1 children of the LAMBDA.
                assert(lambda.children.size() == node.children.size());
                
                // Transfer assertions from the calling scope to the called
                // I.e. if the argument is not null, neither is the parameter
                for (size_t j = 0; j < i; ++j)
                {
                    if (!blockAssertions.context().mightBeMissing(node.children[j]))
                    {
                        blockAssertions.addVariableAssertionByID(lambda.children[j].fieldDescription->id);
                    }
                }
            }
        }
        
        // Catch all
        static void process(Function::FunctionTypeBase, const AstNode & node, size_t i,
                     NonNoneAssertionStackGuard &,
                     NonNoneAssertionStackGuard & runningAssertions)
        {
            if (i > 0)
            {
                runningAssertions.addAssertionsForCheck(node.children[i-1], NO_ASSUMPTIONS);
            }
        }
    };
}

// This method checks whether this AST node might be missing. That is to say, whether there may be a possible state that may create an unknown output.
// It is used by the code generator to determine whether code should be emitted to deal with these unknown values (which may be quite bulky and slow).
// Also, things like ternary representations can be written far more succinctly and efficiently in Lua if they don't need to deal with nils.
// It is also used through checkIfTrivial for dead code removal, particularly for stripping out unneeded parts of inbuilt functions.
bool AnalyserContext::mightBeMissing(const AstNode & node)
{
    if (!mightClauseBeMissing(node.id))
    {
        return false;
    }
    
    MightBeMissing mightBeMissing;
    return Function::dispatchFunctionType<bool>(mightBeMissing, node.function().functionType, node, *this);
}

// Many if statements can be skipped because they are the same every time. This will return if the predicate expression will always return the same thing or not.
// This is used heavily in dead code removal, particularly for removing redundant parts automatically added to inbuilt functions.
TrivialValue AnalyserContext::checkIfTrivial(const AstNode & node)
{
    CheckIfTrivial checkIfTrivial;
    return Function::dispatchFunctionType<TrivialValue>(checkIfTrivial, node.function().functionType, node, *this);
}
    
void ChildAssertionIterator::fixAssertions()
{
    blockAssertions.clear();
    if (maintainAssertions == false || !valid())
    {
        return;
    }
    
    Analyser::FixAssertions fixAssertions;
    Function::dispatchFunctionType<void>(fixAssertions, node.function().functionType, node, i,
                                         blockAssertions, runningAssertions);
}

bool ChildAssertionIterator::valid() const
{
    return i < node.children.size();
}

const AstNode & ChildAssertionIterator::operator*()
{
    return node.children[i];
}

const AstNode * ChildAssertionIterator::operator->()
{
    return &node.children[i];
}
    
// This function is for seeing what would happen if a particular branch is taken.
// For example, you have a nested if statement, inside the inner if statement, there is a condition testing whether a value is unknown or not
// however, if the outer if statement already requires it to be known, the inner check will become dead code.
// It makes things like dead code removal, as well as non-missing assertions far more aggressive.
void NonNoneAssertionStackGuard::addAssertionsForCheck(const AstNode & node, Assumption assumption)
{
    AddAssertionsForCheck addAssertionsForCheck;
    Function::dispatchFunctionType<void>(addAssertionsForCheck, node.function().functionType, node, assumption, *this);
    
    // Unknown values may be treated as false in if statements, so we cannot assume known as well.
    if (assumption == ASSUME_NOT_MISSING || assumption == ASSUME_TRUE || assumption == ASSUME_FALSE)
    {
        addClauseAssertion(node.id);
    }
}
    
void NonNoneAssertionStackGuard::AssertionIntersection::addToIntersection(VarSet & vs, ClauseSet & cs, const NonNoneAssertionStackGuard & src) const
{
    for (const auto & i : src.m_frameContentVariables)
    {
        if (variables.count(i->first))
        {
            vs.insert(i->first);
        }
    }
    for (const auto & i :src.m_frameContentClauses)
    {
        if (clauses.count(i->first))
        {
            cs.insert(i->first);
        }
    }
}

void NonNoneAssertionStackGuard::AssertionIntersection::addToIntersection(VarSet & vs, ClauseSet & cs, const Analyser::ChildAssertionIterator & src) const
{
    addToIntersection(vs, cs, src.runningAssertions);
    addToIntersection(vs, cs, src.blockAssertions);
}

void NonNoneAssertionStackGuard::AssertionIntersection::add(const NonNoneAssertionStackGuard & src)
{
    for (const auto & i : src.m_frameContentVariables)
    {
        variables.insert(i->first);
    }
    for (const auto & i : src.m_frameContentClauses)
    {
        clauses.insert(i->first);
    }
}

void NonNoneAssertionStackGuard::AssertionIntersection::add(const Analyser::ChildAssertionIterator & src)
{
    add(src.runningAssertions);
    add(src.blockAssertions);
}

void NonNoneAssertionStackGuard::AssertionIntersection::apply(NonNoneAssertionStackGuard & guard) const
{
    for (const auto & variable : variables)
    {
        guard.addVariableAssertionByID(variable);
    }
    for (const auto & id : clauses)
    {
        guard.addClauseAssertion(id);
    }
}

}
