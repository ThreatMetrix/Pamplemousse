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
//  Created by Caleb Moore on 9/3/20.
//

#include <math.h>
#include "naivebayesmodel.hpp"
#include "conversioncontext.hpp"
#include "ast.hpp"
#include "transformation.hpp"


static bool buildFromPairs(AstBuilder & builder, const PMMLDocument::MiningField * fieldDefinition, const tinyxml2::XMLElement * pairsStart,
                           std::unordered_map<std::string, std::vector<AstNode>> & outputs, const char * threshold)
{
    builder.field(fieldDefinition);
    AstNode field = builder.popNode();
    
    std::vector<Transformation::DiscretizeBin> bins;
    if (const tinyxml2::XMLElement * derivedField = pairsStart->Parent()->FirstChildElement("DerivedField"))
    {
        if (const tinyxml2::XMLElement * discretize = derivedField->FirstChildElement("Discretize"))
        {
            if (!Transformation::parseDiscretizeBins(builder, bins, discretize))
            {
                return false;
            }
        }
        else
        {
            builder.parsingError("DerivedField only supports Discretize at %i\n", derivedField->GetLineNum());
            return false;
        }
    }
    
    std::unordered_map<std::string, std::unordered_map<std::string, double>> mapOfMaps;
    for (const tinyxml2::XMLElement * pair = pairsStart; pair != nullptr; pair = pair->NextSiblingElement("PairCounts"))
    {
        const char * value = pair->Attribute("value");
        if (value == nullptr)
        {
            builder.parsingError("No value specified at %i\n", pair->GetLineNum());
            return false;
        }
        
        const tinyxml2::XMLElement * targetValueCounts = pair->FirstChildElement("TargetValueCounts");
        if (targetValueCounts == nullptr)
        {
            builder.parsingError("No TargetValueCounts specified at %i\n", pair->GetLineNum());
            return false;
        }
        
        for (const tinyxml2::XMLElement * targetValueCount = targetValueCounts->FirstChildElement("TargetValueCount");
             targetValueCount != nullptr; targetValueCount = targetValueCount->NextSiblingElement("TargetValueCount"))
        {
            const char * targetValue = targetValueCount->Attribute("value");
            double count;
            if (targetValue == nullptr)
            {
                builder.parsingError("No value at %i\n", targetValueCount->GetLineNum());
                return false;
            }
            if (targetValueCount->QueryAttribute("count", &count) != tinyxml2::XML_SUCCESS)
            {
                builder.parsingError("No valid count at %i\n", targetValueCount->GetLineNum());
                return false;
            }
            
            auto & newpairs = mapOfMaps.emplace(targetValue, std::unordered_map<std::string, double>()).first->second;
            newpairs.emplace(value, count);
        }
    }
    
    // Go over each output to build an expression to select a value.
    for (auto & output : outputs)
    {
        auto foundPairs = mapOfMaps.find(output.first);
        if (foundPairs == mapOfMaps.end())
        {
            // Should this be an error?
            continue;
        }
        auto & pairs = foundPairs->second;
        // Calculate the total count for this particular value.
        double total = 0;
        for (auto & pair : pairs)
        {
            total += pair.second;
        }
        
        // Missing values are ignored, where we just multiply by identity
        builder.pushNode(field);
        builder.function(Function::functionTable.names.isNotMissing, 1);
        size_t numChecks = 1;
        
        size_t numInnerTernaries = 0;
        if (bins.empty())
        {
            for (const auto & pair : pairs)
            {
                builder.field(fieldDefinition);
                builder.constant(pair.first, fieldDefinition->variable->field.dataType);
                builder.function(Function::functionTable.names.equal, 2);
                if (pair.second == 0)
                {
                    builder.constant(threshold, PMMLDocument::TYPE_NUMBER);
                }
                else
                {
                    builder.constant(pair.second / total);
                }
                numInnerTernaries++;
            }
            
            // Stick a one at the end, just to handle invalid values correctly.
            builder.constant(1);
        }
        else
        {
            // This may be 1 if undefined (above) or falls in a hole.
            Transformation::findHolesInDiscretizeBins(builder, bins, field);
            numChecks++;

            for (auto iter = bins.cbegin(); iter != bins.cend();)
            {
                auto thisIter = iter++;
                // If it's not the last bin, put a condition in next.
                if (iter != bins.cend())
                {
                    thisIter->interval.addRightCondition(builder, field);
                    numInnerTernaries++;
                }
                
                const auto found = pairs.find(thisIter->binValue);
                if (found == pairs.end())
                {
                    builder.constant(1);
                }
                else if (found->second == 0)
                {
                    builder.constant(threshold, PMMLDocument::TYPE_NUMBER);
                }
                else
                {
                    builder.constant(found->second / total);
                }
            }
        }
        
        // Close up all of the ternary expressions.
        for (size_t i = 0; i < numInnerTernaries; i++)
        {
            builder.function(Function::functionTable.names.ternary, 3);
        }
        
        // Close up all of the ternary expressions.
        for (size_t i = 0; i < numChecks; i++)
        {
            builder.constant(1);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        
        output.second.push_back(builder.popNode());
    }
    
    return true;
}


static bool buildFromStats(AstBuilder & builder, const PMMLDocument::MiningField * fieldDefinition,
                           const tinyxml2::XMLElement * targetValueStats,
                           std::unordered_map<std::string, std::vector<AstNode>> & outputs,
                           const char * threshold)
{
    for (const tinyxml2::XMLElement * targetValueStat = targetValueStats->FirstChildElement("TargetValueStat");
         targetValueStat != nullptr; targetValueStat = targetValueStat->NextSiblingElement("TargetValueStat"))
    {
        const char * value = targetValueStat->Attribute("value");
        if (value == nullptr)
        {
            builder.parsingError("No value specified at %i\n", targetValueStat->GetLineNum());
            return false;
        }
        
        auto found = outputs.find(value);
        if (found == outputs.end())
        {
            builder.parsingError("Value is not a Baysean output at %i\n", value, targetValueStat->GetLineNum());
            return false;
        }
        
        if (const tinyxml2::XMLElement * gaussianDistribution = targetValueStat->FirstChildElement("GaussianDistribution"))
        {
            const tinyxml2::XMLAttribute* mean = gaussianDistribution->FindAttribute("mean");
            double meanAsDouble;
            if (mean == nullptr || mean->QueryDoubleValue(&meanAsDouble) != tinyxml2::XML_SUCCESS)
            {
                builder.parsingError("No mean found at %i\n", gaussianDistribution->GetLineNum());
                return false;
            }
            
            double variance;
            if (gaussianDistribution->QueryAttribute("variance", &variance) != tinyxml2::XML_SUCCESS)
            {
                builder.parsingError("No variance specified at %i\n", gaussianDistribution->GetLineNum());
                return false;
            }
            
            // Create a gaussian Probability Distribution Function
            builder.field(fieldDefinition);
            builder.constant(mean->Value(), PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.minus, 2);
            builder.constant(2);
            builder.function(Function::functionTable.names.pow, 2);
            builder.function(Function::unaryMinus, 1);
            builder.constant(2.0 * variance);
            builder.function(Function::functionTable.names.divide, 2);
            builder.function(Function::functionTable.names.exp, 1);
            builder.constant(sqrt(M_PI * 2 * variance));
            builder.function(Function::functionTable.names.divide, 2);
            if (threshold)
            {
                builder.constant(threshold, PMMLDocument::TYPE_NUMBER);
                builder.function(Function::functionTable.names.max, 2);
            }
            found->second.push_back(builder.popNode());
        }
        else
        {
            builder.parsingError("Sorry, we currently only support GaussianDistribution at %i\n", targetValueStat->GetLineNum());
            return false;
        }
    }
    return true;
}

    
static bool loadInputMappings(AstBuilder & builder, const tinyxml2::XMLElement * inputs, std::unordered_map<std::string, std::vector<AstNode>> & outputs, const char * threshold)
{
    for (const tinyxml2::XMLElement * countElement = inputs->FirstChildElement("BayesInput");
         countElement != nullptr; countElement = countElement->NextSiblingElement("BayesInput"))
    {
        const char * fieldName = countElement->Attribute("fieldName");
        if (fieldName == nullptr)
        {
            builder.parsingError("No fieldName specified at %i\n", countElement->GetLineNum());
            return false;
        }
        
        const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(fieldName);
        if (fieldDefinition == nullptr)
        {
            builder.parsingError("Unknown field specified at %i\n", fieldName, countElement->GetLineNum());
            return false;
        }

        // A field may be defined by pairs or by stats (i.e. a normal distribution)
        if (const tinyxml2::XMLElement * pairCounts = countElement->FirstChildElement("PairCounts"))
        {
            if (!buildFromPairs(builder, fieldDefinition, pairCounts, outputs, threshold))
            {
                return false;
            }
        }
        else if (const tinyxml2::XMLElement * targetValueStats = countElement->FirstChildElement("TargetValueStats"))
        {
            if (!buildFromStats(builder, fieldDefinition, targetValueStats, outputs, threshold))
            {
                return false;
            }
        }
        else
        {
            builder.parsingError("Cannot get PairCounts or TargetValueStats at %i\n", countElement->GetLineNum());
        }
    }
    return true;
}


bool NaiveBayesModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    const tinyxml2::XMLElement * inputs = node->FirstChildElement("BayesInputs");
    if (inputs == nullptr)
    {
        builder.parsingError("No BayesInputs specified at %i\n", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * outputElement = node->FirstChildElement("BayesOutput");
    if (outputElement == nullptr)
    {
        builder.parsingError("No BayesOutput specified at %i\n", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * targetValueCounts = outputElement->FirstChildElement("TargetValueCounts");
    if (targetValueCounts == nullptr)
    {
        builder.parsingError("No TargetValueCounts specified at %i\n", node->GetLineNum());
        return false;
    }
    
    // Find the categories and the original counts.
    std::unordered_map<std::string, std::vector<AstNode>> outputs;
    for (const tinyxml2::XMLElement * countElement = targetValueCounts->FirstChildElement("TargetValueCount");
         countElement != nullptr; countElement = countElement->NextSiblingElement("TargetValueCount"))
    {
        const char * value = countElement->Attribute("value");
        const char * count = countElement->Attribute("count");
        if (value == nullptr)
        {
            builder.parsingError("missing value", node->GetLineNum());
            return false;
        }
        if (count == nullptr)
        {
            builder.parsingError("missing count", node->GetLineNum());
            return false;
        }
        // Shove in the name of the category and the first term (the total count)
        auto inserted = outputs.emplace(value, std::vector<AstNode>());
        builder.constant(count, PMMLDocument::TYPE_NUMBER);
        inserted.first->second.push_back(builder.popNode());
    }
    
    if (!loadInputMappings(builder, inputs, outputs, node->Attribute("threshold")))
    {
        return false;
    }
    
    // Lay out the outputs one at a time.
    size_t blockSize = 0;
    for (const auto & output : outputs)
    {
        auto categoryOutput = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, output.first);
        for (const auto & outputNode : output.second)
        {
            builder.pushNode(outputNode);
        }
        builder.function(Function::functionTable.names.product, output.second.size());
        builder.declare(categoryOutput, AstBuilder::HAS_INITIAL_VALUE);
        blockSize++;
    }
    
    blockSize += PMMLDocument::normaliseProbabilitiesAndPickWinner(builder, config);
    
    builder.block(blockSize);
    return true;
}
