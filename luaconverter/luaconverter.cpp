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
//  Created by Caleb Moore on 7/11/18.
//
//  This file is the core of the system that converts from an Abstract Syntax Tree into Lua code.

#include "conversioncontext.hpp"
#include "analyser.hpp"
#include "luaconverter.hpp"
#include "luaconverter-internal.hpp"
#include "functiondispatch.hpp"
#include <assert.h>
#include <algorithm>

namespace LuaConverter
{
    void convertAstToLuaInner(Analyser::AnalyserContext & context, const AstNode & node,
                              DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    void convertAstSkipNullChecks(Analyser::AnalyserContext & context,
                               const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    void convertBruteForceMissingClause(Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
    void convertStandardMissingClause(Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
    
}

// This is the function that most external users call.
void LuaConverter::convertAstToLua(const AstNode & node, LuaOutputter & output)
{
    Analyser::AnalyserContext analyserContext;
    convertAstToLuaWithNullAssertions(analyserContext, node, DEFAULT_TO_FALSE, output);
}

// Output an expression in Lua. This will potentially add a bit in the front to make sure that it correctly returns nil if it would have
// evaluated to missing according to PMML's logic (rather than throwing an exception or something).
void LuaConverter::convertAstToLuaWithNullAssertions(Analyser::AnalyserContext & context,
                                                     const AstNode & node,  DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    if (node.function().missingValueRule == Function::MISSING_IF_ANY_ARGUMENT_IS_MISSING && context.mightBeMissing(node))
    {
        Analyser::NonNoneAssertionStackGuard innerGuard(context);
        // When defaulting to "true", use a non-inverted check and an "or" (i.e. is null or is in set)
        // Otherwise, use an inverted check and an "and" (i.e. isn't null and is in set)
        if (defaultIfMissing != DEFAULT_TO_TRUE)
        {
            LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
            outputMissing(context, node, true, output);
            innerGuard.addAssertionsForCheck(node, Analyser::ASSUME_NOT_MISSING);

            output.keyword("and");
            convertAstSkipNullChecks(context, node, defaultIfMissing, output);
        }
        else
        {
            LuaOutputter::OperatorScopeHelper orScope(output, LuaOutputter::PRECEDENCE_OR);
            outputMissing(context, node, false, output);
            innerGuard.addAssertionsForCheck(node, Analyser::ASSUME_NOT_MISSING);
            output.keyword("or");
            convertAstSkipNullChecks(context, node, defaultIfMissing, output);
        }
    }
    else
    {
        convertAstSkipNullChecks(context, node, defaultIfMissing, output);
    }
}

// This is the next phase after null protection is applied.
void LuaConverter::convertAstSkipNullChecks(Analyser::AnalyserContext & context,
                                            const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    // Constant doesn't need coersion (as it uses coercedType itself)
    if (node.function().functionType != Function::CONSTANT && node.coercedType != node.type)
    {
        if(node.coercedType == PMMLDocument::TYPE_NUMBER)
        {
            output.keyword("tonumber");
        }
        else if (node.coercedType == PMMLDocument::TYPE_STRING)
        {
            output.keyword("tostring");
        }
        LuaOutputter::OperatorScopeHelper arguments(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
        convertAstToLuaInner(context, node, defaultIfMissing, output);
    }
    else
    {
        convertAstToLuaInner(context, node, defaultIfMissing, output);
    }
}

// This is the main body of Lua conversion. It is done after type coersion is finished.
void LuaConverter::convertAstToLuaInner(Analyser::AnalyserContext & context,
                                        const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    LuaConverter::Converter converter;
    Function::dispatchFunctionType<void>(converter, node.function().functionType, context, node, defaultIfMissing, output);
}

// This will output an expression that will evaluate to TRUE iff the result of this expression specified by "node" is unknown/missing or false iff expression is not missing.
// If "invert" is true, then this function outputs code that returns not nil if not unknown and nil if unknown.
// NonNullassertions on individual attributes will be added to guard.
// The non null assertions apply if this expression is not unknown/missing (regardless of whether "invert" is true or not)
void LuaConverter::outputMissing(Analyser::AnalyserContext & context, const AstNode & node,
                                 bool invert, LuaOutputter & output)
{
    LuaConverter::MissingClauseConverter converter;
    Function::dispatchFunctionType<void>(converter, node.function().functionType, context, node, invert, output);
}

void LuaConverter::Converter::process(Function::MakeTuple, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    bool notFirst = false;
    output.keyword("{");
    for (const AstNode & child : node.children)
    {
        if (notFirst)
        {
            output.comma();
        }
        notFirst = true;
        convertAstToLuaWithNullAssertions(context, child, defaultIfMissing, output);
    }
    output.keyword("}");
}

void LuaConverter::Converter::process(Function::FieldRef, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    if (node.children.empty())
    {
        output.field(node.fieldDescription);
    }
    else
    {
        LuaOutputter::OperatorScopeHelper andScope(output, LuaOutputter::PRECEDENCE_AND);
        // Subsequent children are indirections. This presents a chain of checking previous indirection isn't null.
        // e.g. val and val[ref1] and val[ref1][ref2]
        size_t firstIndirection = context.mightVariableBeMissing(*node.fieldDescription) ? 0 : 1;
        for (size_t nIndirection = firstIndirection; nIndirection <= node.children.size(); nIndirection++)
        {
            if (nIndirection > firstIndirection)
            {
                output.keyword("and");
            }
            output.field(node.fieldDescription);
            for (size_t innerIndirection = 0; innerIndirection < nIndirection; innerIndirection++)
            {
                output.openBracket();
                convertAstToLuaWithNullAssertions(context, node.children[innerIndirection], defaultIfMissing, output);
                output.closeBracket();
            }
        }
    }
    
    if (node.type == PMMLDocument::TYPE_BOOL)
    {
        if (defaultIfMissing == DEFAULT_TO_FALSE)
        {
            output.nullReplacement("false", node.type);
        }
        
        if (defaultIfMissing == DEFAULT_TO_TRUE)
        {
            output.nullReplacement("true", node.type);
        }
    }
}


void LuaConverter::Converter::process(Function::DefaultMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 1);
    // null replacement uses an equals or an or, depending on the type... a bit clumsy.
    LuaOutputter::OperatorScopeHelper clauseScope(output, node.type == PMMLDocument::TYPE_BOOL ? LuaOutputter::PRECEDENCE_EQUAL : LuaOutputter::PRECEDENCE_OR);
    
    // Optimise for sometimes where we can just default to the right thing.
    if (node.type == PMMLDocument::TYPE_BOOL && node.content == "true")
    {
        convertAstToLuaWithNullAssertions(context, node.children.front(), DEFAULT_TO_TRUE, output);
        // No need for any replacement... we already default to true!
        return;
    }
    
    convertAstToLuaWithNullAssertions(context, node.children.front(), DEFAULT_TO_FALSE, output);
    // Still require a null replacement, since DEFAULT_TO_FALSE may return null or false interchangeably
    
    output.nullReplacement(node.content.c_str(), node.type);
}


void LuaConverter::Converter::process(Function::IsNotMissing, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 1);
    // If defaultIfMissing == DEFAULT_TO_NULL, this means in this context, we really care about the difference between nil and false.
    LuaOutputter::OperatorScopeHelper clauseScope(output, LuaOutputter::PRECEDENCE_UNARY, defaultIfMissing == DEFAULT_TO_NIL);
    
    // Output the "isMissing" logic with a not in front of it (which returns true/false)
    // Otherwise, your standard inverted "outputMissing" clause is good enough.
    if (defaultIfMissing == DEFAULT_TO_NIL)
    {
        output.keyword("not");
    }
    outputMissing(context, node.children.front(), defaultIfMissing != DEFAULT_TO_NIL, output);
}

void LuaConverter::Converter::process(Function::IsMissing, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 1);
    // If this is an IS_MISSING, we can maintain assertions outside of the scope if false
    outputMissing(context, node.children.front(), false, output);
}


void LuaConverter::Converter::process(Function::UnaryOperator, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    LuaOutputter::OperatorScopeHelper clauseScope(output, node.function().operatorLevel);
    // Like "not"
    output.keyword(node.function().luaFunction);
    convertAstToLuaWithNullAssertions(context, node.children.front(), defaultIfMissing, output);
}

void LuaConverter::Converter::process(Function::ReturnStatement, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    output.keyword("return");
    for (size_t i = 0; i < node.children.size(); i++)
    {
        if (i != 0)
        {
            output.comma();
        }
        convertAstToLuaWithNullAssertions(context, node.children[i], DEFAULT_TO_NIL, output);
    }
}

void LuaConverter::Converter::process(Function::Constant, Analyser::AnalyserContext &, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    output.literal(node.content.c_str(), node.coercedType);
}

// Used for most operators.
void LuaConverter::Converter::process(Function::Operator, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output)
{
    LuaOutputter::OperatorScopeHelper clauseScope(output, node.function().operatorLevel);
    bool notFirst = false;
    for (const AstNode & child : node.children)
    {
        if (notFirst)
        {
            output.keyword(node.function().luaFunction);
        }
        notFirst = true;
        convertAstToLuaWithNullAssertions(context, child, defaultIfMissing, output);
    }
}

// Used for anything expressed like "function(arg1, arg2, arg3)"
void LuaConverter::Converter::process(Function::Functionlike, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    output.keyword(node.function().luaFunction);
    // This both adds a set of parenthesis and resets the scope.
    LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
    bool notFirst = false;
    for (const AstNode & child : node.children)
    {
        if (notFirst)
        {
            output.comma();
        }
        notFirst = true;
        convertAstToLuaWithNullAssertions(context, child, DEFAULT_TO_NIL, output);
    }
    
    if (node.function().functionType == Function::ROUND_MACRO)
    {
        output.keyword("+").literal(0.5);
    }
    else if (node.function().functionType == Function::LOG10_MACRO)
    {
        output.comma().literal(10);
    }
}

void LuaConverter::Converter::process(Function::RunLambda, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    {
        Analyser::ChildAssertionIterator iter(context, node);
        assert(iter.valid());
        // Go to the last bit (the lambda itself)
        std::advance(iter, node.children.size() - 1);
        
        // Lua doesn't like inline lambdas to not have parenthesis
        LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_NIL, output);
    }
    
    {
        LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
        bool notFirst = false;
        for (size_t i = 0; i + 1 < node.children.size(); ++i)
        {
            if (notFirst)
            {
                output.comma();
            }
            notFirst = true;
            convertAstToLuaWithNullAssertions(context,  node.children[i], DEFAULT_TO_NIL, output);
        }
    }
}

void LuaConverter::Converter::process(Function::Lambda, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(!node.children.empty());
    output.function();
    for (size_t i = 0; i < node.children.size() - 1; i++)
    {
        if (i != 0)
        {
            output.comma();
        }
        assert(node.children[i].function().functionType == Function::FIELD_REF);
        output.rawField(node.children[i].fieldDescription);
    }
    output.finishedArguments();
    const AstNode & body = node.children.back();
    if (body.function().functionType != Function::BLOCK)
    {
        output.keyword("return");
        convertAstToLuaWithNullAssertions(context, body, DEFAULT_TO_NIL, output);
        output.endline();
    }
    else
    {
        for (size_t i = 0; i < body.children.size(); i++)
        {
            const AstNode & child = body.children[i];
            if (i + 1 == body.children.size())
            {
                output.keyword("return");
            }
            convertAstToLuaWithNullAssertions(context, child, DEFAULT_TO_NIL, output);
            output.endline();
        }
    }
    output.endBlock();
}

// Used for the "substring" function. Cannot simply call Lua's substring function directly because arguments are different.
void LuaConverter::Converter::process(Function::SubstringMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    output.keyword(node.function().luaFunction);
    // This both adds a set of parenthesis and resets the scope.
    LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_PARENTHESIS);
    AstNode::Children::const_iterator iter = node.children.begin();
    convertAstToLuaWithNullAssertions(context, *(iter++), DEFAULT_TO_NIL, output);
    output.comma();
    // Do not bump iter here... it is used again in the next parameter
    convertAstToLuaWithNullAssertions(context, *(iter), DEFAULT_TO_NIL, output);
    output.comma();
    // Substrings in Lua are defined as two indexs, whereas they are defined as index + length in pmml
    // So add the index to the length as the final parameter and subtract 1, since the length is inclusive
    LuaOutputter::OperatorScopeHelper plusScope(output, LuaOutputter::PRECEDENCE_PLUS);
    convertAstToLuaWithNullAssertions(context, *(iter++), DEFAULT_TO_NIL, output);
    output.keyword("- 1 +");
    convertAstToLuaWithNullAssertions(context, *(iter++), DEFAULT_TO_NIL, output);
}

