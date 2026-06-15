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

#include <cstring>
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
    
    // Regression test for the tree codegen path where defaultChild is the
    // LAST sibling of its parent. The existing testMissingValue case uses
    // TreeMissingValue.pmml as-shipped, where defaultChild is the FIRST
    // sibling at every internal node; that path is well exercised but the
    // last-sibling case wasn't previously covered. Important now because we
    // intend to start emitting last-sibling defaults as `else` (no predicate)
    // rather than `elseif (negation)` for size/perf reasons.
    void testMissingValueDefaultIsLastSibling()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS,
            document.LoadFile(getPathToFile("TreeMissingValue.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("TreeModel");
        setupIDOutput(document, model);
        model->SetAttribute("missingValueStrategy", "defaultChild");

        // Reassign defaultChild on the two internal nodes to point at the LAST
        // sibling at each level:
        //   root (id=1): default 2 -> 5  (5 is the second/last child)
        //   inner (id=2): default 3 -> 4 (4 is the second/last child)
        tinyxml2::XMLElement * rootNode = model->FirstChildElement("Node");
        CPPUNIT_ASSERT(rootNode != nullptr);
        rootNode->SetAttribute("defaultChild", "5");
        for (tinyxml2::XMLElement * child = rootNode->FirstChildElement("Node"); child; child = child->NextSiblingElement("Node"))
        {
            if (const char * idAttr = child->Attribute("id"))
            {
                if (std::strcmp(idAttr, "2") == 0)
                {
                    child->SetAttribute("defaultChild", "4");
                }
            }
        }

        lua_State * L = makeState(document);
        CPPUNIT_ASSERT(L != nullptr);
        std::string strval;

        // outlook missing -> root default is now 5 -> "may play" leaf.
        // Pre-change, this routes through elseif(outlook==nil or outlook in {overcast,rain}).
        // Post-change (planned), this routes through plain else.
        CPPUNIT_ASSERT(executeModel(L, "outlook", nullptr, "temperature", 40.0, "humidity", 70.0));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("5"), strval);

        // outlook=sunny: routes into Node 2's subtree. Inside Node 2, both
        // children are surrogates. Surrogate evaluates the first predicate
        // and only falls back to the second when the first is *missing*, not
        // when it's false. So with temperature=40 (non-missing, <50),
        // pred_3 = FALSE and pred_4 = TRUE -> Node 4. The defaultChild
        // change doesn't affect this because the predicates resolve cleanly.
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", 40.0, "humidity", 70.0));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("4"), strval);

        // outlook=sunny, temperature missing, humidity missing -> both
        // surrogate predicates have all inputs missing, so they're both
        // MISSING. With Node 2's defaultChild reassigned to 4, the missing
        // case routes to Node 4 instead of the original Node 3.
        CPPUNIT_ASSERT(executeModel(L, "outlook", "sunny", "temperature", nullptr, "humidity", nullptr));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("4"), strval);

        // outlook=overcast: explicitly matches Node 5.
        CPPUNIT_ASSERT(executeModel(L, "outlook", "overcast", "temperature", 40.0, "humidity", 70.0));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "id", strval));
        CPPUNIT_ASSERT_EQUAL(std::string("5"), strval);

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
    CPPUNIT_TEST(testMissingValueDefaultIsLastSibling);
    CPPUNIT_TEST(testMissingValuePenalty);
    CPPUNIT_TEST(testDefaultValue);
    CPPUNIT_TEST_SUITE_END();
};

