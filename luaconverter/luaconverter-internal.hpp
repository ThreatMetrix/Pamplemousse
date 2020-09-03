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
//  This file contains dispatched functions for converting AST nodes into Lua

#ifndef luaconverter_internal_hpp
#define luaconverter_internal_hpp

#include "luaconverter.hpp"
#include "functiondispatch.hpp"

namespace LuaConverter
{
    class Converter
    {
    public:
        static void process(Function::UnaryOperator, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Operator, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Functionlike, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::MeanMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::TernaryMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::BoundMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::IsMissing, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::IsNotMissing, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::IsIn, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::SubstringMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::TrimBlank, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Constant, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::FieldRef, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::SurrogateMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::BooleanAnd, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::BooleanOr, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::DefaultMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::ThresholdMacro, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Block, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Declartion, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Assignment, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::IfChain, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::MakeTuple, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::Lambda, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::RunLambda, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
        static void process(Function::ReturnStatement, Analyser::AnalyserContext & context, const AstNode & node, DefaultIfMissing defaultIfMissing, LuaOutputter & output);
    };
    
    class MissingClauseConverter
    {
    public:
        static void process(Function::FieldRef, Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
        static void process(Function::TernaryMacro, Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
        static void process(Function::BoundMacro, Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
        static void process(Function::SurrogateMacro,Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
        static void process(Function::BooleanAndOr, Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
        static void process(Function::FunctionTypeBase, Analyser::AnalyserContext & context, const AstNode & node, bool invert, LuaOutputter & output);
    };
    
    
}

#endif /* luaconverter_internal_hpp */
