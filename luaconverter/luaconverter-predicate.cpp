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
//  Created by Caleb Moore on 15/11/18.
//
//  This file contains functions for converting AST nodes representing boolean logic into Lua
//  These nodes may be from CompoundPredicate, Apply, or generated internally.

#include "luaconverter-internal.hpp"
#include "analyser.hpp"
#include "conversioncontext.hpp"
#include <algorithm>

namespace LuaConverter
{
    // Defined in luaconverter.cpp
    void convertStandardMissingClause(Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
}

// Covert a SimpleSetLuaConverter into Lua
void LuaConverter::Converter::process(Function::IsIn, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    if (node.children.empty())
    {
        // Safety check, this is already checked when the AST is created
        return;
    }
    
    LuaOutputter::OperatorScopeHelper andScope(output, node.function().operatorLevel);
    AstNode::Children::const_iterator iter = node.children.begin();
    const AstNode & firstField = *(iter++);
    
    bool notFirst = false;
    for (; iter != node.children.end(); ++iter)
    {
        if (notFirst)
        {
            // and for isNotIn, or for isIn
            output.keyword(node.function().operatorLevel == LuaOutputter::PRECEDENCE_OR ? "or" : "and");
        }
        notFirst = true;
        convertAstToLuaWithNullAssertions(context, firstField, defaultIfMissing, output);
        output.keyword(node.function().luaFunction);
        convertAstToLuaWithNullAssertions(context, *iter, defaultIfMissing, output);
    }
}

void LuaConverter::Converter::process(Function::BooleanAnd, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    // Sometimes we already know this isn't going to return not true, but there might be a child who's status is not known.
    if (defaultIfMissing == DEFAULT_TO_NIL && !context.mightBeMissing(node) &&
        std::any_of(node.children.begin(), node.children.end(), [&context](const AstNode & child){ return context.mightBeMissing(child); } ))
    {
        LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_OR);
        // Output the simplier DEFAULT_TO_FALSE logic, then fix any nulls it outputs
        process(Function::BooleanAnd(), context, node, DEFAULT_TO_FALSE, output);
        output.keyword("or").keyword("false");
        return;
    }
    
    LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
    bool notFirst = false;
    
    Analyser::NonNoneAssertionStackGuard assertionsIfTrue(context);
    std::list<AstNode::Children::const_iterator> deferred;
    for (AstNode::Children::const_iterator iter = node.children.begin(); iter != node.children.end(); ++iter)
    {
        // When using default_to_null, put all of the non-nullable clauses first, this lessens the whacky stuff that needs to be done for the nullable clauses
        if (defaultIfMissing == DEFAULT_TO_NIL && context.mightBeMissing(*iter))
        {
            deferred.push_back(iter);
        }
        else
        {
            if (notFirst)
            {
                output.keyword("and");
            }
            notFirst = true;
            // False assertions will not propagate within the statement or outside.
            // Within the statement, it is because the and will short circuit on a false, meaning the previous clauses were all true.
            // Outside this scope, it is because the clause may be false because of any of these clauses being false.
            convertAstToLuaWithNullAssertions(context, *iter, defaultIfMissing, output);
            assertionsIfTrue.addAssertionsForCheck(*iter, Analyser::ASSUME_TRUE);
        }
    }
    
    // PMML's nullity concept and Lua's are quite different.
    // If we actually want this thing to return "unknown" when it is really unknown (not when it is short circuited), then we need to do some funky logic here.
    // It prevent short circuit on all but the last clause by defaulting to true, then it will add those clauses back to the end to return unknown only if
    // the result might have been true if the value was known.
    if (defaultIfMissing == DEFAULT_TO_NIL)
    {
        auto secondLastIter = deferred.begin();
        for (auto iter = deferred.begin(); iter != deferred.end();)
        {
            // Special logic for DEFAULT_TO_NULL. Default to true for all but the last "and" to prevent short circuit.
            // so we pre-incremet the iterator to see if it is the last.
            secondLastIter = iter++;
            if (notFirst)
            {
                output.keyword("and");
            }
            notFirst = true;
            // DEFAULT_TO_TRUE does not add any assertions (since expression may be true even if unknown), so it will not mess up the outputMissing below.
            convertAstToLuaWithNullAssertions(context, **secondLastIter, iter == deferred.end() ? DEFAULT_TO_NIL : DEFAULT_TO_TRUE, output);
        }
        for (auto iter = deferred.begin(); iter != secondLastIter; ++iter)
        {
            output.keyword("and");
            // Assertions will be added by this clause in this case.
            outputMissing(context, **iter, true, output);
            assertionsIfTrue.addAssertionsForCheck(**iter, Analyser::ASSUME_NOT_MISSING);
        }
    }
}

