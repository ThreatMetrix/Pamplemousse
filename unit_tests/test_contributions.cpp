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

#include "contributions_fixture.hpp"
#include "document.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "luaconverter/optimiser.hpp"
#include "testutils.hpp"

#include <sstream>
#include <vector>

using namespace TestUtils;

namespace
{
    const Function::Definition ReturnStatement =
    {
        nullptr,
        Function::RETURN_STATEMENT,
        PMMLDocument::TYPE_VOID,
        LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
    };

    struct RescaleParams
    {
        bool hasFactor = false;
        double factor = 1.0;
        bool hasConstant = false;
        double constant = 0.0;
    };

    // Inject a <Targets><Target rescaleFactor=... rescaleConstant=.../></Targets>
    // element into the inner regression MiningModel of the XGBoost fixture.
    // Pamplemousse must apply the same linear transform to the score and to the
    // contribution bias (and scale contributions when factor != 1) so the
    // Saabas invariant `bias + sum(contribs) == score` keeps holding.
    bool injectRescaleIntoXGBoostFixture(tinyxml2::XMLDocument & document, const RescaleParams & rescale)
    {
        if (!rescale.hasFactor && !rescale.hasConstant)
        {
            return true;
        }

        // Outer MiningModel -> Segmentation -> Segment[1] -> (predicate) -> inner MiningModel
        auto outer = document.RootElement()->FirstChildElement("MiningModel");
        if (!outer) return false;
        auto outerSeg = outer->FirstChildElement("Segmentation");
        if (!outerSeg) return false;
        auto firstSegment = outerSeg->FirstChildElement("Segment");
        if (!firstSegment) return false;
        auto predicate = firstSegment->FirstChildElement(); // <True/> in this fixture
        if (!predicate) return false;
        auto innerMM = predicate->NextSiblingElement("MiningModel");
        if (!innerMM) return false;

        auto targets = document.NewElement("Targets");
        auto target = document.NewElement("Target");
        if (rescale.hasFactor)
        {
            target->SetAttribute("rescaleFactor", rescale.factor);
        }
        if (rescale.hasConstant)
        {
            target->SetAttribute("rescaleConstant", rescale.constant);
        }
        targets->InsertEndChild(target);
        innerMM->InsertEndChild(targets);
        return true;
    }

    lua_State * makeContributionsState(const RescaleParams & rescale = {})
    {
        tinyxml2::XMLDocument document;
        if (document.LoadFile(getPathToFile("XGBoostBinaryLogistic.pmml").c_str()) != tinyxml2::XML_SUCCESS)
        {
            return nullptr;
        }

        if (!injectRescaleIntoXGBoostFixture(document, rescale))
        {
            return nullptr;
        }

        AstBuilder builder;
        auto contributionsTable = builder.context().createVariable(PMMLDocument::TYPE_TABLE, "__contributions__", PMMLDocument::ORIGIN_OUTPUT);
        auto biasAccumulator = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "__bias__", PMMLDocument::ORIGIN_OUTPUT);
        PMMLDocument::ModelConfig config;
        config.contributionsTable = contributionsTable;
        config.biasAccumulator = biasAccumulator;
        if (!PMMLDocument::convertPMML(builder, document.RootElement(), &config))
        {
            return nullptr;
        }

        AstNode model = builder.popNode();
        builder.function(Function::makeTuple, 0);
        builder.declare(contributionsTable, AstBuilder::HAS_INITIAL_VALUE);
        builder.constant(0);
        builder.declare(biasAccumulator, AstBuilder::HAS_INITIAL_VALUE);
        builder.pushNode(std::move(model));

        builder.field(biasAccumulator);
        builder.constant("__bias__", PMMLDocument::TYPE_STRING);
        builder.assignIndirect(contributionsTable, 1);

        auto rawScore = builder.context().getFieldDescription("xgbValue");
        if (!rawScore)
        {
            return nullptr;
        }
        builder.field(rawScore);
        builder.field(contributionsTable);
        builder.function(ReturnStatement, 2);
        builder.block(builder.stackSize());

