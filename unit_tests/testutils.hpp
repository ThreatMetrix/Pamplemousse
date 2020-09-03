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
//  Created by Caleb Moore on 21/11/18.
//

#ifndef testutils_hpp
#define testutils_hpp

#include <string>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "tinyxml2.h"
#include "conversioncontext.hpp"
#include "analyser.hpp"
static const char * const ROOT_PATH = "/../jpmml-samples/test-models/";

namespace TestUtils
{
    std::string getPathToFile(const char * name);
    lua_State * makeState(const tinyxml2::XMLDocument & document);
    void setupIDOutput(tinyxml2::XMLDocument & document, tinyxml2::XMLElement * model);
    void setupProbOutput(tinyxml2::XMLDocument & document, tinyxml2::XMLElement * model, const char * value);
    
    bool getValue(lua_State * L, const std::string & name, double & out);
    bool getValue(lua_State * L, const std::string & name, std::string & out);
    
    void pushValue(lua_State * L, std::nullptr_t ptr);
    void pushValue(lua_State * L, const std::string & value);
    void pushValue(lua_State * L, double value);
    
    bool executeModel(lua_State * L);
    template<typename T, typename... Args>
    bool executeModel(lua_State * L, const std::string & name, T value, Args... args)
    {
        pushValue(L, value);
        lua_setglobal(L,  name.c_str());
        return executeModel(L, args...);
    }
    
    inline bool mightBeMissingRecursive(Analyser::AnalyserContext & analyserContext, AstNode & node)
    {
        return analyserContext.mightBeMissing(node);
    }
    
    template<typename... Args>
    bool mightBeMissingRecursive(Analyser::AnalyserContext & analyserContext, AstNode & node, const PMMLDocument::ConstFieldDescriptionPtr & field, Args... args)
    {
        Analyser::NonNoneAssertionStackGuard guard(analyserContext);
        guard.addVariableAssertion(*field);
        return mightBeMissingRecursive(analyserContext, node, args...);
    }
    
    // Test if it might be missing even when a list of things might be defined
    template<typename... Args>
    bool mightBeMissingWith(AstNode & node, Args... args)
    {
        Analyser::AnalyserContext analyserContext;
        return mightBeMissingRecursive(analyserContext, node, args...);
    }
}


#define DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, name, type) \
    PMMLDocument::ScopedVariableDefinitionStackGuard holderFor##name(astBuilder.context()); \
    auto fieldFor##name = holderFor##name.addDataField(#name, type, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);

#define DEFINE_VARIABLE_INTO_SCOPE(astBuilder, name, type) \
    DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, name, type); \
    astBuilder.context().addDefaultMiningField(#name, fieldFor##name)

#define DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, name) DEFINE_VARIABLE_INTO_SCOPE(astBuilder, name, PMMLDocument::TYPE_NUMBER)
#define DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, name) DEFINE_VARIABLE_INTO_SCOPE(astBuilder, name, PMMLDocument::TYPE_STRING)
#define DEFINE_BOOL_VARIABLE_INTO_SCOPE(astBuilder, name) DEFINE_VARIABLE_INTO_SCOPE(astBuilder, name, PMMLDocument::TYPE_BOOL)


#endif /* testutils_hpp */