void LuaConverter::Converter::process(Function::BooleanOr, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    LuaOutputter::OperatorScopeHelper orScope(output, LuaOutputter::PRECEDENCE_OR);
    bool notFirst = false;
    
    Analyser::NonNoneAssertionStackGuard assertionsIfFalse(context);
    std::list<AstNode::Children::const_iterator> deferred;
    for (AstNode::Children::const_iterator iter = node.children.begin(); iter != node.children.end(); ++iter)
    {
        // When using default_to_null, put all of the non-nullable clauses first, this lessens the whacky stuff that needs to be done for the nullable clauses
        if (defaultIfMissing == DEFAULT_TO_NIL && context.mightBeMissing(*iter))
        {
            deferred.push_back(iter);
        }
        else
        {
            if (notFirst)
            {
                output.keyword("or");
            }
            notFirst = true;
            convertAstToLuaWithNullAssertions(context, *iter, defaultIfMissing, output);
            assertionsIfFalse.addAssertionsForCheck(*iter, Analyser::ASSUME_NOT_TRUE);
        }
    }
    
    if (defaultIfMissing == DEFAULT_TO_NIL && !deferred.empty())
    {
        // Add all of the clauses that were deferred
        for (const AstNode::Children::const_iterator & iter : deferred)
        {
            if (notFirst)
            {
                output.keyword("or");
            }
            notFirst = true;
            // If deferred.size() == 0, we don't need the subsequent clause so use default_to_null to output the proper value
            // (having the nullable clause last means that Lua's short-circuit logic works), otherwise, use default_to_false
            // to output simpler clauses.
            convertAstToLuaWithNullAssertions(context, *iter, deferred.size() > 1 ? DEFAULT_TO_FALSE : DEFAULT_TO_NIL, output);
        }
        
        // If there was more than one nullable clause, we have a possibility that just writing A or B or C will give us the wrong answer.
        // this is because the lua "or" operator returns the second value if the first value is false or nil. However, we want to get the nil.
        // To accomplish this, we put the nullity test for previous clauses back at the end, with an "and nil" after them.
        // that way, if any of them are nil, return nil.
        if (deferred.size() > 1)
        {
            output.keyword("or");
            LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
            notFirst = false;
            {
                for (const AstNode::Children::const_iterator & iter : deferred)
                {
                    if (notFirst)
                    {
                        output.keyword("and");
                    }
                    notFirst = true;
                    outputMissing(context, *iter, true, output);
                }
            }
            output.keyword("and false");
        }
    }
}