        AstNode astTree = builder.popNode();
        std::stringstream source;
        LuaOutputter output(source);
        PMMLDocument::optimiseAST(astTree, output);

        output.function("func");
        output.keyword("median_age_Fname");
        output.comma();
        output.keyword("q60_age_Fname");
        output.comma();
        output.keyword("q70_age_Fname");
        output.comma();
        output.keyword("q80_age_Fname");
        output.comma();
        output.keyword("q90_age_Fname");
        output.finishedArguments();
        LuaConverter::convertAstToLua(astTree, output);
        output.endBlock();

        lua_State * L = luaL_newstate();
        luaL_openlibs(L);
        if (luaL_dostring(L, source.str().c_str()))
        {
            fprintf(stderr, "%s\n", lua_tostring(L, -1));
            lua_close(L);
            return nullptr;
        }
        return L;
    }

    double getTableNumber(lua_State * L, const char * key)
    {
        lua_pushstring(L, key);
        lua_gettable(L, -2);
        const double out = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return out;
    }
}

TEST_CLASS (TestContributions)
{
public:
    void testXGBoostBinaryLogisticContributions()
    {
        lua_State * L = makeContributionsState();
        CPPUNIT_ASSERT(L != nullptr);

        for (const ContributionsFixture::Case & thisCase : ContributionsFixture::xgboostBinaryLogisticCases())
        {
            lua_getglobal(L, "func");
            lua_pushnumber(L, thisCase.input.at("median_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q60_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q70_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q80_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q90_age_Fname"));
            CPPUNIT_ASSERT_EQUAL(0, lua_pcall(L, 5, 2, 0));

            const double rawScore = lua_tonumber(L, -2);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.raw_score, rawScore, 1e-6);
            CPPUNIT_ASSERT(lua_istable(L, -1));

            double sum = 0;
            for (const auto & contribution : thisCase.contribs)
            {
                const double value = getTableNumber(L, contribution.first.c_str());
                CPPUNIT_ASSERT_DOUBLES_EQUAL(contribution.second, value, 1e-6);
                sum += value;
            }

            const double bias = getTableNumber(L, "__bias__");
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.bias, bias, 1e-6);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(rawScore, bias + sum, 1e-9);
            lua_pop(L, 2);
        }

        lua_close(L);
    }

    // <Target rescaleConstant="X"/> shifts both the score output and the
    // contribution bias by exactly X, leaving per-feature contributions
    // unchanged. Without this fix in output.cpp::doTargetPostprocessing, the
    // score-side gets the offset but the bias does not, silently breaking the
    // Saabas invariant by exactly the rescaleConstant for any XGBoost model
    // with a non-trivial base_score (which JPMML-XGBoost encodes as
    // rescaleConstant).
    void testRescaleConstantFoldedIntoBias()
    {
        const double constant = 7.5;
        RescaleParams rescale;
        rescale.hasConstant = true;
        rescale.constant = constant;

        lua_State * L = makeContributionsState(rescale);
        CPPUNIT_ASSERT(L != nullptr);

        for (const ContributionsFixture::Case & thisCase : ContributionsFixture::xgboostBinaryLogisticCases())
        {
            lua_getglobal(L, "func");
            lua_pushnumber(L, thisCase.input.at("median_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q60_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q70_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q80_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q90_age_Fname"));
            CPPUNIT_ASSERT_EQUAL(0, lua_pcall(L, 5, 2, 0));

            const double rawScore = lua_tonumber(L, -2);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.raw_score + constant, rawScore, 1e-6);
            CPPUNIT_ASSERT(lua_istable(L, -1));

            double sum = 0;
            for (const auto & contribution : thisCase.contribs)
            {
                const double value = getTableNumber(L, contribution.first.c_str());
                // Per-feature contributions are unchanged: rescaleConstant is
                // additive on the score, not multiplicative.
                CPPUNIT_ASSERT_DOUBLES_EQUAL(contribution.second, value, 1e-6);
                sum += value;
            }

            const double bias = getTableNumber(L, "__bias__");
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.bias + constant, bias, 1e-6);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(rawScore, bias + sum, 1e-9);
            lua_pop(L, 2);
        }

        lua_close(L);
    }

    // <Target rescaleFactor="F"/> scales the score, the bias, AND every
    // per-feature contribution by F. Saabas invariant continues to hold.
    void testRescaleFactorScalesContributions()
    {
        const double factor = 2.5;
        RescaleParams rescale;
        rescale.hasFactor = true;
        rescale.factor = factor;

        lua_State * L = makeContributionsState(rescale);
        CPPUNIT_ASSERT(L != nullptr);

        for (const ContributionsFixture::Case & thisCase : ContributionsFixture::xgboostBinaryLogisticCases())
        {
            lua_getglobal(L, "func");
            lua_pushnumber(L, thisCase.input.at("median_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q60_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q70_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q80_age_Fname"));
            lua_pushnumber(L, thisCase.input.at("q90_age_Fname"));
            CPPUNIT_ASSERT_EQUAL(0, lua_pcall(L, 5, 2, 0));

            const double rawScore = lua_tonumber(L, -2);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.raw_score * factor, rawScore, 1e-6);
            CPPUNIT_ASSERT(lua_istable(L, -1));

            double sum = 0;
            for (const auto & contribution : thisCase.contribs)
            {
                const double value = getTableNumber(L, contribution.first.c_str());
                CPPUNIT_ASSERT_DOUBLES_EQUAL(contribution.second * factor, value, 1e-6);
                sum += value;
            }

            const double bias = getTableNumber(L, "__bias__");
            CPPUNIT_ASSERT_DOUBLES_EQUAL(thisCase.bias * factor, bias, 1e-6);
            CPPUNIT_ASSERT_DOUBLES_EQUAL(rawScore, bias + sum, 1e-9);
            lua_pop(L, 2);
        }

        lua_close(L);
    }

    void testUnsupportedMiningMethod()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("MiningModelMajority.pmml").c_str()));
        AstBuilder builder;
        PMMLDocument::ModelConfig config;
        config.contributionsTable = builder.context().createVariable(PMMLDocument::TYPE_TABLE, "__contributions__", PMMLDocument::ORIGIN_OUTPUT);
        config.biasAccumulator = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "__bias__", PMMLDocument::ORIGIN_OUTPUT);
        CPPUNIT_ASSERT(!PMMLDocument::convertPMML(builder, document.RootElement(), &config));
    }

    void testUnsupportedNonTreeModel()
    {
        tinyxml2::XMLDocument document;
        CPPUNIT_ASSERT_EQUAL(tinyxml2::XML_SUCCESS, document.LoadFile(getPathToFile("SampleScorcard.pmml").c_str()));
        AstBuilder builder;
        PMMLDocument::ModelConfig config;
        config.contributionsTable = builder.context().createVariable(PMMLDocument::TYPE_TABLE, "__contributions__", PMMLDocument::ORIGIN_OUTPUT);
        config.biasAccumulator = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "__bias__", PMMLDocument::ORIGIN_OUTPUT);
        CPPUNIT_ASSERT(!PMMLDocument::convertPMML(builder, document.RootElement(), &config));
    }

    CPPUNIT_TEST_SUITE(TestContributions);
    CPPUNIT_TEST(testXGBoostBinaryLogisticContributions);
    CPPUNIT_TEST(testRescaleConstantFoldedIntoBias);
    CPPUNIT_TEST(testRescaleFactorScalesContributions);
    CPPUNIT_TEST(testUnsupportedMiningMethod);
    CPPUNIT_TEST(testUnsupportedNonTreeModel);
    CPPUNIT_TEST_SUITE_END();
};
