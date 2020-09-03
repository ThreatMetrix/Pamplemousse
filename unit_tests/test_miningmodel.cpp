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

TEST_CLASS (TestMiningModel)
{
public:
    void testClassificationMajorityVote()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        lua_State * L = makeState(document);
        
        double probability;
        std::string predictedClass;
        
        // setosa, setosa, setosa
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(1.0, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);

        // versicolor, virginica, setosa
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "asia"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(0.3333333333333333333333, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, versicolor, setosa
        CPPUNIT_ASSERT_EQUAL(0.3333333333333333333333, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }

    void testClassificationWeightedMajorityVote()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "weightedMajorityVote");
            }
        }
        
        lua_State * L = makeState(document);
        
        double probability;
        std::string predictedClass;
        
        // setosa, setosa, setosa
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(9.0 / 9.0, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "asia"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, virginica, setosa
        CPPUNIT_ASSERT_EQUAL(4.0 / 9.0, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probability));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, versicolor, setosa
        CPPUNIT_ASSERT_EQUAL(4.0 / 9.0, probability);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }
    
    void testClassificationAverage()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "average");
            }
        }
        
        lua_State * L = makeState(document);
        
        double probabilityVirginica;
        std::string predictedClass;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(0., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "asia"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL((5. / 54. + 50. / 50. + 0. / 50.) / 3., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-virginica"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, versicolor, setosa
        CPPUNIT_ASSERT_EQUAL((5. / 54. + 0. / 50. + 0. / 50.) / 3., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }
    
    void testClassificationWeightedAverage()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "weightedAverage");
            }
        }
        
        lua_State * L = makeState(document);
        
        double probabilityVirginica;
        std::string predictedClass;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(0., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "asia"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL((5. / 54. * 2. + 50. / 50. * 3. + 0. / 50. * 4.) / 9., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, versicolor, setosa
        CPPUNIT_ASSERT_EQUAL((5. / 54. * 2. + 0. / 50. * 3. + 0. / 50. * 4.) / 9., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }
    
    void testClassificationMax()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "max");
            }
        }
        
        lua_State * L = makeState(document);
        
        double probabilityVirginica;
        double probabilitySetosa;
        double probabilityVeriscolor;
        std::string predictedClass;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probabilitySetosa));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        CPPUNIT_ASSERT_EQUAL(0., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(1., probabilitySetosa);
        
        // Split with three choices with the same max probability forcing it to average the choises
        // 45/46 + 45/46 + 45/46 virginica
        // 1/46 + 0/46 + 1/46 versicolor
        // 0/46 + 1/46 + 0/46 setosa
        // Petal len >= 2.45, petal_width >= 2.85, continent == "asia"
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.5, "petal_width", 3, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbSetosa", probabilitySetosa));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVeriscolor", probabilityVeriscolor));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-virginica"), predictedClass);
        CPPUNIT_ASSERT_EQUAL(45./46., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(1. / 46. / 3., probabilitySetosa);
        CPPUNIT_ASSERT_EQUAL(2. / 46. / 3., probabilityVeriscolor);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 3, "petal_width", 1, "continent", "africa"));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        // versicolor, versicolor, setosa
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }
    
    void testClassificationWeightedFirst()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelClassificationFirst.pmml").c_str()));
        
        lua_State * L = makeState(document);
        
        double probabilityVirginica;
        std::string predictedClass;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "continent", "asia", "day", 30));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(0., probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-setosa"), predictedClass);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 3, "continent", "africa", "day", 100));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "ProbVirginica", probabilityVirginica));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedClass", predictedClass));
        
        CPPUNIT_ASSERT_EQUAL(0.5, probabilityVirginica);
        CPPUNIT_ASSERT_EQUAL(std::string("Iris-versicolor"), predictedClass);
    }
    
    void testRegressionWeightedAverage()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelRegressionAverage.pmml").c_str()));
        lua_State * L = makeState(document);
        
        double predictedSepalLength;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(5.005660 * 0.25 + 6.413333 * 0.25 + 5.005660 * 0.5, predictedSepalLength);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 4.0, "petal_width", 2.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(4.735000 * 0.25 + 6.768966 * 0.25 + 5.640000 * 0.5, predictedSepalLength);
    }
    
    void testRegressionAverage()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelRegressionAverage.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "average");
            }
        }
        lua_State * L = makeState(document);
        
        double predictedSepalLength;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL((5.005660 + 6.413333 + 5.005660) / 3.0, predictedSepalLength);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 4.0, "petal_width", 2.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL((4.735000  + 6.768966 + 5.640000) / 3.0, predictedSepalLength);
    }
    
    void testRegressionMedian()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelRegressionAverage.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "median");
            }
        }
        lua_State * L = makeState(document);
        
        double predictedSepalLength;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(5.005660, predictedSepalLength);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 4.0, "petal_width", 2.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(5.640000, predictedSepalLength);
    }
    
    void testRegressionSum()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelRegressionAverage.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "sum");
            }
        }
        lua_State * L = makeState(document);
        
        double predictedSepalLength;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(5.005660 + 6.413333 + 5.005660, predictedSepalLength);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 4.0, "petal_width", 2.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(4.735000  + 6.768966 + 5.640000, predictedSepalLength);
    }
    
    void testRegressionMax()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelRegressionAverage.pmml").c_str()));
        if (tinyxml2::XMLElement * miningModel = document.RootElement()->FirstChildElement("MiningModel"))
        {
            if (tinyxml2::XMLElement * segmentation = miningModel->FirstChildElement("Segmentation"))
            {
                segmentation->SetAttribute("multipleModelMethod", "max");
            }
        }
        lua_State * L = makeState(document);
        
        double predictedSepalLength;
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 2.0, "petal_width", 1.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(6.413333, predictedSepalLength);
        
        CPPUNIT_ASSERT(executeModel(L, "petal_length", 4.0, "petal_width", 2.5, "sepal_width", 3));
        CPPUNIT_ASSERT_EQUAL(true, getValue(L, "PredictedSepalLength", predictedSepalLength));
        CPPUNIT_ASSERT_EQUAL(6.768966, predictedSepalLength);
    }
    
    CPPUNIT_TEST_SUITE(TestMiningModel);
    CPPUNIT_TEST(testClassificationMajorityVote);
    CPPUNIT_TEST(testClassificationWeightedMajorityVote);
    CPPUNIT_TEST(testClassificationAverage);
    CPPUNIT_TEST(testClassificationWeightedAverage);
    CPPUNIT_TEST(testClassificationWeightedFirst);
    CPPUNIT_TEST(testClassificationMax);
    CPPUNIT_TEST(testRegressionAverage);
    CPPUNIT_TEST(testRegressionWeightedAverage);
    CPPUNIT_TEST(testRegressionMedian);
    CPPUNIT_TEST(testRegressionSum);
    CPPUNIT_TEST(testRegressionMax);
    CPPUNIT_TEST_SUITE_END();
};
