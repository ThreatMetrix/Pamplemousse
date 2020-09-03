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

#include "Cuti.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "tinyxml2.h"
#include "conversioncontext.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/optimiser.hpp"
#include "model/transformation.hpp"
#include "analyser.hpp"
#include "testutils.hpp"
#include <iostream>

TEST_CLASS (TestTransform)
{
public:
    void parseIntoVM(AstBuilder & astBuilder, lua_State * L)
    {
        AstNode node = astBuilder.popNode();
        // Always run the optimiser here. It shouldn't make any functional difference, but it is important to make sure it's not breaking anything.
        LuaOutputter unusedOutputter(std::cout);
        PMMLDocument::optimiseAST(node, unusedOutputter);
        astBuilder.pushNode(node);

        {
            std::stringstream mystream;
            LuaOutputter outputter(mystream);
            outputter.keyword("nullity =");
            astBuilder.pushNode(node);
            astBuilder.function(Function::functionTable.names.isMissing, 1);
            astBuilder.lambda(0);
            LuaConverter::convertAstToLua(astBuilder.topNode(), outputter);
            astBuilder.popNode();
            
            printf("%s\n", mystream.str().c_str());
            if (luaL_dostring(L, mystream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
        {
            std::stringstream mystream;
            LuaOutputter outputter(mystream);
            outputter.keyword("inverseNullity =");
            astBuilder.pushNode(node);
            astBuilder.function(Function::functionTable.names.isNotMissing, 1);
            astBuilder.lambda(0);
            
            Analyser::AnalyserContext analyserContext;
            LuaConverter::convertAstToLuaWithNullAssertions(analyserContext, astBuilder.topNode(), LuaConverter::DEFAULT_TO_NIL, outputter);
            astBuilder.popNode();
            
            printf("%s\n", mystream.str().c_str());
            if (luaL_dostring(L, mystream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
        {
            std::stringstream mystream;
            LuaOutputter outputter(mystream);
            outputter.keyword("test =");
            astBuilder.pushNode(node);
            astBuilder.lambda(0);
            
            Analyser::AnalyserContext analyserContext;
            LuaConverter::convertAstToLuaWithNullAssertions(analyserContext, astBuilder.topNode(), LuaConverter::DEFAULT_TO_NIL, outputter);
            astBuilder.popNode();
            
            printf("%s\n", mystream.str().c_str());
            if (luaL_dostring(L, mystream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
    }
    
    
    void executeSimpleQuery(lua_State * L)
    {
        lua_getglobal(L, "nullity");
        if (lua_pcall(L, 0, 1, 0))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }
        bool isNullityTrue = lua_toboolean(L, -1);
        lua_pop(L, 1);
        
        lua_getglobal(L, "inverseNullity");
        if (lua_pcall(L, 0, 1, 0))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }
        bool isInverseNullityTrue = lua_toboolean(L, -1);
        lua_pop(L, 1);
        
        lua_getglobal(L, "test");
        if (lua_pcall(L, 0, 1, 0))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }
        bool resultIsNull = lua_isnil(L, -1);
        
        CPPUNIT_ASSERT_EQUAL(resultIsNull, isNullityTrue);
        CPPUNIT_ASSERT_EQUAL(!resultIsNull, isInverseNullityTrue);
    }
    
    void executeSimpleQuery(lua_State * L, const char * field, double value)
    {
        lua_pushnumber(L, value);
        lua_setglobal(L, field);
        executeSimpleQuery(L);
    }
    
    void executeSimpleQuery(lua_State * L, const char * field, const char * value)
    {
        if (value == nullptr)
        {
            lua_pushnil(L);
        }
        else
        {
            lua_pushstring(L, value);
        }
        lua_setglobal(L, field);
        executeSimpleQuery(L);
    }
    
    void executeSimpleQuery(lua_State * L, const char * field, bool value)
    {
        lua_pushboolean(L, value);
        lua_setglobal(L, field);
        executeSimpleQuery(L);
    }
    
    void testConstant()
    {
        lua_State * L = luaL_newstate();
        tinyxml2::XMLDocument numberDocument;
        numberDocument.Parse("<Constant>5</Constant>");
        {
            AstBuilder astBuilder;
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, numberDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(5.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument numberAsStringDocument;
        numberAsStringDocument.Parse("<Constant dataType=\"string\">5</Constant>");
        {
            AstBuilder astBuilder;
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, numberAsStringDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("5"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument stringDocument;
        stringDocument.Parse("<Constant>Hello World</Constant>");
        {
            AstBuilder astBuilder;
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, stringDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Hello World"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument boolDocument;
        boolDocument.Parse("<Constant dataType=\"boolean\">true</Constant>");
        {
            AstBuilder astBuilder;
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, boolDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);

#if LUA_VERSION_NUM >= 503
        // Older version of lua doesn't support hex escape codes
        tinyxml2::XMLDocument escapedDocument;
        escapedDocument.Parse("<Constant>newline&#10;tab&#09;backslash\\bel&#08;</Constant>");
        {
            AstBuilder astBuilder;
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, escapedDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));

            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("newline\ntab\tbackslash\\bel\b"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
#endif
    }
    
    void testFieldRef()
    {
        lua_State * L = luaL_newstate();

        tinyxml2::XMLDocument numberDocument;
        numberDocument.Parse("<FieldRef field=\"aNumber\"/>");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, aNumber);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, numberDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);

            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForaNumber));
        }
        executeSimpleQuery(L, "aNumber", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aNumber", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        numberDocument.RootElement()->SetAttribute("mapMissingTo", "4");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, aNumber);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, numberDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L, "aNumber", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aNumber", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        
        tinyxml2::XMLDocument stringDocument;
        stringDocument.Parse("<FieldRef field=\"aString\"/>");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, aString);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, stringDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);

            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForaString));
        }
        executeSimpleQuery(L, "aString", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aString", "Hello World");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Hello World"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        stringDocument.RootElement()->SetAttribute("mapMissingTo", "Missing");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, aString);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, stringDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "aString", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Missing"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aString", "Hello World");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Hello World"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        
        tinyxml2::XMLDocument boolDocument;
        boolDocument.Parse("<FieldRef field=\"aBool\"/>");
        {
            AstBuilder astBuilder;
            DEFINE_BOOL_VARIABLE_INTO_SCOPE(astBuilder, aBool);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, boolDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForaBool));
            
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L, "aBool", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", false);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", true);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        boolDocument.RootElement()->SetAttribute("mapMissingTo", "false");
        {
            AstBuilder astBuilder;
            DEFINE_BOOL_VARIABLE_INTO_SCOPE(astBuilder, aBool);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, boolDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "aBool", nullptr);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", false);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", true);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        boolDocument.RootElement()->SetAttribute("mapMissingTo", "true");
        {
            AstBuilder astBuilder;
            DEFINE_BOOL_VARIABLE_INTO_SCOPE(astBuilder, aBool);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, boolDocument.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "aBool", nullptr);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", false);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "aBool", true);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
    }
    
    void testDiscretise()
    {
        lua_State * L = luaL_newstate();
        tinyxml2::XMLDocument document;
        document.Parse("<Discretize field=\"Profit\">"
                       "<DiscretizeBin binValue=\"negative\">"
                       "<Interval closure=\"openOpen\" rightMargin=\"0\"/>"
                       "<!-- left margin is -infinity by default -->"
                       "</DiscretizeBin>"
                       "<DiscretizeBin binValue=\"positive\">"
                       "<Interval closure=\"closedOpen\" leftMargin=\"0\"/>"
                       "<!-- right margin is +infinity by default -->"
                       "</DiscretizeBin>"
                       "</Discretize>");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, Profit);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "Profit", 78.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("positive"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "Profit", -7.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("negative"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "Profit", 0.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("positive"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "Profit", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        tinyxml2::XMLElement * db1 = document.RootElement()->FirstChildElement("DiscretizeBin");
        tinyxml2::XMLElement * db2 = db1->NextSiblingElement("DiscretizeBin");
        db1->FirstChildElement()->SetAttribute("closure", "openClosed");
        db2->FirstChildElement()->SetAttribute("closure", "openOpen");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, Profit);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L, "Profit", 0.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("negative"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
    }
    
    void testDiscretiseDefaultAndMissing()
    {
        tinyxml2::XMLDocument defaultMiningSchema;
        defaultMiningSchema.Parse("<MiningSchema>"
                                  "<MiningField name=\"number\"/>"
                                  "</MiningSchema>");
        
        tinyxml2::XMLDocument document;
        lua_State * L = luaL_newstate();
        document.Parse("<Discretize field=\"number\">"
                       "<DiscretizeBin binValue=\"little\">"
                       "<Interval closure=\"closedOpen\" leftMargin=\"0\" rightMargin=\"10\"/>"
                       "</DiscretizeBin>"
                       "<DiscretizeBin binValue=\"medium\">"
                       "<Interval closure=\"closedOpen\" leftMargin=\"10\" rightMargin=\"100\"/>"
                       "</DiscretizeBin>"
                       "<DiscretizeBin binValue=\"big\">"
                       "<Interval closure=\"closedOpen\" leftMargin=\"100\"/>"
                       "</DiscretizeBin>"
                       "</Discretize>");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            // This function might still return a missing value even without the input missing
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldFornumber));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 78.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", -5.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        // This puts a default in the "medium" category
        tinyxml2::XMLDocument specialMiningSchema;
        specialMiningSchema.Parse("<MiningSchema>"
                                  "<MiningField name=\"number\" missingValueReplacement=\"20\" missingValueTreatment=\"asValue\"/>"
                                  "</MiningSchema>");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), specialMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            // Can still be missing (as there is still no default for the hole)
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 78.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        // Out of bounds are not handled.
        executeSimpleQuery(L, "number", -5.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        // This puts a default outside of any valid category
        tinyxml2::XMLDocument outOfBoundsMiningSchema;
        outOfBoundsMiningSchema.Parse("<MiningSchema>"
                                      "<MiningField name=\"number\" missingValueReplacement=\"-20\" missingValueTreatment=\"asValue\"/>"
                                      "</MiningSchema>");
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), outOfBoundsMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            // Can still be missing (as there is still no default for the hole)
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 78.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        // Out of bounds are not handled.
        executeSimpleQuery(L, "number", -5.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        // Default is now out of bounds... same result
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("defaultValue", "weirdNumber");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            {
                // If input is there, output is there
                CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldFornumber));
            }
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L,"number", 7.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("little"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L,"number", -5.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("weirdNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        // Use the mining schema with missing value replacement value
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), specialMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            // Now gaps are filled
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 78.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        // Out of bounds are not handled.
        executeSimpleQuery(L, "number", -5.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("weirdNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), outOfBoundsMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 700.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("big"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", -50.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("weirdNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("weirdNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("mapMissingTo", "noNumber");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 700.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("big"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", -50.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("weirdNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("noNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        document.RootElement()->DeleteAttribute("defaultValue");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            // This function might still return a missing value even without the input missing
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldFornumber));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "number", 700.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("big"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("noNumber"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", -5.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument newDocument;
        newDocument.Parse("<Discretize field=\"number\">"
                          "<DiscretizeBin binValue=\"negative\">"
                          "<Interval closure=\"closedOpen\" rightMargin=\"0\"/>"
                          "</DiscretizeBin>"
                          "<DiscretizeBin binValue=\"little\">"
                          "<Interval closure=\"closedOpen\" leftMargin=\"0\" rightMargin=\"10\"/>"
                          "</DiscretizeBin>"
                          "<DiscretizeBin binValue=\"medium\">"
                          "<Interval closure=\"closedOpen\" leftMargin=\"10\" rightMargin=\"100\"/>"
                          "</DiscretizeBin>"
                          "<DiscretizeBin binValue=\"big\">"
                          "<Interval closure=\"closedOpen\" leftMargin=\"100\"/>"
                          "</DiscretizeBin>"
                          "</Discretize>");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, newDocument.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            // We've filled in the hole in the left margin, cannot be missing.
            CPPUNIT_ASSERT(not TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldFornumber));
            parseIntoVM(astBuilder, L);
        }
        executeSimpleQuery(L, "number", -100.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("negative"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "number", 0.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("little"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);

        executeSimpleQuery(L, "number", 100.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("big"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, number, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), specialMiningSchema.RootElement());
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, newDocument.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
        }
    }
    
    void testNormContinuous()
    {
        tinyxml2::XMLDocument defaultMiningSchema;
        defaultMiningSchema.Parse("<MiningSchema>"
                                  "<MiningField name=\"sepal_length\"/>"
                                  "</MiningSchema>");

        tinyxml2::XMLDocument document;
        lua_State * L = luaL_newstate();
        document.Parse("<NormContinuous field=\"sepal_length\">"
                       "<LinearNorm orig=\"0\" norm=\"1\"/>"
                       "<LinearNorm orig=\"1\" norm=\"1\"/>"
                       "<LinearNorm orig=\"2\" norm=\"2\"/>"
                       "<LinearNorm orig=\"3\" norm=\"3\"/>"
                       "<LinearNorm orig=\"4\" norm=\"5\"/>"
                       "<LinearNorm orig=\"5\" norm=\"8\"/>"
                       "</NormContinuous>");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForsepal_length));
        }
        
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 0.5);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 1.5);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.5, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 3.5);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 6.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(11.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", -1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument missingValue3Point5MS;
        missingValue3Point5MS.Parse("<MiningSchema>"
                                    "<MiningField name=\"sepal_length\" missingValueReplacement=\"3.5\" missingValueTreatment=\"asValue\"/>"
                                    "</MiningSchema>");
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), missingValue3Point5MS.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        tinyxml2::XMLDocument missingValue6MS;
        missingValue6MS.Parse("<MiningSchema>"
                              "<MiningField name=\"sepal_length\" missingValueReplacement=\"6\" missingValueTreatment=\"asValue\"/>"
                              "</MiningSchema>");
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), missingValue6MS.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(11.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("mapMissingTo", "4");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        
        document.RootElement()->SetAttribute("outliers", "asExtremeValues");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", 6.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(8.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", -1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), missingValue6MS.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(8.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", -1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("outliers", "asMissingValues");
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), defaultMiningSchema.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            // This function might still return a missing value even without the input missing
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForsepal_length));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "sepal_length", 6.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", -1.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "sepal_length", 3.5);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        // mapMissingTo is still set above, should still work
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        
        {
            AstBuilder astBuilder;
            DEFINE_VARIABLE_INTO_SCOPE_CUSTOM(astBuilder, sepal_length, PMMLDocument::TYPE_NUMBER);
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), missingValue6MS.RootElement());
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        // This matches right over the edge, it shouldn't be accepted
        executeSimpleQuery(L, "sepal_length", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
    }
    
    void testNormDiscrete()
    {
        tinyxml2::XMLDocument document;
        lua_State * L = luaL_newstate();
        document.Parse("<NormDiscrete field=\"tag\" value=\"present\" />");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, tag);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldFortag));
        }
        
        executeSimpleQuery(L, "tag", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "tag", "present");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);

        executeSimpleQuery(L, "tag", "absent");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "tag", "somethingelse");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("mapMissingTo", "0");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, tag);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "tag", nullptr);
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "tag", "present");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "tag", "absent");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    
    void testMapValues()
    {
        tinyxml2::XMLDocument document;
        lua_State * L = luaL_newstate();
        document.Parse("<MapValues outputColumn=\"out\" dataType=\"integer\">"
                       "<FieldColumnPair field=\"BAND\" column=\"band\"/>"
                       "<FieldColumnPair field=\"STATE\" column=\"state\"/>"
                       "<InlineTable>"
                       "<row>"
                       "<band>1</band>"
                       "<state>MN</state>"
                       "<out>10000</out>"
                       "</row>"
                       "<row>"
                       "<band>1</band>"
                       "<state>IL</state>"
                       "<out>12000</out>"
                       "</row>"
                       "<row>"
                       "<band>1</band>"
                       "<state>NY</state>"
                       "<out>20000</out>"
                       "</row>"
                       "<row>"
                       "<band>2</band>"
                       "<state>MN</state>"
                       "<out>20000</out>"
                       "</row>"
                       "<row>"
                       "<band>2</band>"
                       "<state>IL</state>"
                       "<out>23000</out>"
                       "</row>"
                       "<row>"
                       "<band>2</band>"
                       "<state>NY</state>"
                       "<out>30000</out>"
                       "</row>"
                       "</InlineTable>"
                       "</MapValues>");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, BAND);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, STATE);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForBAND, fieldForSTATE));
        }
        executeSimpleQuery(L, "BAND", 1.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        lua_pushstring(L, "NY");
        lua_setglobal(L, "STATE");
        
        executeSimpleQuery(L, "BAND", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(20000.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", 2.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(30000.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", 3.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("mapMissingTo", "0");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, BAND);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, STATE);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForBAND, fieldForSTATE));
        }
        
        lua_pushstring(L, "IL");
        lua_setglobal(L, "STATE");
        
        executeSimpleQuery(L, "BAND", 2.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(23000.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", 3.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->SetAttribute("defaultValue", "19999");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, BAND);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, STATE);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
        }
        
        executeSimpleQuery(L, "BAND", 3.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(19999.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        lua_pushstring(L, "MN");
        lua_setglobal(L, "STATE");
        
        executeSimpleQuery(L, "BAND", 2.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(20000.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        document.RootElement()->DeleteAttribute("mapMissingTo");
        {
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, BAND);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, STATE);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, L);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForBAND, fieldForSTATE));
        }
        
        executeSimpleQuery(L, "BAND", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(10000.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "BAND", 3.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(19999.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    void testDictionaryImport()
    {
        tinyxml2::XMLDocument dictionary;
        dictionary.Parse("<TransformationDictionary>"
                         "<DerivedField name=\"derived1\" optype=\"continuous\" dataType=\"double\">"
                         "<Apply function=\"/\">"
                         "<Apply function=\"-\">"
                         "<FieldRef field=\"a\"/>"
                         "<Constant dataType=\"double\">2</Constant>"
                         "</Apply>"
                         "<Constant dataType=\"double\">5</Constant>"
                         "</Apply>"
                         "</DerivedField>"
                         "<DerivedField name=\"derived2\" optype=\"continuous\" dataType=\"double\">"
                         "<Apply function=\"/\">"
                         "<Apply function=\"-\">"
                         "<FieldRef field=\"a\"/>"
                         "<FieldRef field=\"b\"/>"
                         "</Apply>"
                         "<Constant dataType=\"double\">3</Constant>"
                         "</Apply>"
                         "</DerivedField>"
                         "</TransformationDictionary>");

        AstBuilder astBuilder;
        DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, a);
        DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, b);
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(astBuilder.context());
        size_t block_size = 0;
        CPPUNIT_ASSERT(Transformation::parseTransformationDictionary(astBuilder, dictionary.RootElement(), scope, block_size));
        CPPUNIT_ASSERT_EQUAL(size_t(0), block_size);

        tinyxml2::XMLDocument miningSchema;
        miningSchema.Parse("<MiningSchema>"
                           "<MiningField name=\"a\"  missingValueReplacement=\"3\" missingValueTreatment=\"asValue\"/>"
                           "</MiningSchema>");
        {
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), miningSchema.RootElement());
            Transformation::importTransformationDictionary(astBuilder, scope, block_size);

            CPPUNIT_ASSERT_EQUAL(size_t(1), block_size);
            CPPUNIT_ASSERT_EQUAL(static_cast<const PMMLDocument::MiningField *>(nullptr), astBuilder.context().getMiningField("derived2"));

            astBuilder.field(astBuilder.context().getMiningField("derived1"));
        }
        astBuilder.block(2);

        lua_State * L = luaL_newstate();
        parseIntoVM(astBuilder, L);

        executeSimpleQuery(L, "a", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.2, lua_tonumber(L, -1));
        lua_pop(L, 1);

        executeSimpleQuery(L, "a", 12.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(2.0, lua_tonumber(L, -1));
        lua_pop(L, 1);

        // This will read the same value in two schemas. The first is multiplied by 5, the second isn't multiplied.
        // It allows us to ensure that the values will respect the current active schema, nomatter how other many places they are accessed.
        auto accum = scope.addDataField("accum", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_TEMPORARY, PMMLDocument::OPTYPE_CONTINUOUS);
        astBuilder.context().addDefaultMiningField("accum", accum);
        block_size = 0;
        {
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), miningSchema.RootElement());
            Transformation::importTransformationDictionary(astBuilder, scope, block_size);

            CPPUNIT_ASSERT_EQUAL(size_t(1), block_size);
            CPPUNIT_ASSERT_EQUAL(static_cast<const PMMLDocument::MiningField *>(nullptr), astBuilder.context().getMiningField("derived2"));


            astBuilder.field(astBuilder.context().getMiningField("derived1"));
            astBuilder.constant(5);
            astBuilder.function(Function::functionTable.names.times, 2);
            astBuilder.declare(accum, AstBuilder::HAS_INITIAL_VALUE);
            block_size++;
        }
        {
            tinyxml2::XMLDocument newminingSchema;
            newminingSchema.Parse("<MiningSchema>"
                               "<MiningField name=\"a\"  missingValueReplacement=\"12\" missingValueTreatment=\"asValue\"/>"
                               "</MiningSchema>");
            PMMLDocument::MiningSchemaStackGuard schema(astBuilder.context(), newminingSchema.RootElement());
            Transformation::importTransformationDictionary(astBuilder, scope, block_size);

            CPPUNIT_ASSERT_EQUAL(static_cast<const PMMLDocument::MiningField *>(nullptr), astBuilder.context().getMiningField("derived2"));

            astBuilder.field(astBuilder.context().getMiningField("derived1"));
            astBuilder.field(accum);
            astBuilder.function(Function::functionTable.names.plus, 2);
            block_size++;
        }
        astBuilder.block(block_size);
        parseIntoVM(astBuilder, L);

        executeSimpleQuery(L, "a", nullptr);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(3.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
    }

    void testUniqueIdentifier()
    {
        PMMLDocument::ConversionContext context;
        CPPUNIT_ASSERT_EQUAL(std::string("var"), context.createVariable(PMMLDocument::TYPE_STRING, "var")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_1"), context.createVariable(PMMLDocument::TYPE_STRING, "var")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_1_1"), context.createVariable(PMMLDocument::TYPE_STRING, "var_1")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_2"), context.createVariable(PMMLDocument::TYPE_STRING, "var")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_3"), context.createVariable(PMMLDocument::TYPE_STRING, "var_3")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_4"), context.createVariable(PMMLDocument::TYPE_STRING, "var")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("notvar"), context.createVariable(PMMLDocument::TYPE_STRING, "notvar")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("notvar_1"), context.createVariable(PMMLDocument::TYPE_STRING, "notvar")->luaName);
        CPPUNIT_ASSERT_EQUAL(std::string("var_5"), context.createVariable(PMMLDocument::TYPE_STRING, "var")->luaName);
    }
    
    CPPUNIT_TEST_SUITE(TestTransform);
    CPPUNIT_TEST(testDiscretise);
    CPPUNIT_TEST(testDiscretiseDefaultAndMissing);
    CPPUNIT_TEST(testNormContinuous);
    CPPUNIT_TEST(testNormDiscrete);
    CPPUNIT_TEST(testConstant);
    CPPUNIT_TEST(testFieldRef);
    CPPUNIT_TEST(testMapValues);
    CPPUNIT_TEST(testDictionaryImport);
    CPPUNIT_TEST(testUniqueIdentifier);

    CPPUNIT_TEST_SUITE_END();
};
