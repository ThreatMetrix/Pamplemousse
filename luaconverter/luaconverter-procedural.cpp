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

#include "luaconverter-internal.hpp"
#include "analyser.hpp"
#include "conversioncontext.hpp"
#include <assert.h>

void LuaConverter::Converter::process(Function::Assignment, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    assert(!node.children.empty());
    // In assignment, content is the field name
    output.rawField(node.fieldDescription);
    // The children apart from the first are indirections.
    AstNode::Children::const_iterator iter = node.children.begin();
    for (++iter; iter != node.children.end(); ++iter)
    {
        output.openBracket();
        convertAstToLuaWithNullAssertions(context, *iter, DEFAULT_TO_NIL, output);
        output.closeBracket();
    }
    
    output.keyword("=");
    // And the first child is the expression to set to
    convertAstToLuaWithNullAssertions(context, node.children.front(), DEFAULT_TO_NIL, output);
}


void LuaConverter::Converter::process(Function::Declartion, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    const bool hasContent = !node.children.empty();
    output.declare(node.fieldDescription, hasContent);
    if (hasContent)
    {
        // And the first child is the expression to set to
        convertAstToLuaWithNullAssertions(context, node.children.front(), DEFAULT_TO_NIL, output);
    }
}

void LuaConverter::Converter::process(Function::Block, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    for (Analyser::ChildAssertionIterator iter(context, node); iter.valid(); ++iter)
    {
        // The assertions carry into this statement
        // As these have no return values,
        convertAstSkipNullChecks(context, *iter, DEFAULT_TO_NIL, output);
        output.endline();
    }
}

// An if chain is like:
// A if a or B if b or C if c or D
// The predicate comes after the expression, which seems funny at first, but it works better this way.
void LuaConverter::Converter::process(Function::IfChain, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing, LuaOutputter & output)
{
    bool hasStartedBlock = false;
    // This set of assertions is maintained down the chain, based on the if conditions _not_ taken.
    Analyser::NonNoneAssertionStackGuard continuingNonNullAssertions(context);
    AstNode::Children::const_iterator iter = node.children.begin();
    while (iter != node.children.end())
    {
        Analyser::NonNoneAssertionStackGuard thisClauseAssertions(context);
        // Every second child is the body of the if statement, starting from the first.
        AstNode::Children::const_iterator body = (iter++);
        bool hasWrittenPredicate = false;
        AstNode::Children::const_iterator predicate = iter;
        // If we're not at the end of the list, the following condition is a predicate.
        if (predicate != node.children.end())
        {
            ++iter;
            
            if (!hasStartedBlock)
            {
                output.startIf();
                hasStartedBlock = true;
            }
            else
            {
                output.startElseIf();
            }
            
            convertAstToLuaWithNullAssertions(context, *predicate, DEFAULT_TO_FALSE, output);
            thisClauseAssertions.addAssertionsForCheck(*predicate, Analyser::ASSUME_TRUE);
            output.endPredicate();
            hasWrittenPredicate = true;
        }
        
        if (!hasWrittenPredicate && hasStartedBlock)
        {
            // If there is an odd number of items, then the final child is an else.
            output.startElse();
        }
        
        // Because there is no return value here, skip the null checks.
        convertAstSkipNullChecks(context, *body, DEFAULT_TO_NIL, output);
        
        output.endline();
        // Now that this clause is over, anything afterwards can assume it was not true.
        if (predicate != node.children.end())
        {
            continuingNonNullAssertions.addAssertionsForCheck(*predicate, Analyser::ASSUME_NOT_TRUE);
        }
    }
    
    if (hasStartedBlock)
    {
        output.endBlock();
    }
}
