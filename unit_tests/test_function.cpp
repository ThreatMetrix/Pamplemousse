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
//  Created by Caleb Moore on 12/11/18.
//

#include "Cuti.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "function.hpp"
#include "analyser.hpp"
#include "tinyxml2.h"
#include "model/transformation.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/optimiser.hpp"
#include "testutils.hpp"

TEST_CLASS (TestFunction)
{
public:
    void parseIntoVM(AstBuilder & astBuilder, const std::string & globalStuff, lua_State * L)
    {
        std::string functionPrologue;
        std::stringstream functionProloguestream;
        LuaOutputter outputter(functionProloguestream);
        Analyser::AnalyserContext context;
        if (Function::prologue(astBuilder))
        {
            LuaConverter::convertAstToLuaWithNullAssertions(context, astBuilder.topNode(), LuaConverter::DEFAULT_TO_NIL, outputter);
            functionPrologue = functionProloguestream.str();
            astBuilder.popNode();
        }
        AstNode node = astBuilder.popNode();
        // Always run the optimiser here. It shouldn't make any functional difference, but it is important to make sure it's not breaking anything.
        PMMLDocument::optimiseAST(node, outputter);
        astBuilder.pushNode(node);
        {
            std::stringstream mystream;
            mystream << globalStuff;
            mystream << functionPrologue;
            LuaOutputter outputter(mystream);
            outputter.keyword("function nullity() return");
            LuaConverter::outputMissing(context, astBuilder.topNode(), false, outputter);
            outputter.keyword("end");
            
            printf("%s\n", mystream.str().c_str());
            if (luaL_dostring(L, mystream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
        {
            std::stringstream mystream;
            mystream << globalStuff;
            mystream << functionPrologue;
            LuaOutputter outputter(mystream);
            outputter.keyword("function inverseNullity() return");
            LuaConverter::outputMissing(context, astBuilder.topNode(), true, outputter);
            outputter.keyword("end");
            
            printf("%s\n", mystream.str().c_str());
            if (luaL_dostring(L, mystream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
        {
            std::stringstream mystream;
            mystream << globalStuff;
            mystream << functionPrologue;
            LuaOutputter outputter(mystream);
            outputter.keyword("function test() return");
            LuaConverter::convertAstToLuaWithNullAssertions(context, astBuilder.topNode(), LuaConverter::DEFAULT_TO_NIL, outputter);
            outputter.keyword("end");
            
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
        bool isInverseNullityNull = lua_isnil(L, -1);
        lua_pop(L, 1);
        
        lua_getglobal(L, "test");
        if (lua_pcall(L, 0, 1, 0))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }
        bool resultIsNull = lua_isnil(L, -1);
        
        CPPUNIT_ASSERT_EQUAL(resultIsNull, isNullityTrue);
        CPPUNIT_ASSERT_EQUAL(resultIsNull, isInverseNullityNull);
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
    
    void testIf()
    {
        lua_State * L = luaL_newstate();
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"if\">"
                           "<Apply function=\"isIn\">"
                           "<FieldRef field=\"color\"/>"
                           "<Constant dataType=\"string\">red</Constant>"
                           "<Constant dataType=\"string\">green</Constant>"
                           "<Constant dataType=\"string\">blue</Constant>"
                           "</Apply>"
                           "<Constant dataType=\"string\">primary</Constant>"
                           "<Constant dataType=\"string\">other</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, emission);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForcolor));
        }
        executeSimpleQuery(L, "color", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "color", "red");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("primary"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("other"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"if\">"
                           "<Apply function=\"equal\">"
                           "<FieldRef field=\"color\"/>"
                           "<Constant>red</Constant>"
                           "</Apply>"
                           "<Apply function=\"+\"><FieldRef field=\"emission\"/><Constant>5</Constant></Apply>"
                           "<FieldRef field=\"emission\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, emission);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
        }
        executeSimpleQuery(L, "color", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "color", "red");
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        lua_pushnumber(L, 5);
        lua_setglobal(L, "emission");
        
        executeSimpleQuery(L, "color", "red");
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(10.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        // Test type coersion
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"if\">"
                           "<Apply function=\"notEqual\">"
                           "<FieldRef field=\"color\"/>"
                           "<Constant>red</Constant>"
                           "</Apply>"
                           "<Constant>Some slow colour</Constant>"
                           "<Constant>9000</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, emission);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Some slow colour"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "color", "red");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("9000"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        // Test one sided if
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"if\">"
                           "<Apply function=\"equal\">"
                           "<FieldRef field=\"color\"/>"
                           "<Constant>blue</Constant>"
                           "</Apply>"
                           "<Constant>Cool</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, emission);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForcolor));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Cool"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "color", "red");
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
    }
    
    void testMissingAndDefault()
    {
        lua_State * L = luaL_newstate();
        tinyxml2::XMLDocument document;
        document.Parse("<Apply function=\"if\">"
                       "<Apply function=\"equal\">"
                       "<FieldRef field=\"color\"/>"
                       "<Constant>purple</Constant>"
                       "</Apply>"
                       "<FieldRef field=\"description\"/>"
                       "<Constant>Not Purple</Constant>"
                       "</Apply>");
        
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, description);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForcolor));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForcolor, fieldFordescription));
            parseIntoVM(astBuilder, "", L);
            
        }
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(!lua_isnil(L, -1));

        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        
        document.RootElement()->SetAttribute("defaultValue", "violet");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, description);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Not Purple"), std::string(lua_tostring(L, -1)));

        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("violet"), std::string(lua_tostring(L, -1)));
        
        document.RootElement()->SetAttribute("mapMissingTo", "mauve");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, description);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
        }
        
        // description is still missing
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("mauve"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("mauve"), std::string(lua_tostring(L, -1)));
        
        // More complex this time. "And" expression is only missing if it can be either known or unknown.
        tinyxml2::XMLDocument document2;
        document2.Parse("<Apply function=\"if\" mapMissingTo=\"unknown colour\">"
                           "<Apply function=\"and\">"
                           "<Apply function=\"equal\">"
                           "<FieldRef field=\"color\"/>"
                           "<Constant>purple</Constant>"
                           "</Apply>"
                           "<Apply function=\"equal\">"
                           "<FieldRef field=\"description\"/>"
                           "<Constant>violet</Constant>"
                           "</Apply>"
                           "</Apply>"
                           "<Constant>Purple</Constant>"
                           "<Constant>Not Purple</Constant>"
                           "</Apply>");
        
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, description);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document2.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Not Purple"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("unknown colour"), std::string(lua_tostring(L, -1)));
        
        lua_pushstring(L, "violet");
        lua_setglobal(L, "description");
        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Purple"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "blue");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Not Purple"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("unknown colour"), std::string(lua_tostring(L, -1)));
        
        // This can be known without a missing value
        tinyxml2::XMLDocument document3;
        document3.Parse("<Apply function=\"if\" mapMissingTo=\"unspecified colour\">"
                        "<Apply function=\"equal\">"
                        "<FieldRef field=\"color\"/>"
                        "<Constant>purple</Constant>"
                        "</Apply>"
                        "<Constant>Royal</Constant>"
                        "</Apply>");
        
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document3.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForcolor));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("unspecified colour"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "Yellow");
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        
        
        document3.RootElement()->SetAttribute("defaultValue", "unknown colour");
        {
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, color);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document3.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "color", nullptr);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("unspecified colour"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "Yellow");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("unknown colour"), std::string(lua_tostring(L, -1)));
        
        executeSimpleQuery(L, "color", "purple");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Royal"), std::string(lua_tostring(L, -1)));
    }
    
    void testUserDefinedFunction()
    {
        AstBuilder astBuilder;
        PMMLDocument::ScopedVariableDefinitionStackGuard variables(astBuilder.context());
        auto someSillyVariable = variables.addDataField("someSillyVariable", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
        auto input = variables.addDataField("input", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
        astBuilder.context().addDefaultMiningField("someSillyVariable", someSillyVariable);
        astBuilder.context().addDefaultMiningField("input", input);
        
        std::stringstream globalStream;
        lua_State * L = luaL_newstate();
        {
            tinyxml2::XMLDocument document;
            document.Parse("<TransformationDictionary>"
                           "<DefineFunction name=\"AMPM\" dataType=\"string\" optype=\"categorical\">"
                           "<ParameterField name=\"TimeVal\" optype=\"continuous\" dataType=\"integer\"/>"
                           "<Discretize field=\"TimeVal\" defaultValue=\"AM\">"
                           "<DiscretizeBin binValue=\"AM\">"
                           "<Interval closure=\"closedClosed\" leftMargin=\"0\" rightMargin=\"43199\"/>"
                           "</DiscretizeBin>"
                           "<DiscretizeBin binValue=\"PM\">"
                           "<Interval closure=\"closedOpen\" leftMargin=\"43200\" rightMargin=\"86400\"/>"
                           "</DiscretizeBin>"
                           "</Discretize>"
                           "</DefineFunction>"
                           "<DefineFunction name=\"1contrived-Function\" dataType=\"integer\" optype=\"categorical\">"
                           "<ParameterField name=\"param1\" optype=\"continuous\" dataType=\"integer\"/>"
                           "<ParameterField name=\"param2\" optype=\"continuous\" dataType=\"integer\"/>"
                           "<Apply function=\"if\">"
                           "<Apply function=\"lessThan\">"
                           "<FieldRef field=\"param1\"/>"
                           "<Constant>4</Constant>"
                           "</Apply>"
                           "<FieldRef field=\"param2\"/>"
                           "<FieldRef field=\"someSillyVariable\"/>"
                           "</Apply>"
                           "</DefineFunction>"
                           "</TransformationDictionary>");
            
            size_t blockSize = 0;
            CPPUNIT_ASSERT(Transformation::parseTransformationDictionary(astBuilder, document.RootElement(), variables, blockSize));
            astBuilder.block(blockSize);

            LuaOutputter outputter(globalStream);
            Analyser::AnalyserContext analyserContext;
            LuaConverter::convertAstToLuaWithNullAssertions(analyserContext, astBuilder.topNode(), LuaConverter::DEFAULT_TO_NIL, outputter);
            printf("%s\n", globalStream.str().c_str());
            if (luaL_dostring(L, globalStream.str().c_str()))
            {
                std::string message = lua_tostring( L , -1 );
                CPPUNIT_ASSERT_MESSAGE( message, false );
            }
        }
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"AMPM\"><FieldRef field=\"input\" /></Apply>");
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            parseIntoVM(astBuilder, globalStream.str(), L);
            
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), input));
            
            astBuilder.popNode();
        }
        
        executeSimpleQuery(L, "input", 50000.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("PM"), std::string(lua_tostring(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "input", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"1contrived-Function\"><FieldRef field=\"input\" /><Constant>7</Constant></Apply>");
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode(), input));
            parseIntoVM(astBuilder, globalStream.str(), L);
            astBuilder.popNode();
        }
        
        lua_pushnumber(L, 12);
        lua_setglobal(L, "someSillyVariable");
        
        executeSimpleQuery(L, "input", 50000.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(12.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "input", 3.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(7.0, lua_tonumber(L, -1));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "input", nullptr);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
        
        lua_pushnil(L);
        lua_setglobal(L, "someSillyVariable");
        
        executeSimpleQuery(L, "input", 40.0);
        CPPUNIT_ASSERT(lua_isnil(L, -1));
        lua_pop(L, 1);
    }

    void testComparison()
    {
        lua_State * L = luaL_newstate();
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"equal\">"
                           "<FieldRef field=\"year\"/>"
                           "<Constant>1995</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, year);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForyear));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "year", "1995");
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "year", "1997");
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"isIn\">"
                           "<FieldRef field=\"year\"/>"
                           "<Constant>2004</Constant>"
                           "<Constant>2008</Constant>"
                           "<Constant>2012</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, year);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForyear));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "year", "2008");
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
        
        executeSimpleQuery(L, "year", "2009");
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);

        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"isMissing\">"
                           "<FieldRef field=\"year\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, year);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }

        executeSimpleQuery(L, "year", nullptr);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);

        executeSimpleQuery(L, "year", 1975.0);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);

        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"isNotMissing\">"
                           "<FieldRef field=\"year\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, year);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_BOOL, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }

        executeSimpleQuery(L, "year", nullptr);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(false, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);

        executeSimpleQuery(L, "year", 1975.0);
        CPPUNIT_ASSERT(lua_isboolean(L, -1));
        CPPUNIT_ASSERT_EQUAL(true, bool(lua_toboolean(L, -1)));
        lua_pop(L, 1);
    }
    
    void testConcat()
    {
        lua_State * L = luaL_newstate();
        luaopen_base ( L );
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"concat\">"
                           "<FieldRef field=\"month\"/>"
                           "<Constant>-</Constant>"
                           "<FieldRef field=\"year\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            PMMLDocument::ScopedVariableDefinitionStackGuard variables(astBuilder.context());
            auto year = variables.addDataField("year", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
            auto month = variables.addDataField("month", PMMLDocument::TYPE_STRING, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
            astBuilder.context().addDefaultMiningField("year", year);
            astBuilder.context().addDefaultMiningField("month", month);

            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), year, month));
            parseIntoVM(astBuilder, "", L);
        }
        
        lua_pushinteger(L, 2011);
        lua_setglobal(L, "year");
        
        executeSimpleQuery(L, "month", "December");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("December-2011"), std::string(lua_tostring(L, -1)));
    }

    void testStringOps()
    {
        lua_State * L = luaL_newstate();
        luaL_openlibs(L);
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"substring\">"
                           "<FieldRef field=\"Str\"/>"
                           "<Constant dataType=\"integer\">2</Constant>"
                           "<Constant dataType=\"integer\">3</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, Str);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForStr));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "Str", "aBc9x");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("Bc9"), std::string(lua_tostring(L, -1)));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"uppercase\">"
                           "<FieldRef field=\"Str\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, Str);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForStr));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "Str", "aBc9");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("ABC9"), std::string(lua_tostring(L, -1)));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"lowercase\">"
                           "<FieldRef field=\"Str\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, Str);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForStr));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "Str", "aBc9");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("abc9"), std::string(lua_tostring(L, -1)));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"trimBlanks\">"
                           "<FieldRef field=\"Str\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_STRING_VARIABLE_INTO_SCOPE(astBuilder, Str);
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            
            CPPUNIT_ASSERT(TestUtils::mightBeMissingWith(astBuilder.topNode()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            CPPUNIT_ASSERT(!TestUtils::mightBeMissingWith(astBuilder.topNode(), fieldForStr));
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "Str", " aBc9x ");
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("aBc9x"), std::string(lua_tostring(L, -1)));
    }
    
    void testNumericOps()
    {
        lua_State * L = luaL_newstate();
        luaL_openlibs(L);

        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"min\">"
                           "<Constant dataType=\"integer\">5</Constant>"
                           "<Constant dataType=\"integer\">2</Constant>"
                           "<Constant dataType=\"integer\">3</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(2.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"max\">"
                           "<Constant dataType=\"integer\">5</Constant>"
                           "<Constant dataType=\"integer\">2</Constant>"
                           "<Constant dataType=\"integer\">3</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(5.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"avg\">"
                           "<Constant dataType=\"integer\">7</Constant>"
                           "<Constant dataType=\"integer\">2</Constant>"
                           "<Constant dataType=\"integer\">3</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"threshold\">"
                           "<FieldRef field=\"number\"/>"
                           "<Constant dataType=\"integer\">2</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 3.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 1.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(0.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"round\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 1.2);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 3.8);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"floor\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 1.2);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(1.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 3.8);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(3.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 2.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(2.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"ceil\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 1.2);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(2.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 3.8);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(4.0, lua_tonumber(L, -1));
        
        executeSimpleQuery(L, "number", 5.0);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_EQUAL(5.0, lua_tonumber(L, -1));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"formatNumber\">"
                           "<FieldRef field=\"number\"/>"
                           "<Constant>%3d</Constant>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_STRING, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 2.0);
        CPPUNIT_ASSERT(lua_isstring(L, -1));
        CPPUNIT_ASSERT_EQUAL(std::string("  2"), std::string(lua_tostring(L, -1)));
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"erf\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 0.4);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.428392355, lua_tonumber(L, -1), 0.0001);
        
        executeSimpleQuery(L, "number", 1.3);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.934007945, lua_tonumber(L, -1), 0.0001);
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"stdNormalCDF\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        
        executeSimpleQuery(L, "number", -1.959964);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.025, lua_tonumber(L, -1), 0.001);
        
        {
            tinyxml2::XMLDocument document;
            document.Parse("<Apply function=\"stdNormalIDF\">"
                           "<FieldRef field=\"number\"/>"
                           "</Apply>");
            AstBuilder astBuilder;
            DEFINE_NUMERIC_VARIABLE_INTO_SCOPE(astBuilder, number);
            
            CPPUNIT_ASSERT(Transformation::parse(astBuilder, document.RootElement()));
            CPPUNIT_ASSERT_EQUAL(PMMLDocument::TYPE_NUMBER, astBuilder.topNode().type);
            parseIntoVM(astBuilder, "", L);
        }
        
        executeSimpleQuery(L, "number", 0.025);
        CPPUNIT_ASSERT(lua_isnumber(L, -1));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.959964, lua_tonumber(L, -1), 0.001);
    }
    
    CPPUNIT_TEST_SUITE(TestFunction);
    CPPUNIT_TEST(testIf);
    CPPUNIT_TEST(testMissingAndDefault);
    CPPUNIT_TEST(testUserDefinedFunction);
    CPPUNIT_TEST(testComparison);
    CPPUNIT_TEST(testConcat);
    CPPUNIT_TEST(testStringOps);
    CPPUNIT_TEST(testNumericOps);

    CPPUNIT_TEST_SUITE_END();
};