// This converts a surrogate LuaConverter for bool into a Lua expression. For other types, it is handled by a simple short circuit chain.
void LuaConverter::Converter::process(Function::SurrogateMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    if (node.type == PMMLDocument::TYPE_BOOL)
    {
        // Starting the inline function is delayed in case the first option is always going to be taken
        bool inFunction = false;
        bool hasElse = false;
        for (const AstNode & child : node.children)
        {
            // Surrogates cannot maintain its null assertions into decending scope as any one of the clauses may be unknown.
            // Individual clauses get their own scope.
            Analyser::NonNoneAssertionStackGuard assertionsIfNotNull(context);
            // We build each possible surrogate by first outputting an inverse null clause (if needed) in an if block
            // if statement_A_valid then return statement_A elseif statement_B_valid then return statement_B ...
            bool mightBeUnknown = context.mightBeMissing(child);
            if (mightBeUnknown)
            {
                if (!inFunction)
                {
                    output.openParen().function().finishedArguments().startIf();
                    inFunction = true;
                }
                else
                {
                    output.endline().startElseIf();
                }
                outputMissing(context, child, true, output);
                assertionsIfNotNull.addAssertionsForCheck(child, Analyser::ASSUME_NOT_MISSING);
                output.endPredicate().keyword("return");
            }
            else if (inFunction)
            {
                output.endline().startElse().keyword("return");
            }
            
            convertAstToLuaWithNullAssertions(context, child, defaultIfMissing, output);
            
            if (!mightBeUnknown)
            {
                hasElse = true;
                break;
            }
        }
        
        if (defaultIfMissing == DEFAULT_TO_TRUE && !hasElse)
        {
            if (inFunction)
            {
                output.endline().startElse().keyword("return");
            }
            output.keyword("true");
        }
        
        if (inFunction)
        {
            output.endline().endBlock();
            output.endBlock(false).closeParen().openParen().closeParen();
        }
    }
    else
    {
        // Technically "surrogate" being used in compound LuaConverter may never be anything but a bool.
        // however, this function is borrowed in other places for other types.
        LuaOutputter::OperatorScopeHelper clauseScope(output, LuaOutputter::PRECEDENCE_OR);
        bool notFirst = false;
        for (const AstNode & child : node.children)
        {
            if (notFirst)
            {
                output.keyword("or");
            }
            notFirst = true;
            // Surrogates cannot maintain its null assertions into decending scope as any one of the clauses may be unknown.
            // Individual clauses get their own scope.
            convertAstToLuaWithNullAssertions(context, child, defaultIfMissing, output);
        }
    }
}
// This outputs an expression that evaluates to true if the value of an AND or OR compound LuaConverter is unknown.
void LuaConverter::MissingClauseConverter::process(Function::BooleanAndOr, Analyser::AnalyserContext & context,
                                                   const AstNode & node, bool invert, LuaOutputter & output)
{
    bool isOr = node.function().functionType == Function::BOOLEAN_OR;
    // Logic regarding unknown values in compound LuaConverters in pmml is based on possibility. If the resulting value would be the same whatever the value of an unknown is
    // then the value is not unknown. Specifically, this is if a condition based on a known value is true in an OR statement or a sub-expression false in an AND statement.
    
    // This has two sections. The first section is whether this MIGHT be true (for and) or false (for or). Otherwise, it is not unknown.
    LuaOutputter::OperatorScopeHelper outerScope(output, invert ? LuaOutputter::PRECEDENCE_OR : LuaOutputter::PRECEDENCE_AND);
    {
        LuaOutputter::OperatorScopeHelper notScope(output, LuaOutputter::PRECEDENCE_UNARY, isOr != invert);
        if (isOr != invert)
        {
            output.keyword("not");
        }
        
        convertAstToLuaWithNullAssertions(context, node, isOr ? DEFAULT_TO_FALSE : DEFAULT_TO_TRUE, output);
    }
    output.keyword(invert ? "or" : "and");
    
    // The second section is the same as other operations that require all arguments to be known to be known itself.
    convertStandardMissingClause(context, node, invert, output);
}

// This outputs an expression that evaluates to true if the value of an AND or OR compound LuaConverter is unknown.
void LuaConverter::MissingClauseConverter::process(Function::SurrogateMacro, Analyser::AnalyserContext & context,
                                                   const AstNode & node, bool invert, LuaOutputter & output)
{
    bool notFirst = false;
    Analyser::NonNoneAssertionStackGuard guard(context);
    LuaOutputter::OperatorScopeHelper scope(output, invert ? LuaOutputter::PRECEDENCE_OR : LuaOutputter::PRECEDENCE_AND);
    for (const AstNode & child : node.children)
    {
        if (context.mightBeMissing(child))
        {
            if (notFirst)
            {
                output.keyword(invert ? "or" : "and");
            }
            notFirst = true;
            outputMissing(context, child, invert, output);
        }
    }
    
    if (!notFirst)
    {
        output.keyword(invert ? "true" : "false");
    }
}
