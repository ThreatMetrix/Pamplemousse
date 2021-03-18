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

//  This file contains functions to convert the ternary expression (a ? b : c) and bound psudo-expression (a ? b : nil) into Lua.
//  Becaue Lua doesn't have a ternary expression, this is more complex than it sounds and it is capable of choosing the most appropriate out of multiple different formats.

#include "analyser.hpp"
#include "luaconverter-internal.hpp"
#include "conversioncontext.hpp"
#include <sstream>
#include <assert.h>

namespace LuaConverter
{
    void convertTernaryExpressionInternal(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    void convertTernaryExpressionAsFunction(Analyser::AnalyserContext & context, const AstNode & node,DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    void convertTernaryExpressionTraditional(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    void convertTernaryExpressionBackwards(Analyser::AnalyserContext & context, const AstNode & node,
                                           const std::string & falseValue, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
}

void LuaConverter::convertTernaryExpressionInternal(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    Analyser::ChildAssertionIterator iter(context, node);
    const bool trueMightBeMissing = context.mightBeMissing(*(++iter));
    iter.reset();
    
    if (node.children[1].type != PMMLDocument::TYPE_BOOL && !trueMightBeMissing)
    {
        convertTernaryExpressionTraditional(context, node, defaultIfMissing, output);
    }
    else if (node.children[2].function().functionType == Function::CONSTANT)
    {
        convertTernaryExpressionBackwards(context, node, node.children[2].content, defaultIfMissing, output);
    }
    else
    {
        convertTernaryExpressionAsFunction(context, node, defaultIfMissing, output);
    }
}

// The traditional idiomatic way of writing a Lua ternary is "boolean_clause and value_if_true or value_if_false", however this fails if
// value_if_true is either nil or false. Thus, we can only use it in a very constrained situation where this will not be the case.
void LuaConverter::convertTernaryExpressionTraditional(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    Analyser::ChildAssertionIterator iter(context, node);
    LuaOutputter::OperatorScopeHelper orScope(output, LuaOutputter::PRECEDENCE_OR);
    {
        LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
        {
            convertAstToLuaWithNullAssertions(context, *(iter), DEFAULT_TO_FALSE, output);
        }
        output.keyword("and");
        convertAstToLuaWithNullAssertions(context, *(++iter), DEFAULT_TO_FALSE, output);
    }
    output.keyword("or");
    convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
}

// A sneaky way of writing a traditional Lua ternary for something that cannot be counted on for not being null or false is by inverting the condition and useing the false clause first.
void LuaConverter::convertTernaryExpressionBackwards(Analyser::AnalyserContext & context, const AstNode & node,
                                                     const std::string & falseValue, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    // To use Lua's fake ternary "a and b or c", we need to first determine whether "b" would evaluate to true or false.
    // If it defaults to false, we need to make an inverse fake ternary, "(not(a) or b) and c"
    
    PMMLDocument::FieldType trueClauseType = node.children[1].type;
    const bool replacementEvaluatesToPositive = trueClauseType != PMMLDocument::TYPE_BOOL || PMMLDocument::strcasecmp(falseValue.c_str(), "false") != 0;
    
    Analyser::ChildAssertionIterator iter(context, node);
    if (replacementEvaluatesToPositive)
    {
        // When default is not false
        // We output: something like "(isnull(a) or isnull(b)) and default or expression(a, b)
        // If the missing check returns true (value is missing), AND returns the right hand argument (the default) and the OR will return the left hand argument.
        // If the missing check returns false (value is not missing), AND returns the left hand argument (null) and OR will return the right hand argument.
        LuaOutputter::OperatorScopeHelper outerOperatorScope(output, LuaOutputter::PRECEDENCE_OR);
        {
            LuaOutputter::OperatorScopeHelper middleOperatorScope(output, LuaOutputter::PRECEDENCE_AND);
            {
                LuaOutputter::OperatorScopeHelper innerOperatorScope(output, LuaOutputter::PRECEDENCE_UNARY);
                output.keyword("not");
                convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
            }
            output.keyword("and").literal(falseValue.c_str(), trueClauseType);
        }
        output.keyword("or");
        convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
    }
    else
    {
        // When the default is false
        // We output: something like "((isnotnull(a) and isnotnull(b)) or default) and expression(a, b)
        // If the inverted missing check returns false (value is missing), OR returns the right hand argument (the default) and the AND will return the left hand argument.
        // If the inverted missing check returns true (value is not missing), OR returns the left hand argument (nonnull) and AND will return the right hand argument.
        LuaOutputter::OperatorScopeHelper outerOperatorScope(output, LuaOutputter::PRECEDENCE_AND);
        {
            LuaOutputter::OperatorScopeHelper middleOperatorScope(output, LuaOutputter::PRECEDENCE_OR);
            convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
            output.keyword("or").literal(falseValue.c_str(), trueClauseType);
        }
        output.keyword("and");
        convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
    }
}
// This is the final fallback for when a and b or c fails. It creates a function with up to three return paths.
void LuaConverter::convertTernaryExpressionAsFunction(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    Analyser::ChildAssertionIterator iter(context, node);
    // Open scope for operator scope guard
    {
        // This both adds a set of parenthesis and resets the scope. The parenthesis are actually required, otherwise Lua gets confused.
        LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
        output.function().finishedArguments();
        
        output.startIf();
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
        output.endPredicate();
        
        output.keyword("return");
        convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
        output.endline();
        
        output.startElse();
        
        output.keyword("return");
        convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
        output.endline();
        
        output.endBlock().endBlock(false);
    }
    output.openParen().closeParen();
}

// Converts a ternary expression A ? B : C into Lua
void LuaConverter::Converter::process(Function::TernaryMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 3);
    AstNode::Children::const_iterator predicate = node.children.begin();
    
    if (context.mightBeMissing(*predicate))
    {
        // If the predicate itself might be null, add an inverted null check before it to make it short circuit to a null
        LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
        outputMissing(context, *predicate, true, output);
        Analyser::NonNoneAssertionStackGuard assertionsIfTrue(context);
        assertionsIfTrue.addAssertionsForCheck(*predicate, Analyser::ASSUME_NOT_MISSING);
        output.keyword("and");
        convertTernaryExpressionInternal(context, node, defaultIfMissing, output);
    }
    else
    {
        convertTernaryExpressionInternal(context, node, defaultIfMissing, output);
    }
}

// Converts a bound psudo-expression (a ? b : nil) into Lua
void LuaConverter::Converter::process(Function::BoundMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 2);
    Analyser::ChildAssertionIterator iter(context, node);

    LuaOutputter::OperatorScopeHelper andScope(output, defaultIfMissing != DEFAULT_TO_TRUE ? LuaOutputter::PRECEDENCE_AND : LuaOutputter::PRECEDENCE_OR);
    {
        // Default to true adds a not, default to nil adds an or nil, default to false adds nothing:
        LuaOutputter::OperatorScopeHelper notScope(output,
                                                   defaultIfMissing == DEFAULT_TO_NIL ? LuaOutputter::PRECEDENCE_OR : LuaOutputter::PRECEDENCE_UNARY,
                                                   defaultIfMissing != DEFAULT_TO_FALSE);
        if (defaultIfMissing == DEFAULT_TO_TRUE)
        {
            output.keyword("not");
        }
        
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
        
        if (defaultIfMissing == DEFAULT_TO_NIL)
        {
            output.keyword("or").keyword("nil");
        }
    }
    output.keyword(defaultIfMissing != DEFAULT_TO_TRUE ? "and" : "or");
    convertAstToLuaWithNullAssertions(context, *(++iter), defaultIfMissing, output);
}

void LuaConverter::MissingClauseConverter::process(Function::TernaryMacro, Analyser::AnalyserContext & context,
                                                   const AstNode & node, bool invert, LuaOutputter & output)
{
    assert(node.children.size() == 3);
    
    Analyser::ChildAssertionIterator iter(context, node);
    const bool predicateMightBeMissing = context.mightBeMissing(*iter);
    // Firstly, take care of the scenario when the LuaConverter itself is null
    LuaOutputter::OperatorScopeHelper orScope(output, invert ? LuaOutputter::PRECEDENCE_AND : LuaOutputter::PRECEDENCE_OR);
    if (predicateMightBeMissing)
    {
        outputMissing(context, *iter, invert, output);
    }
    
    // These are calculated AFTER outputting the predicate's missing clause
    bool trueMightBeMissing = context.mightBeMissing(*(++iter));
    bool falseMightBeMissing = context.mightBeMissing(*(++iter));
    
    iter.reset();
    
    // Now the scenio when both false and true might be null (very complex, use a function)
    if (trueMightBeMissing && falseMightBeMissing)
    {
        if (predicateMightBeMissing)
        {
            output.keyword(invert ? "and" : "or");
        }
        // Open scope for operator scope guard
        {
            // This both adds a set of parenthesis and resets the scope.
            LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
            output.function().finishedArguments();
            output.startIf();
            convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
            output.endPredicate();
            
            {
                output.keyword("return");
                outputMissing(context, *(++iter), invert, output);
                output.endline();
            }
            
            output.startElse();
            
            {
                output.keyword("return");
                outputMissing(context, *(++iter), invert, output);
                output.endline();
            }
            
            output.endBlock().endBlock(false);
        }
        output.openParen().closeParen();
    }
    else if (trueMightBeMissing || falseMightBeMissing)
    {
        if (predicateMightBeMissing)
        {
            output.keyword(invert ? "and" : "or");
        }
        // Otherwise, use a simple and/or statement.
        LuaOutputter::OperatorScopeHelper insideScope(output, invert ? LuaOutputter::PRECEDENCE_OR : LuaOutputter::PRECEDENCE_AND);
        if (trueMightBeMissing == invert)
        {
            LuaOutputter::OperatorScopeHelper notScope(output, LuaOutputter::PRECEDENCE_UNARY);
            output.keyword("not");
            convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
        }
        else
        {
            convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
        }
        
        output.keyword(invert ?  "or" : "and");
        
        if (trueMightBeMissing)
        {
            ++iter;
            outputMissing(context, *iter, invert, output);
        }
        else
        {
            ++iter;
            ++iter;
            outputMissing(context, *iter, invert, output);
        }
    }
    else if (!predicateMightBeMissing)
    {
        // Absolutely nothing might be unknown... would be syntactically invalid to add nothing.
        output.literal(invert ? "true" : "false", PMMLDocument::TYPE_BOOL);
    }
}

void LuaConverter::MissingClauseConverter::process(Function::BoundMacro, Analyser::AnalyserContext & context,
                                                   const AstNode & node, bool invert, LuaOutputter & output)

{
    AstNode::Children::const_iterator iter = node.children.begin();
    if (iter == node.children.end())
    {
        return;
    }
    LuaOutputter::OperatorScopeHelper andScope(output, invert ? LuaOutputter::PRECEDENCE_AND : LuaOutputter::PRECEDENCE_OR);
    if (invert)
    {
        LuaOutputter::OperatorScopeHelper orScope(output, LuaOutputter::PRECEDENCE_OR);
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
        // False gets mapped to nil, as does nil.
        output.keyword("or nil");
    }
    else
    {
        LuaOutputter::OperatorScopeHelper unaryScope(output, LuaOutputter::PRECEDENCE_UNARY);
        output.keyword("not");
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_FALSE, output);
    }
    ++iter;
    if (context.mightBeMissing(*iter))
    {
        output.keyword(invert ? "and " : "or");
        outputMissing(context, *iter, invert, output);
    }
}
