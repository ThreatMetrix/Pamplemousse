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

#include "testutils.hpp"
#include "document.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "luaconverter/optimiser.hpp"
#include <sstream>

std::string TestUtils::getPathToFile(const char * name)
{
#if _WIN32
    const char allowed_sep[] = "/\\";
    const char preferred_sep[] = "\\";
#else
    const char allowed_sep[] = "/";
    const char preferred_sep[] = "/";
#endif

    std::string thisFileName(__FILE__);
    // Hack off the filename
    const size_t last_of = thisFileName.find_last_of(allowed_sep);
    if (last_of < std::string::npos)
        thisFileName.resize(last_of + 1);
    return thisFileName + preferred_sep + name;
}

const Function::Definition ReturnStatement =
{
    nullptr,
    Function::RETURN_STATEMENT,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};

lua_State * TestUtils::makeState(const tinyxml2::XMLDocument & document)
{
    std::stringstream mystream;
    
    AstBuilder builder;
    PMMLDocument::ConversionContext & context = builder.context();
    if (!PMMLDocument::convertPMML( builder, document.RootElement() ))
    {
        return nullptr;
    }
    
    auto var = context.createVariable(PMMLDocument::TYPE_TABLE, "output");
    builder.declare(var, AstBuilder::NO_INITIAL_VALUE);
    auto outputs = context.getOutputs();
    for (const auto & thisOutput : outputs)
    {
        builder.field(thisOutput.second);
        builder.constant(thisOutput.first, PMMLDocument::TYPE_STRING);
        builder.assignIndirect(var, 1);
    }
    builder.field(var);
    builder.function(ReturnStatement, 1);
    builder.block(builder.stackSize());
    
    AstNode astTree = builder.popNode();
    
    LuaOutputter output(mystream);
    PMMLDocument::optimiseAST(astTree, output);
    
    output.function("func");
    output.finishedArguments();
    
    LuaConverter::convertAstToLua(astTree, output);
    output.endBlock();
    
    std::string sourcecode = mystream.str();
    printf("%s", sourcecode.c_str());
    
    lua_State * L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dostring(L, sourcecode.c_str()))
    {
        fprintf(stderr, "%s\n", lua_tostring( L , -1 ) );
        lua_close(L);
        return nullptr;
    }
    
    return L;
}

bool TestUtils::executeModel(lua_State * L)
{
    lua_getglobal(L, "func");
    if (lua_pcall(L, 0, 1, 0))
    {
        fprintf(stderr, "%s\n", lua_tostring( L , -1 ) );
        return false;
    }
    return true;
}

void TestUtils::pushValue(lua_State * L, double value)
{
    lua_pushnumber(L, value);
}

void TestUtils::pushValue(lua_State * L, const std::string & value)
{
    lua_pushstring(L, value.c_str());
}

void TestUtils::pushValue(lua_State * L, std::nullptr_t ptr)
{
    lua_pushnil(L);
}

bool TestUtils::getValue(lua_State * L, const std::string & name, std::string & out)
{
    lua_getfield(L, -1, name.c_str());
    bool found = false;
    if (lua_isstring(L, -1))
    {
        out = lua_tostring(L, -1);
        found = true;
    }
    lua_pop(L, 1);
    
    return found;
}

bool TestUtils::getValue(lua_State * L, const std::string & name, double & out)
{
    lua_getfield(L, -1, name.c_str());
    bool found = false;
    if (lua_isnumber(L, -1))
    {
        out = lua_tonumber(L, -1);
        found = true;
    }
    lua_pop(L, 1);
    
    return found;
}

void TestUtils::setupIDOutput(tinyxml2::XMLDocument & document, tinyxml2::XMLElement * model)
{
    tinyxml2::XMLElement * output = model->FirstChildElement("Output");
    if (output == nullptr)
    {
        output = document.NewElement("Output");
        model->InsertAfterChild(model->FirstChildElement("MiningSchema"), output);
    }
    tinyxml2::XMLElement * outputField = document.NewElement("OutputField");
    outputField->SetAttribute("name", "id");
    outputField->SetAttribute("feature", "entityId");
    outputField->SetAttribute("dataType", "string");
    output->InsertFirstChild(outputField);
}

void TestUtils::setupProbOutput(tinyxml2::XMLDocument & document, tinyxml2::XMLElement * model, const char * value)
{
    tinyxml2::XMLElement * output = model->FirstChildElement("Output");
    if (output == nullptr)
    {
        output = document.NewElement("Output");
        model->InsertAfterChild(model->FirstChildElement("MiningSchema"), output);
    }
    tinyxml2::XMLElement * outputField = document.NewElement("OutputField");
    outputField->SetAttribute("name", value);
    outputField->SetAttribute("feature", "probability");
    outputField->SetAttribute("dataType", "double");
    outputField->SetAttribute("value", value);
    output->InsertFirstChild(outputField);
}
