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

#include "Cuti.h"

#include "document.hpp"

#include "testutils.hpp"
using namespace TestUtils;


TEST_CLASS (TestScorecard)
{
public:
    void testCharacteristicReasonCode()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SampleScorcard.pmml").c_str()));
        lua_State * L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);

        CPPUNIT_ASSERT(executeModel(L, "department", "engineering", "age", 30, "income", 500));
        double score;
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(41.0, score);
        std::string reasonCode;
        CPPUNIT_ASSERT(getValue(L, "Reason Code 1", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC1"), reasonCode);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC2"), reasonCode);
        CPPUNIT_ASSERT(!getValue(L, "Reason Code 3", reasonCode));
    }

    void testAttributeReasonCode()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SampleScorecard-Attribute.pmml").c_str()));
        lua_State * L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "department", "engineering", "age", 29, "income", 500));
        double score;
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(29.0, score);
        std::string reasonCode;
        CPPUNIT_ASSERT(getValue(L, "Reason Code 1", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC2_3"), reasonCode);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC1"), reasonCode);
        CPPUNIT_ASSERT(!getValue(L, "Reason Code 3", reasonCode));
    }

    void testComplexPartialScore()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SampleScorcard.pmml").c_str()));
        lua_State * L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        // 13 below baseline
        const double DEPARTMENT_SCORE = 6;
        // 6 below baseline
        const double AGE_SCORE = 12;
        double score;
        std::string reasonCode;
        CPPUNIT_ASSERT(executeModel(L, "department", "business", "age", 32, "income", nullptr));
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(DEPARTMENT_SCORE + AGE_SCORE + 3, score);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC3"), reasonCode);
        CPPUNIT_ASSERT_EQUAL(DEPARTMENT_SCORE + AGE_SCORE + 3, score);
        
        CPPUNIT_ASSERT(executeModel(L, "department", "business", "age", 32, "income", 999.0));
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(DEPARTMENT_SCORE + AGE_SCORE +(0.03 * 999.0) + 11.0, score);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC2"), reasonCode);
        
        CPPUNIT_ASSERT(executeModel(L, "department", "business", "age", 32, "income", 1500.0));
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(DEPARTMENT_SCORE + AGE_SCORE +5.0, score);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC2"), reasonCode);
        
        CPPUNIT_ASSERT(executeModel(L, "department", "business", "age", 32, "income", 2100.0));
        CPPUNIT_ASSERT(getValue(L, "Final Score", score));
        CPPUNIT_ASSERT_EQUAL(DEPARTMENT_SCORE + AGE_SCORE + (0.01 * 2100.0) - 18.0, score);
        CPPUNIT_ASSERT(getValue(L, "Reason Code 2", reasonCode));
        CPPUNIT_ASSERT_EQUAL(std::string("RC3"), reasonCode);
    }

    CPPUNIT_TEST_SUITE(TestScorecard);
    CPPUNIT_TEST(testCharacteristicReasonCode);
    CPPUNIT_TEST(testAttributeReasonCode);
    CPPUNIT_TEST(testComplexPartialScore);
    CPPUNIT_TEST_SUITE_END();
};
