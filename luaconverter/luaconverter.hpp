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

#ifndef luaconverter_hpp
#define luaconverter_hpp

#include "ast.hpp"
#include "luaoutputter.hpp"

namespace Analyser
{
    class AnalyserContext;
}

namespace LuaConverter
{
    enum DefaultIfMissing
    {
        // This is the proper behaviour of any PMML function. It returns nil whenever the result is missing/unknown. The only problem is, it generates slow and strange code
        // use it only when saving a result to a derived field or something similar.
        DEFAULT_TO_NIL,
        // This is a shortcut behaviour for situations using if statements.
        // Mostly returns nil when the result is missing/unknown, but sometimes also returns false, whatever is easier.
        DEFAULT_TO_FALSE,
        // This is a behaviour used to construct other pieces of logic, in particular the "AND" predicate. Returns true when value is missing/unknown.
        DEFAULT_TO_TRUE
    };
    
    // Convert an AST node to Lua code.
    void convertAstToLua(const AstNode & node, LuaOutputter & output);
    
    // Same as above, but assertions of any attributes that must not be not unknown if this expression evaluates to true/false.
    // This is used to propagate assertions to later code in other to save redundant null checks.
    void convertAstToLuaWithNullAssertions(Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    
    void convertAstSkipNullChecks(Analyser::AnalyserContext & context, const AstNode & node,
                                  DefaultIfMissing defaultIfMissing, LuaOutputter & output);

    // This will output an expression that will evaluate to TRUE iff the result of this expression specified by "node" is unknown/missing.
    // If "invert" is true, then this function outputs code that returns true if not unknown and nil if unknown.
    void outputMissing(Analyser::AnalyserContext & context, const AstNode & node,
                       bool invert, LuaOutputter & output);
}

#endif /* luaconverter_hpp */
