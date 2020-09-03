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
//  Created by Caleb Moore on 24/11/18.
//

#include <stdio.h>

#include "Cuti.h"

#include "document.hpp"

#include "testutils.hpp"
using namespace TestUtils;


TEST_CLASS (TestSupportVectorMachine)
{
    public:
    void testSVM()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SupportVectorXor.pmml").c_str()));
        lua_State * L;
        std::string prediction;
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        // This model is just an xor function... treat it like that
        for (int testCase = 0; testCase < 4; ++testCase)
        {
            int x = testCase % 2;
            int y = testCase / 2;
            CPPUNIT_ASSERT(executeModel(L, "x1", x , "x2", y));
            CPPUNIT_ASSERT_EQUAL(true, getValue(L, "class", prediction));
            CPPUNIT_ASSERT_EQUAL(std::string(x != y ? "yes" : "no"), prediction);
        }
    }
    
    
    void testBinaryClass()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SupportVectorBinary.pmml").c_str()));
        lua_State * L;
        std::string prediction;
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        CPPUNIT_ASSERT(executeModel(L, "Age", 14 , "Employment", "Consultant"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "TARGET", prediction));
        CPPUNIT_ASSERT_EQUAL(std::string("1"), prediction);
        CPPUNIT_ASSERT(executeModel(L, "Age", 14 , "Employment", "SelfEmp"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "TARGET", prediction));
        CPPUNIT_ASSERT_EQUAL(std::string("0"), prediction);
        CPPUNIT_ASSERT(executeModel(L, "Age", 34 , "Employment", "SelfEmp"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "TARGET", prediction));
        CPPUNIT_ASSERT_EQUAL(std::string("1"), prediction);
    }
    
    CPPUNIT_TEST_SUITE(TestSupportVectorMachine);
    CPPUNIT_TEST(testSVM);
    CPPUNIT_TEST(testBinaryClass);
    CPPUNIT_TEST_SUITE_END();
};

