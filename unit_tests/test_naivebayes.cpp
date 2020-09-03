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
//  Created by Caleb Moore on 19/7/20.
//

#include "Cuti.h"

#include "document.hpp"

#include "testutils.hpp"
#include <math.h>
using namespace TestUtils;

TEST_CLASS (TestNaiveBayes)
{
public:
    void testScoring()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("NaiveBayes.pmml").c_str()));
        tinyxml2::XMLElement * model = document.RootElement()->FirstChildElement("NaiveBayesModel");
        setupProbOutput(document, model, "1000");
        lua_State * L;
        L = makeState(document);
        CPPUNIT_ASSERT(executeModel(L, "age_of_individual", 24, "gender", "male", "no_of_claims", "2", "domicile", nullptr, "age_of_car", 1));
        double probability;
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "1000", probability));
        double L0 = 8723. * 0.001 * 4273./8598. * 225./8561. * 830./8008.;
        double L1 = 2557. * (exp(-pow(24. - 24.936, 2) / (2. * 0.516) ) / sqrt(M_PI * 2 * 0.516))* 1321./2533. * 10./2436. * 182./2266.;
        double L2 = 1530. * (exp(-pow(24. - 24.588, 2) / (2. * 0.635) ) / sqrt(M_PI * 2 * 0.635)) * 780./1522. * 9./1496. * 51./1191.;
        double L3 = 709. * (exp(-pow(24. - 24.428, 2) / (2. * 0.379) ) / sqrt(M_PI * 2 * 0.379)) * 405./697. * .001 * 26./699.;
        double L4 = 100. * (exp(-pow(24. - 24.770, 2) / (2. * 0.314) ) / sqrt(M_PI * 2 * 0.314)) * 42./90. * 10./98. * 6./87.;
        
        CPPUNIT_ASSERT_DOUBLES_EQUAL(L2 / (L0 + L1 + L2 + L3 + L4), probability, 0.000001);
    }
    
    
    CPPUNIT_TEST_SUITE(TestNaiveBayes);
    CPPUNIT_TEST(testScoring);
    CPPUNIT_TEST_SUITE_END();
};
