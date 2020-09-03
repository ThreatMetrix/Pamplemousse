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
//  Created by Caleb Moore on 30/11/18.
//


#include "Cuti.h"

#include "document.hpp"

#include "testutils.hpp"
using namespace TestUtils;


TEST_CLASS (TestRuleset)
{
    public:
    void testCompoundRule()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("RuleSetComplex.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("RuleSetModel");
        setupIDOutput(document, model);
        lua_State * L;
        std::string eID;
        
        tinyxml2::XMLElement * ruleSet = model->FirstChildElement("RuleSet");
        // Weighted sum is currently not supported. This puts weighted max to the front.
        ruleSet->DeleteChild(ruleSet->FirstChildElement("RuleSelectionMethod"));
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "BP", "HIGH", "K", 0.0621, "Age", 36, "Na", 0.5023));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("RULE2"), eID);
        lua_pop(L, 1);
        
        lua_close(L);
        // Now first hit is in the front
        ruleSet->DeleteChild(ruleSet->FirstChildElement("RuleSelectionMethod"));
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "BP", "HIGH", "K", 0.0621, "Age", 36, "Na", 0.5023));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("RULE1"), eID);
        lua_pop(L, 1);
        
        // Test default
        CPPUNIT_ASSERT(executeModel(L, "BP", "LOW", "K", nullptr, "Age", nullptr, "Na", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "$C-Drug", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("drugY"), eID);
        lua_pop(L, 1);
        lua_close(L);
    }
    
    void testSimpleRule()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("RuleSetSimple.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("RuleSetModel");
        setupIDOutput(document, model);
        lua_State * L;
        std::string eID;
        
        tinyxml2::XMLElement * ruleSet = model->FirstChildElement("RuleSet");
        // Weighted sum is currently not supported. This puts weighted max to the front.
        ruleSet->DeleteChild(ruleSet->FirstChildElement("RuleSelectionMethod"));
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "BP", "HIGH", "K", 0.0621, "Age", 36, "Na", 0.5023));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("RULE2"), eID);
        lua_pop(L, 1);
        
        lua_close(L);
        // Now first hit is in the front
        ruleSet->DeleteChild(ruleSet->FirstChildElement("RuleSelectionMethod"));
        
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "BP", "HIGH", "K", 0.0621, "Age", 36, "Na", 0.5023));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("RULE1"), eID);
        lua_pop(L, 1);
        
        // Test default
        CPPUNIT_ASSERT(executeModel(L, "BP", "LOW", "K", nullptr, "Age", nullptr, "Na", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "$C-Drug", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("drugY"), eID);
        lua_pop(L, 1);
        lua_close(L);
    }
    
    CPPUNIT_TEST_SUITE(TestRuleset);
    CPPUNIT_TEST(testCompoundRule);
    CPPUNIT_TEST(testSimpleRule);
    CPPUNIT_TEST_SUITE_END();
};
