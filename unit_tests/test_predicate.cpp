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
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "luaconverter/optimiser.hpp"
#include "model/predicate.hpp"
#include "conversioncontext.hpp"
#include "analyser.hpp"

#include <sstream>

struct SimpleTestCase
{
    const char * operatorString;
    const char * field;
    double baseValue;
    double goodValue1;
    double goodValue2;
    double badValue1;
    double badValue2;
};

static const int nSimpleCases = 6;

SimpleTestCase simpleTestCases[nSimpleCases] = {
    {
        "equal",
        "AValue",
        10.5,
        10.5,
        10.5,
        9.0,
        11
    },
    {
        "notEqual",
        "anotherValue",
        11,
        15,
        10,
        11,
        11
    },
    {
        "lessThan",
        "ttttssht",
        10.5,
        9.0,
        10.25,
        10.5,
        12.0
    },
    {
        "lessOrEqual",
        "Gurgle",
        10.5,
        9.0,
        10.5,
        10.55,
        12.0
    },
    {
        "greaterThan",
        "hick",
        10.5,
        10.55,
        12.0,
        9.0,
        10.5
    },
    {
        "greaterOrEqual",
        "hyerk",
        10.5,
        10.5,
        12.0,
        9.0,
        10.25
    }
};

enum
{
    RESULT_TRUE,
    RESULT_FALSE,
    RESULT_MISSING
};
static const char * valueNames[3] = {
    "true",
    "false",
    "unknown"
};
static const int AND_REFERENCE[9] = {
    RESULT_TRUE,
    RESULT_FALSE,
    RESULT_MISSING,
    RESULT_FALSE,
    RESULT_FALSE,
    RESULT_FALSE,
    RESULT_MISSING,
    RESULT_FALSE,
    RESULT_MISSING
};
static const int OR_REFERENCE[9] = {
    RESULT_TRUE,
    RESULT_TRUE,
    RESULT_TRUE,
    RESULT_TRUE,
    RESULT_FALSE,
    RESULT_MISSING,
    RESULT_TRUE,
    RESULT_MISSING,
    RESULT_MISSING
};
static const int XOR_REFERENCE[9] = {
    RESULT_FALSE,
    RESULT_TRUE,
    RESULT_MISSING,
    RESULT_TRUE,
    RESULT_FALSE,
    RESULT_MISSING,
    RESULT_MISSING,
    RESULT_MISSING,
    RESULT_MISSING
};

static int AND(int a, int b)
{
    return AND_REFERENCE[a * 3 + b];
}

static int OR(int a, int b)
{
    return OR_REFERENCE[a * 3 + b];
}

static int XOR(int a, int b)
{
    return XOR_REFERENCE[a * 3 + b];
}

tinyxml2::XMLElement * createSimpleCase(tinyxml2::XMLDocument & xmlDoc, SimpleTestCase & testCase)
{
    tinyxml2::XMLElement * element = xmlDoc.NewElement("SimplePredicate");
    element->SetAttribute("operator", testCase.operatorString);
    element->SetAttribute("field", testCase.field);
    element->SetAttribute("value", testCase.baseValue);
    return element;
}