// Used for the "trimBlanks" function.
void LuaConverter::Converter::process(Function::TrimBlank, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 1);
    LuaOutputter::OperatorScopeHelper parenScope(output, LuaOutputter::PRECEDENCE_OR);
    
    convertAstToLuaWithNullAssertions(context, node.children.front(), DEFAULT_TO_NIL, output);
    // This match string yields very efficient trimming if string is not all spaces
    // but absolutely shocking performance if it is.
    // See: http://lua-users.org/wiki/StringTrim
    output.keyword(":match'^%s*(.*%S)' or ''");
}

// Used for the "avg" function.
void LuaConverter::Converter::process(Function::MeanMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    int nElements = 0;
    LuaOutputter::OperatorScopeHelper timesScope(output, LuaOutputter::PRECEDENCE_TIMES);
    {
        LuaOutputter::OperatorScopeHelper plusScope(output, LuaOutputter::PRECEDENCE_PLUS);
        for (const AstNode & child : node.children)
        {
            if (nElements > 0)
            {
                output.keyword("+");
            }
            convertAstToLuaWithNullAssertions(context, child, DEFAULT_TO_NIL, output);
            nElements++;
        }
    }
    output.keyword("/").literal(nElements);
}

// Used for the "threshold" function
void LuaConverter::Converter::process(Function::ThresholdMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(node.children.size() == 2);
    AstNode::Children::const_iterator iter = node.children.begin();
    LuaOutputter::OperatorScopeHelper plusScope(output, LuaOutputter::PRECEDENCE_OR);
    {
        LuaOutputter::OperatorScopeHelper timesScope(output, LuaOutputter::PRECEDENCE_AND);
        {
            LuaOutputter::OperatorScopeHelper plusScope(output, LuaOutputter::PRECEDENCE_EQUAL);
            convertAstToLuaWithNullAssertions(context, *iter++, DEFAULT_TO_NIL, output);
            output.keyword(">");
            convertAstToLuaWithNullAssertions(context, *iter++, DEFAULT_TO_NIL, output);
        }
        output.keyword("and 1");
    }
    output.keyword("or 0");
}

