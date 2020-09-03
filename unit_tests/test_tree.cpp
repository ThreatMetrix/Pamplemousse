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
//  Created by Caleb Moore on 16/11/18.

#include "Cuti.h"

#include "document.hpp"

#include "testutils.hpp"
using namespace TestUtils;


TEST_CLASS (TestTree)
{
public:
    void testNoTrueChild()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("TreeNoTrueChild.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("TreeModel");
        setupIDOutput(document, model);
        lua_State * L;
        std::string eID;
        
        model->SetAttribute("noTrueChildStrategy", "returnNullPrediction");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "prob1", 0.1));
        CPPUNIT_ASSERT_EQUAL(false, getValue(L, "id", eID));
        lua_pop(L, 1);
        
        CPPUNIT_ASSERT(executeModel(L, "prob1", 0.5));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("T1"), eID);
        lua_pop(L, 1);
        
        lua_close(L);
        
        model->SetAttribute("noTrueChildStrategy", "returnLastPrediction");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "prob1", 0.1));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("N1"), eID);
        
        CPPUNIT_ASSERT(executeModel(L, "prob1", 0.5));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", eID));
        CPPUNIT_ASSERT_EQUAL(std::string("T1"), eID);
        
        lua_close(L);
    }
    
    void testMissingValue()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("TreeMissingValue.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("TreeModel");
        setupIDOutput(document, model);
        lua_State * L;
        std::string strval;
        double confMayPlay;
        double confWillPlay;
        double confNoPlay;
        std::string whatIdo;
        
        model->SetAttribute("missingValueStrategy", "lastPrediction");
        L = makeState(document);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("2"), strval);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("1"), strval);

        lua_close(L);
        
        model->SetAttribute("missingValueStrategy", "nullPrediction");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(false, getValue(L, "id", strval));
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(false, getValue(L, "id", strval));
        
        lua_close(L);
        
        model->SetAttribute("missingValueStrategy", "defaultChild");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", 40.0, "humidity", 70.0));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("4"), strval);
        
        lua_close(L);
        
        model->SetAttribute("missingValueStrategy", "aggregateNodes");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", 45, "humidity", 90));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfWillPlay", confWillPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfMayPlay", confMayPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfNoPlay", confNoPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "whatIdo", whatIdo));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(24. / 60., confWillPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(28. / 60., confMayPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(8. / 60., confNoPlay, 0.0000001);
        CPPUNIT_ASSERT_EQUAL(std::string("may play"), whatIdo);
        
        lua_close(L);
        
        model->SetAttribute("missingValueStrategy", "weightedConfidence");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfWillPlay", confWillPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfMayPlay", confMayPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfNoPlay", confNoPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "whatIdo", whatIdo));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.8, confWillPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.04, confMayPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.16, confNoPlay, 0.0000001);
        CPPUNIT_ASSERT_EQUAL(std::string("will play"), whatIdo);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfWillPlay", confWillPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfMayPlay", confMayPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfNoPlay", confNoPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "whatIdo", whatIdo));
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.6, confWillPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, confMayPlay, 0.0000001);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, confNoPlay, 0.0000001);
        CPPUNIT_ASSERT_EQUAL(std::string("will play"), whatIdo);
        
        lua_close(L);
        
        model->SetAttribute("missingValueStrategy", "none");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(false, getValue(L, "id", strval));
        
        lua_close(L);
        
        model->SetAttribute("noTrueChildStrategy", "returnLastPrediction");
        L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("2"), strval);
        
        lua_close(L);
    }
    
    void testMissingValuePenalty()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("TreeMissingValue.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("TreeModel");
        lua_State * L;
        double confNoPlay;
        std::string whatIdo;
        
        model->SetAttribute("missingValueStrategy", "defaultChild");
        model->SetAttribute("missingValuePenalty", "0.8");
        L = makeState(document);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", 40, "humidity", 70));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfNoPlay", confNoPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "whatIdo", whatIdo));
        
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.48, confNoPlay, 0.0000001);
        CPPUNIT_ASSERT_EQUAL(std::string("no play"), whatIdo);
        
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", nullptr, "humidity", 70));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ConfWillPlay", confNoPlay));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "whatIdo", whatIdo));
        
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.576, confNoPlay, 0.0000001);
        CPPUNIT_ASSERT_EQUAL(std::string("will play"), whatIdo);
        
        lua_close(L);
    }

    void testDefaultValue()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("TreeMissingValue.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("TreeModel");
        setupIDOutput(document, model);
        lua_State * L;
        std::string strval;

        model->SetAttribute("missingValueStrategy", "nullPrediction");

        tinyxml2::XMLElement * miningField = model->FirstChildElement("MiningSchema")->FirstChildElement("MiningField");
        while (strcmp(miningField->Attribute("name"), "outlook"))
        {
            miningField = miningField->NextSiblingElement("MiningField");
        }
        miningField->SetAttribute("missingValueTreatment", "asValue");
        miningField->SetAttribute("missingValueReplacement", "sunny");

        L = makeState(document);

        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", 50, "humidity", 70));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("3"), strval);

        lua_close(L);
    }

    CPPUNIT_TEST_SUITE(TestTree);
    CPPUNIT_TEST(testNoTrueChild);
    CPPUNIT_TEST(testMissingValue);
    CPPUNIT_TEST(testMissingValuePenalty);
    CPPUNIT_TEST(testDefaultValue);
    CPPUNIT_TEST_SUITE_END();
};