TEST_CLASS (TestPredicate)
{
public:
	void testSimplePredicate()
    {
        for (int defaultValTestCase = 0; defaultValTestCase < 3; defaultValTestCase++)
        {
            for (int testCase = 0; testCase < nSimpleCases; testCase++)
            {
                SimpleTestCase & thisCase = simpleTestCases[testCase];
                tinyxml2::XMLDocument document;
                tinyxml2::XMLElement * element = createSimpleCase(document, thisCase);

                lua_State * L = luaL_newstate();
                AstBuilder astBuilder;
                PMMLDocument::ScopedVariableDefinitionStackGuard scope(astBuilder.context());
                auto field = scope.addDataField(thisCase.field, PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
                astBuilder.context().addDefaultMiningField(thisCase.field, field);
                CPPUNIT_ASSERT(Predicate::parse(astBuilder, element));
                AstNode builtNode = astBuilder.popNode();
                
                Analyser::AnalyserContext analyserContext;
                {
                    std::stringstream mystream;
                    LuaOutputter outputter(mystream);
                    PMMLDocument::optimiseAST(builtNode, outputter);
                    
                    outputter.keyword("function test() return");
                    LuaConverter::convertAstToLuaWithNullAssertions(analyserContext, builtNode, static_cast<LuaConverter::DefaultIfMissing>(defaultValTestCase), outputter);
                    Analyser::NonNoneAssertionStackGuard assertions(analyserContext);
                    if (defaultValTestCase != LuaConverter::DEFAULT_TO_TRUE)
                    {
                        assertions.addAssertionsForCheck(builtNode, Analyser::ASSUME_NOT_MISSING);
                        CPPUNIT_ASSERT_EQUAL(false, analyserContext.mightVariableBeMissing(*astBuilder.context().getFieldDescription(thisCase.field)));
                    }
                    
                    outputter.keyword("end");
                    if (luaL_dostring(L, mystream.str().c_str()))
                    {
                        std::string message = lua_tostring( L , -1 );
                        CPPUNIT_ASSERT_MESSAGE( message, false );
                    }
                }
                {
                    std::stringstream myNewStream;
                    LuaOutputter outputter(myNewStream);
                    outputter.keyword("function nullity() return");
                    LuaConverter::outputMissing(analyserContext, builtNode, false, outputter);

                    {
                        Analyser::NonNoneAssertionStackGuard nonnullassertions(analyserContext);
                        nonnullassertions.addAssertionsForCheck(builtNode, Analyser::ASSUME_NOT_MISSING);
                        CPPUNIT_ASSERT_EQUAL(false, analyserContext.mightVariableBeMissing(*astBuilder.context().getFieldDescription(thisCase.field)));
                    }

                    outputter.keyword("end");
                    if (luaL_dostring(L, myNewStream.str().c_str()))
                    {
                        std::string message = lua_tostring( L , -1 );
                        CPPUNIT_ASSERT_MESSAGE( message, false );
                    }
                }



                char message[100];
                for (int valueToTry = 0; valueToTry < 5; valueToTry++)
                {
                    bool expectedGood;
                    bool expectedNil = false;
                    switch(valueToTry)
                    {
                        case 0:
                            lua_pushnumber( L, thisCase.badValue1 );
                            expectedGood = false;
                            break;
                        case 1:
                            lua_pushnumber( L, thisCase.badValue2 );
                            expectedGood = false;
                            break;
                        case 2:
                            lua_pushnumber( L, thisCase.goodValue1 );
                            expectedGood = true;
                            break;
                        case 3:
                            lua_pushnumber( L, thisCase.goodValue2 );
                            expectedGood = true;
                            break;
                        default:
                            lua_pushnil( L );
                            // null
                            expectedGood = defaultValTestCase == LuaConverter::DEFAULT_TO_TRUE;
                            expectedNil = defaultValTestCase == LuaConverter::DEFAULT_TO_NIL;
                    }

                    lua_setglobal(L, thisCase.field);
                    sprintf(message, "%s(%i) %s %f", thisCase.field, valueToTry, thisCase.operatorString, thisCase.baseValue);
                    lua_getglobal(L, "test");
                    if (lua_pcall(L, 0, 1, 0))
                    {
                        std::string message = lua_tostring( L , -1 );
                        CPPUNIT_ASSERT_MESSAGE( message, false );
                    }
                    CPPUNIT_ASSERT_EQUAL_MESSAGE(message, expectedGood, bool(lua_toboolean(L, -1)));
                    if (expectedNil)
                    {
                        CPPUNIT_ASSERT_MESSAGE(message, lua_isnil(L, -1));
                    }
                    
                    lua_pop(L, 1);
                    lua_getglobal(L, "nullity");
                    if (lua_pcall(L, 0, 1, 0))
                    {
                        std::string message = lua_tostring( L , -1 );
                        CPPUNIT_ASSERT_MESSAGE( message, false );
                    }
                    CPPUNIT_ASSERT_EQUAL_MESSAGE(message, bool(valueToTry == 4), bool(lua_toboolean(L, -1)));
                    lua_pop(L, 1);
                }

                lua_close(L);
            }
        }
    }
    
    template <int (*F)(int,int,int,int)>
    void testFourArgumentComplexExpression(tinyxml2::XMLElement * element)
    {
        AstBuilder astBuilder;
        PMMLDocument::ConversionContext & conversionContext = astBuilder.context();
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(conversionContext);
        for (int testCase = 0; testCase < nSimpleCases; testCase++)
        {
            SimpleTestCase & thisCase = simpleTestCases[testCase];
            auto field = scope.addDataField(thisCase.field, PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
            conversionContext.addDefaultMiningField(thisCase.field, field);
        }
        
        CPPUNIT_ASSERT(Predicate::parse(astBuilder, element));
        
        // Find out which inputs may have been null for what outputs, so we can verify our static analyser
        bool mightBeNull[3 /*outputs*/][4 /*inputs*/] = {{false, false, false, false}, {false, false, false, false}, {false, false, false, false}};
        
        for (int defaultValTestCase = 0; defaultValTestCase < 3; defaultValTestCase++)
        {
            lua_State * L = luaL_newstate();
            PMMLDocument::ScopedVariableDefinitionStackGuard scope(conversionContext);
            for (int simpleCase = 0; simpleCase < 4; simpleCase++)
            {
                scope.addDataField(simpleTestCases[simpleCase].field, PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
            }

            {
                std::stringstream mystream;
                LuaOutputter outputter(mystream);
                outputter.keyword("function test() return");
                
                Analyser::AnalyserContext contextForNull;
                LuaConverter::convertAstToLuaWithNullAssertions(contextForNull, astBuilder.topNode(),
                                              static_cast<LuaConverter::DefaultIfMissing>(defaultValTestCase), outputter);
                outputter.keyword("end");
                if (luaL_dostring(L, mystream.str().c_str()))
                {
                    std::string message = lua_tostring( L , -1 );
                    CPPUNIT_ASSERT_MESSAGE( message, false );
                }
                printf("%s\n", mystream.str().c_str());
            }

            // This doesn't depend on the default, so only do it once.
            if (defaultValTestCase != LuaConverter::DEFAULT_TO_NIL)
            {
                std::stringstream nullFunctionStream;
                LuaOutputter outputter(nullFunctionStream);
                outputter.keyword("function nullity() return");
                Analyser::AnalyserContext contextForNull;
                LuaConverter::outputMissing(contextForNull, astBuilder.topNode(), defaultValTestCase == LuaConverter::DEFAULT_TO_TRUE, outputter);
                
                outputter.keyword("end");
                
                if (luaL_dostring(L, nullFunctionStream.str().c_str()))
                {
                    std::string message = lua_tostring( L , -1 );
                    CPPUNIT_ASSERT_MESSAGE( message, false );
                }
                printf("%s\n", nullFunctionStream.str().c_str());
            }
            
            // 3 to the power of 4 combinations to test
            for (int i = 0; i < 3 * 3 * 3 * 3; i++)
            {
                int states[4];
                int value = i;
                for (int simpleCase = 0; simpleCase < 4; simpleCase++)
                {
                    int thisState = value % 3;
                    states[simpleCase] = thisState;
                    value /= 3;
                    SimpleTestCase & thisCase = simpleTestCases[simpleCase];

                    switch(thisState)
                    {
                        case RESULT_TRUE:
                            lua_pushnumber( L, thisCase.goodValue1 );
                            break;
                        case RESULT_FALSE:
                            lua_pushnumber( L, thisCase.badValue1 );
                            break;
                        default:
                            lua_pushnil( L );
                            // null
                    }
                    lua_setglobal(L, thisCase.field);
                }
                char message[100];
                int referenceValue = F(states[0], states[1], states[2], states[3]);
                sprintf(message, "%i %s %s %s %s -> %s\n", i, valueNames[states[0]], valueNames[states[1]], valueNames[states[2]], valueNames[states[3]], valueNames[referenceValue]);
                
                lua_getglobal(L, "test");
                if (lua_pcall(L, 0, 1, 0))
                {
                    std::string message = lua_tostring( L , -1 );
                    CPPUNIT_ASSERT_MESSAGE( message, false );
                }
                
                int expectedValue = referenceValue;
                if (referenceValue == RESULT_MISSING && defaultValTestCase != LuaConverter::DEFAULT_TO_NIL)
                {
                    expectedValue = defaultValTestCase == LuaConverter::DEFAULT_TO_TRUE ? RESULT_TRUE : RESULT_FALSE;
                }
                
                int actualValue = lua_toboolean(L, -1) ? RESULT_TRUE : RESULT_FALSE;
                // DEFAULT_TO_FALSE doesn't distinguish between nil and false either way
                if (defaultValTestCase != LuaConverter::DEFAULT_TO_FALSE && lua_isnil(L, -1))
                {
                    actualValue = RESULT_MISSING;
                }

                CPPUNIT_ASSERT_EQUAL_MESSAGE(message, expectedValue, actualValue);
                lua_pop(L, 1);

                // This tests the nullity function. It's independent of the defaultValue thing... it's inverted if
                if (defaultValTestCase != LuaConverter::DEFAULT_TO_NIL)
                {
                    lua_getglobal(L, "nullity");
                    if (lua_pcall(L, 0, 1, 0))
                    {
                        std::string message = lua_tostring( L , -1 );
                        CPPUNIT_ASSERT_MESSAGE( message, false );
                    }
                    
                    const bool isInverted = defaultValTestCase == LuaConverter::DEFAULT_TO_TRUE;
                    const bool expectingMissing = referenceValue == RESULT_MISSING;
                    const bool gotMissing = bool(lua_toboolean(L, -1)) != isInverted;
                    if (!isInverted)
                    {
                        // Non-inverted nullity always returns a bool. Inverted returns a bool for not-missing
                        CPPUNIT_ASSERT( lua_isboolean( L, -1 ) );
                    }
                    else if (gotMissing)
                    {
                        // Inverted nullity returns a nil for missing
                        CPPUNIT_ASSERT( lua_isnil( L, -1 ) );
                    }
                    
                    CPPUNIT_ASSERT_EQUAL_MESSAGE(message, expectingMissing, gotMissing);
                    lua_pop(L, 1);
                }
                else
                {
                    // Build input missing value to output mapping (only needs to happen once)
                    for (int simpleCase = 0; simpleCase < 4; simpleCase++)
                    {
                        if (states[simpleCase] == RESULT_MISSING)
                        {
                            mightBeNull[referenceValue][simpleCase] = true;
                        }
                    }
                }
            }

            lua_close(L);
        }
        
        Analyser::AnalyserContext context;
        for (int assumptionTestCase = 0; assumptionTestCase < 3; assumptionTestCase++)
        {
            Analyser::Assumption mapping[] = {Analyser::ASSUME_TRUE, Analyser::ASSUME_FALSE, Analyser::ASSUME_MISSING};
            Analyser::NonNoneAssertionStackGuard guard(context);
            guard.addAssertionsForCheck(astBuilder.topNode(), mapping[assumptionTestCase] );
            for (int simpleCase = 0; simpleCase < 4; simpleCase++)
            {
                CPPUNIT_ASSERT_EQUAL(mightBeNull[assumptionTestCase][simpleCase], context.mightVariableBeMissing(*conversionContext.getFieldDescription(simpleTestCases[simpleCase].field)));
            }
        }
    }

    static int ANDANDAND(int a, int b, int c, int d)
    {
        return AND(a, AND(b, AND(c, d)));
    }

    static int OROROR(int a, int b, int c, int d)
    {
        return OR(a, OR(b, OR(c, d)));
    }

    static int XORXORXOR(int a, int b, int c, int d)
    {
        return XOR(a, XOR(b, XOR(c, d)));
    }

    static int ANDOROR(int a, int b, int c, int d)
    {
        return AND(OR(a, b), OR(c, d));
    }

    static int ORANDAND(int a, int b, int c, int d)
    {
        return OR(AND(a, b), AND(c, d));
    }

    static int ANDORORn(int a, int b, int c, int d)
    {
        return AND(OR(OR(a, b), c), d);
    }

    static int ANDORAND(int a, int b, int c, int d)
    {
        return AND(OR(a, AND(b, c)), d);
    }

    static int ORANDXOR(int a, int b, int c, int d)
    {
        return OR(AND(a, b), XOR(c, d));
    }

    void testCompoundAndPredicate()
    {
        tinyxml2::XMLDocument document;

        tinyxml2::XMLElement * andandand = document.NewElement("CompoundPredicate");
        andandand->SetAttribute("booleanOperator", "and");
        andandand->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        andandand->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));
        andandand->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
        andandand->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));
        testFourArgumentComplexExpression<ANDANDAND>(andandand);
    }

    void testCompoundOrPredicate()
    {
        tinyxml2::XMLDocument document;

        tinyxml2::XMLElement * ororor = document.NewElement("CompoundPredicate");
        ororor->SetAttribute("booleanOperator", "or");
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));
        testFourArgumentComplexExpression<OROROR>(ororor);
    }

    void testCompoundXorPredicate()
    {
        tinyxml2::XMLDocument document;

        tinyxml2::XMLElement * ororor = document.NewElement("CompoundPredicate");
        ororor->SetAttribute("booleanOperator", "xor");
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
        ororor->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));
        testFourArgumentComplexExpression<XORXORXOR>(ororor);
    }
    void testMixedCompound()
    {
        tinyxml2::XMLDocument document;
        {
            tinyxml2::XMLElement * or1 = document.NewElement("CompoundPredicate");
            or1->SetAttribute("booleanOperator", "or");
            or1->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
            or1->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));

            tinyxml2::XMLElement * or2 = document.NewElement("CompoundPredicate");
            or2->SetAttribute("booleanOperator", "or");
            or2->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
            or2->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

            tinyxml2::XMLElement * andoror = document.NewElement("CompoundPredicate");
            andoror->SetAttribute("booleanOperator", "and");
            andoror->InsertEndChild(or1);
            andoror->InsertEndChild(or2);
            testFourArgumentComplexExpression<ANDOROR>(andoror);
        }

        {
            tinyxml2::XMLElement * or3 = document.NewElement("CompoundPredicate");
            or3->SetAttribute("booleanOperator", "or");
            or3->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
            or3->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));
            
            tinyxml2::XMLElement * or4 = document.NewElement("CompoundPredicate");
            or4->SetAttribute("booleanOperator", "or");
            or4->InsertEndChild(or3);
            or4->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
            
            tinyxml2::XMLElement * andoror2 = document.NewElement("CompoundPredicate");
            andoror2->SetAttribute("booleanOperator", "and");
            andoror2->InsertEndChild(or4);
            andoror2->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));
            testFourArgumentComplexExpression<ANDORORn>(andoror2);
        }
        
        {
            tinyxml2::XMLElement * and1 = document.NewElement("CompoundPredicate");
            and1->SetAttribute("booleanOperator", "and");
            and1->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
            and1->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));

            tinyxml2::XMLElement * and2 = document.NewElement("CompoundPredicate");
            and2->SetAttribute("booleanOperator", "and");
            and2->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
            and2->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

            tinyxml2::XMLElement * orandand = document.NewElement("CompoundPredicate");
            orandand->SetAttribute("booleanOperator", "or");
            orandand->InsertEndChild(and1);
            orandand->InsertEndChild(and2);
            testFourArgumentComplexExpression<ORANDAND>(orandand);
        }
        
        {
            tinyxml2::XMLElement * and3 = document.NewElement("CompoundPredicate");
            and3->SetAttribute("booleanOperator", "and");
            and3->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));
            and3->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));

            tinyxml2::XMLElement * andor = document.NewElement("CompoundPredicate");
            andor->SetAttribute("booleanOperator", "or");
            andor->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
            andor->InsertEndChild(and3);

            tinyxml2::XMLElement * andorand = document.NewElement("CompoundPredicate");
            andorand->SetAttribute("booleanOperator", "and");
            andorand->InsertEndChild(andor);
            andorand->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));
            testFourArgumentComplexExpression<ANDORAND>(andorand);
        }

        {
            tinyxml2::XMLElement * and4 = document.NewElement("CompoundPredicate");
            and4->SetAttribute("booleanOperator", "and");
            and4->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
            and4->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));

            tinyxml2::XMLElement * xor1 = document.NewElement("CompoundPredicate");
            xor1->SetAttribute("booleanOperator", "xor");
            xor1->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
            xor1->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

            tinyxml2::XMLElement * orandxor = document.NewElement("CompoundPredicate");
            orandxor->SetAttribute("booleanOperator", "or");
            orandxor->InsertEndChild(and4);
            orandxor->InsertEndChild(xor1);
            testFourArgumentComplexExpression<ORANDXOR>(orandxor);
        }
    }


    static int SURROGATE_TEST_CASE(int a, int b, int c, int d)
    {
        int value = OR(AND(a, b), XOR(c, d));
        if (value != RESULT_MISSING)
        {
            return value;
        }
        value = OR(AND(a, b), d);
        if (value != RESULT_MISSING)
        {
            return value;
        }
        value = AND(a, d);
        if (value != RESULT_MISSING)
        {
            return value;
        }
        if (c != RESULT_MISSING)
        {
            return c;
        }
        return RESULT_MISSING;
    }

    void testSurrogate()
    {
        tinyxml2::XMLDocument document;
        // First case
        tinyxml2::XMLElement * and1 = document.NewElement("CompoundPredicate");
        and1->SetAttribute("booleanOperator", "and");
        and1->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        and1->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));

        tinyxml2::XMLElement * xor1 = document.NewElement("CompoundPredicate");
        xor1->SetAttribute("booleanOperator", "xor");
        xor1->InsertEndChild(createSimpleCase(document, simpleTestCases[2]));
        xor1->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

        tinyxml2::XMLElement * orandxor = document.NewElement("CompoundPredicate");
        orandxor->SetAttribute("booleanOperator", "or");
        orandxor->InsertEndChild(and1);
        orandxor->InsertEndChild(xor1);

        // Second case
        tinyxml2::XMLElement * and2 = document.NewElement("CompoundPredicate");
        and2->SetAttribute("booleanOperator", "and");
        and2->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        and2->InsertEndChild(createSimpleCase(document, simpleTestCases[1]));

        tinyxml2::XMLElement * orand = document.NewElement("CompoundPredicate");
        orand->SetAttribute("booleanOperator", "or");
        orand->InsertEndChild(and2);
        orand->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

        // Third case
        tinyxml2::XMLElement * and3 = document.NewElement("CompoundPredicate");
        and3->SetAttribute("booleanOperator", "and");
        and3->InsertEndChild(createSimpleCase(document, simpleTestCases[0]));
        and3->InsertEndChild(createSimpleCase(document, simpleTestCases[3]));

        // Fourth case
        tinyxml2::XMLElement * simple = createSimpleCase(document, simpleTestCases[2]);

        tinyxml2::XMLElement * surrogate = document.NewElement("CompoundPredicate");
        surrogate->SetAttribute("booleanOperator", "surrogate");
        surrogate->InsertEndChild(orandxor);
        surrogate->InsertEndChild(orand);
        surrogate->InsertEndChild(and3);
        surrogate->InsertEndChild(simple);

        testFourArgumentComplexExpression<SURROGATE_TEST_CASE>(surrogate);
    }


    bool executeSimpleQuery(lua_State * L, const char * function, const char * field, const char * value)
    {
        if (value)
        {
            lua_pushstring(L, value);
        }
        else
        {
            lua_pushnil(L);
        }
        lua_setglobal(L, field);
        lua_getglobal(L, function);
        if (lua_pcall(L, 0, 1, 0))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }
        bool output = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return output;
    }

    void testSimpleSet()
    {
        std::stringstream mystream;
        Analyser::AnalyserContext analyserContext;
        Analyser::NonNoneAssertionStackGuard nonnullassertions(analyserContext);

        tinyxml2::XMLDocument document;
        document.Parse("<SimpleSetPredicate field=\"model_year\" booleanOperator=\"isIn\">"
                       "<Array n=\"10\" type=\"string\">&quot;70&quot;   &quot;71&quot;   &quot;72&quot;   &quot;73&quot;   &quot;74&quot;   &quot;75&quot;   &quot;76&quot;   &quot;77&quot;   &quot;78&quot;   &quot;80&quot;</Array>"
                       "</SimpleSetPredicate>");
        LuaOutputter outputter(mystream);
        outputter.keyword("function test() return");
        AstBuilder astBuilder;
        
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(astBuilder.context());
        auto field = scope.addDataField("model_year", PMMLDocument::TYPE_STRING, PMMLDocument::ORIGIN_DATA_DICTIONARY, PMMLDocument::OPTYPE_CONTINUOUS);
        astBuilder.context().addDefaultMiningField("model_year", field);
        
        CPPUNIT_ASSERT_EQUAL(true, Predicate::parse(astBuilder, document.RootElement()));
        LuaConverter::convertAstToLua(astBuilder.topNode(), outputter);
        outputter.keyword("end");
        
        lua_State * L = luaL_newstate();
        if (luaL_dostring(L, mystream.str().c_str()))
        {
            std::string message = lua_tostring( L , -1 );
            CPPUNIT_ASSERT_MESSAGE( message, false );
        }

        CPPUNIT_ASSERT_EQUAL(true, executeSimpleQuery(L, "test", "model_year", "78"));
        CPPUNIT_ASSERT_EQUAL(false, executeSimpleQuery(L, "test", "model_year", "12"));
        CPPUNIT_ASSERT_EQUAL(false, executeSimpleQuery(L, "test", "model_year", nullptr));
    }

    CPPUNIT_TEST_SUITE(TestPredicate);
    CPPUNIT_TEST(testSimplePredicate);
    CPPUNIT_TEST(testCompoundAndPredicate);
    CPPUNIT_TEST(testCompoundOrPredicate);
    CPPUNIT_TEST(testCompoundXorPredicate);
    CPPUNIT_TEST(testMixedCompound);
    CPPUNIT_TEST(testSurrogate);
    CPPUNIT_TEST(testSimpleSet);
    CPPUNIT_TEST_SUITE_END();
};