// Used when nullity cannot be inferred in a simpler way than just executing it and comparing to nil. Hopefully, used very, very rarely.
void LuaConverter::convertBruteForceMissingClause(Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output)
{
    if (node.type == PMMLDocument::TYPE_BOOL && invert)
    {
        LuaOutputter::OperatorScopeHelper equalScope(output, LuaOutputter::PRECEDENCE_OR);
        {
            LuaOutputter::OperatorScopeHelper equalScope(output, LuaOutputter::PRECEDENCE_EQUAL);
            convertAstToLuaWithNullAssertions(context, node, DEFAULT_TO_NIL, output);
            
            output.keyword("~= nil");
        }
        output.keyword("or nil");
    }
    else
    {
        LuaOutputter::OperatorScopeHelper equalScope(output, LuaOutputter::PRECEDENCE_EQUAL);
        convertAstToLuaWithNullAssertions(context, node, DEFAULT_TO_NIL, output);
        if (!invert)
        {
            output.keyword("== nil");
        }
    }
}

// This outputs an expression that evaluates to true if the value of an XOR compound LuaConverter is unknown.
void LuaConverter::convertStandardMissingClause(Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output)
{
    // Sometimes only a single value is missing, in which case opening parenthesis looks fugly. Count and special case.
    size_t missingCount = std::count_if(node.children.begin(), node.children.end(),
                                        [&context](const AstNode & child)
    {
        return context.mightBeMissing(child);
    });
    
    if (missingCount == 0)
    {
        output.keyword(invert ? "true" : "false");
    }
    else if (missingCount == 1)
    {
        for (const AstNode & child : node.children)
        {
            if (context.mightBeMissing(child))
            {
                outputMissing(context, child, invert, output);
                break;
            }
        }
    }
    else
    {
        bool gotContent = false;
        LuaOutputter::OperatorScopeHelper scope(output, invert ? LuaOutputter::PRECEDENCE_AND : LuaOutputter::PRECEDENCE_OR);
        Analyser::NonNoneAssertionStackGuard assertionHolder(context);
        for (const AstNode & child : node.children)
        {
            if (context.mightBeMissing(child))
            {
                if (gotContent)
                {
                    output.keyword(invert ? "and" : "or");
                }
                gotContent = true;
                outputMissing(context, child, invert, output);
                assertionHolder.addAssertionsForCheck(child, Analyser::ASSUME_NOT_MISSING);
            }
        }
    }
}

void LuaConverter::MissingClauseConverter::process(Function::FieldRef, Analyser::AnalyserContext & context, const AstNode & node,
                                                   bool invert, LuaOutputter & output)
{
    if (invert && node.children.empty())
    {
        output.nullCheck(node.fieldDescription);
    }
    else
    {
        LuaOutputter::OperatorScopeHelper equalScope(output, LuaOutputter::PRECEDENCE_EQUAL);
        // Subsequent children are indirections
        output.field(node.fieldDescription);
        for (const AstNode & child : node.children)
        {
            output.openBracket();
            convertAstToLuaWithNullAssertions(context, child, DEFAULT_TO_NIL, output);
            output.closeBracket();
        }
        
        output.keyword(invert ? "~=" : "==").keyword("nil");
    }
}


void LuaConverter::MissingClauseConverter::process(Function::FunctionTypeBase, Analyser::AnalyserContext & context, const AstNode & node,
                                                   bool invert, LuaOutputter & output)
{
    switch (node.function().missingValueRule)
    {
        case Function::NEVER_MISSING:
            output.literal(invert ? "true" : "false", PMMLDocument::TYPE_BOOL);
            break;
            
        case Function::MISSING_IF_ANY_ARGUMENT_IS_MISSING:
            convertStandardMissingClause(context, node, invert, output);
            break;
            
        case Function::MAYBE_MISSING_IF_ANY_ARGUMENT_IS_MISSING:
        case Function::MAYBE_MISSING:
            convertBruteForceMissingClause(context, node, invert, output);
            break;
    }
}
