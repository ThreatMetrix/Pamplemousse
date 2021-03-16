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
//  Created by Caleb Moore on 31/8/18.
//

#include "treemodel.hpp"
#include "document.hpp"
#include "predicate.hpp"
#include "conversioncontext.hpp"
#include <algorithm>

namespace
{
    // These two lists must be kept in sync and ordered
    enum MissingValueStrategy
    {
        MVS_AGGREGATENODES,
        MVS_DEFAULTCHILD,
        MVS_LASTPREDICTION,
        MVS_NONE,
        MVS_NULLPREDICTION,
        MVS_WEIGHTEDCONFIDENCE,
        MVS_INVALID
    };

    const char * const MISSING_VALUE_STRATEGY_NAME[]
    {
        "aggregateNodes",
        "defaultChild",
        "lastPrediction",
        "none",
        "nullPrediction",
        "weightedConfidence"
    };
    
    MissingValueStrategy getMissingValueStrategyFromString(const char * name)
    {
        auto found = std::equal_range(MISSING_VALUE_STRATEGY_NAME, MISSING_VALUE_STRATEGY_NAME + static_cast<int>(MVS_INVALID), name, PMMLDocument::stringIsBefore);
        if (found.first != found.second)
        {
            return static_cast<MissingValueStrategy>(found.first - MISSING_VALUE_STRATEGY_NAME);
        }
        else
        {
            return MVS_INVALID;
        }
    }
    
    struct TreeConfig
    {
        PMMLDocument::ModelConfig & config;
        bool returnLastPrediction = false;
        MissingValueStrategy missingValueStrategy = MVS_NONE;
        PMMLDocument::ConstFieldDescriptionPtr totalNumberOfRecords;
        const char * missingValuePenalty = nullptr;
        PMMLDocument::ConstFieldDescriptionPtr totalMissingValuePenalty;
        TreeConfig(PMMLDocument::ModelConfig & c) : config(c) {}
    };
    
    bool parseTreeNode(AstBuilder & builder, const tinyxml2::XMLElement * node, TreeConfig & config)
    {
        ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
        const tinyxml2::XMLElement * firstChildNode = node->FirstChildElement("Node");
        
        if (firstChildNode == nullptr)
        {
            // Leaf node.
            return TreeModel::writeScore(builder, node, config.config, config.totalNumberOfRecords);
        }
        
        const char * defaultChildID = node->Attribute("defaultChild");
        bool foundDefaultChild = false;
        
        size_t ifChainSize = 0;

        std::vector<AstNode> savedPredicatesForNotFound;
        for (const tinyxml2::XMLElement * childNode = firstChildNode; childNode; childNode = childNode->NextSiblingElement("Node"))
        {
            const char * thisID = childNode->Attribute("id");
            bool isDefaultChild = config.missingValueStrategy == MVS_DEFAULTCHILD && defaultChildID && thisID && strcmp(thisID, defaultChildID) == 0;
            
            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(childNode->FirstChildElement());
            if (predicate == nullptr)
            {
                builder.parsingError("Tree node without predicate", childNode->GetLineNum());
                return false;
            }
            
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            
            AstNode predicateNode = builder.popNode();
            
            if (config.missingValueStrategy == MVS_LASTPREDICTION ||
                config.missingValueStrategy == MVS_NULLPREDICTION)
            {
                // If last prediction, we will output the value of THIS node in the null value clause
                if (config.missingValueStrategy == MVS_LASTPREDICTION)
                {
                    if (!TreeModel::writeScore(builder, node, config.config, config.totalNumberOfRecords))
                    {
                        return false;
                    }
                }
                else // missingValueStrategy == MVS_NULLPREDICTION
                {
                    // Instead of explicitly setting anything to null... just add an empty block to the if-chain
                    builder.block(0);
                }
                builder.pushNode(predicateNode);
                builder.function(Function::functionTable.names.isMissing, 1);
                ifChainSize += 2; // One for the body, one for the predicate.
            }

            // Add body.
            if (!parseTreeNode(builder, childNode, config))
            {
                return false;
            }
            
            // Add a penalty clause.
            if (config.missingValuePenalty && (predicateNode.function().functionType == Function::SURROGATE_MACRO || isDefaultChild))
            {
                // Multiply by the penalty
                builder.field(config.totalMissingValuePenalty);
                builder.constant(config.missingValuePenalty, PMMLDocument::TYPE_NUMBER);
                builder.function(Function::functionTable.names.times, 2);
                builder.assign(config.totalMissingValuePenalty);
                
                // Predicate
                size_t numThingsToGoWrong = 0;
                // This could be a surrogate where the first thing is unknown
                if (predicateNode.function().functionType == Function::SURROGATE_MACRO)
                {
                    builder.pushNode(predicateNode.children[0]);
                    builder.function(Function::functionTable.names.isMissing, 1);
                    numThingsToGoWrong++;
                }
                
                if (isDefaultChild)
                {
                    // Something before this could have been missing
                    for (const AstNode & node : savedPredicatesForNotFound)
                    {
                        builder.pushNode(node);
                        builder.function(Function::functionTable.names.isMissing, 1);
                    }
                    
                    // Or this node itself could be missing or false (indicating something after was missing)
                    builder.pushNode(predicateNode);
                    builder.defaultValue("false");
                    builder.function(Function::functionTable.names.fnNot, 1);
                    numThingsToGoWrong += savedPredicatesForNotFound.size() + 1;
                }
                
                builder.function(Function::functionTable.names.fnOr, numThingsToGoWrong);
                builder.ifChain(2);
                
                // Stick it after the body
                builder.block(2);
            }
            
            // Add predicate
            builder.pushNode(predicateNode);
            ifChainSize += 2;  // One for the body, one for the predicate.
            
            // These types evaluate all missing branches
            if (config.missingValueStrategy == MVS_AGGREGATENODES ||
                config.missingValueStrategy == MVS_WEIGHTEDCONFIDENCE)
            {
                builder.defaultValue("true");
                // We may need to put an inverse condition after, so save a copy
                savedPredicatesForNotFound.push_back(predicateNode);
                // These modes use seperate if statements, not a chain
                builder.ifChain(2);
                ifChainSize--; // It's now just one node per condition
            }
            else if (config.missingValueStrategy == MVS_DEFAULTCHILD)
            {
                if (isDefaultChild)
                {
                    foundDefaultChild = true;
                    
                    // Default child handles the case if its own predicate is unknown
                    builder.defaultValue("true");

                    // Or if any previous conditions are missing
                    for (const AstNode & node : savedPredicatesForNotFound)
                    {
                        builder.pushNode(node);
                        builder.function(Function::functionTable.names.isMissing, 1);
                    }
                    
                    size_t trailingThings = 0;
                    // As well as future conditions... with the caveat that if a branch WAS taken, it doesn't matter if anything beyond it is unknown
                    // build something like isMissing(conditionB) or (not conditionB and (isMissing(conditionC) or not conditionC))... etc.
                    for (const tinyxml2::XMLElement * nextChildNode = childNode->NextSiblingElement("Node"); nextChildNode; nextChildNode = nextChildNode->NextSiblingElement("Node"))
                    {
                        const tinyxml2::XMLElement * nextPredicate = PMMLDocument::skipExtensions(childNode->FirstChildElement());
                        if (Predicate::parse(builder, nextPredicate))
                        {
                            AstNode nextPredicateNode = builder.topNode();
                            builder.function(Function::functionTable.names.isMissing, 1);
                            builder.pushNode(nextPredicateNode);
                            builder.defaultValue("false");
                            builder.function(Function::functionTable.names.fnNot, 1);
                            ++trailingThings;
                        }
                    }
                    
                    // There will be a trailing not(condition) at the end... kill it
                    if (trailingThings > 0)
                    {
                        builder.popNode();
                    }
                    
                    while (trailingThings > 1)
                    {
                        builder.function(Function::functionTable.names.fnOr, 2);
                        trailingThings--;
                        if (trailingThings > 1)
                            builder.function(Function::functionTable.names.fnAnd, 2);
                    }
                    
                    // Build the final or statement. The condition itself is true, or anything before it is unknown, or anything after it is unknown.
                    builder.function(Function::functionTable.names.fnOr, 1 + savedPredicatesForNotFound.size() + trailingThings);
                }
                else if (!foundDefaultChild)
                {
                    // Before the default child, we need to make sure we don't automatically drop into any other conditions
                    if (!savedPredicatesForNotFound.empty())
                    {
                        for (const AstNode & node : savedPredicatesForNotFound)
                        {
                            builder.pushNode(node);
                            builder.function(Function::functionTable.names.isNotMissing, 1);
                        }
                        builder.function(Function::functionTable.names.fnAnd, savedPredicatesForNotFound.size() + 1);
                    }
                    // Save it for the predecate for NOT FOUND
                    savedPredicatesForNotFound.push_back(predicateNode);
                }
            }
            
        }

        if (config.returnLastPrediction)
        {
            if (!TreeModel::writeScore(builder, node, config.config, config.totalNumberOfRecords))
            {
                return false;
            }
            ifChainSize++;
            
            // These modes don't use else if, so create an inverse comparison for NOT matching anything above
            if (config.missingValueStrategy == MVS_AGGREGATENODES ||
                config.missingValueStrategy == MVS_WEIGHTEDCONFIDENCE)
            {
                for (const AstNode & node : savedPredicatesForNotFound)
                {
                    builder.pushNode(node);
                }
                builder.function(Function::functionTable.names.fnOr, savedPredicatesForNotFound.size());
                builder.function(Function::functionTable.names.fnNot, 1);
                builder.ifChain(2);
            }
        }
        
        if (config.missingValueStrategy == MVS_AGGREGATENODES ||
            config.missingValueStrategy == MVS_WEIGHTEDCONFIDENCE)
        {
            builder.block(ifChainSize);
        }
        else
        {
            // For every other mode, use a chain of p
            builder.ifChain(ifChainSize);
        }
        return true;
    }
    
    void assignOrIncrement(AstBuilder & builder, PMMLDocument::ConstFieldDescriptionPtr field, bool doIncrement)
    {
        if (doIncrement)
        {
            builder.field(field);
            builder.defaultValue("0");
            builder.function(Function::functionTable.names.plus, 2);
        }
        builder.assign(field);
    }
}


bool TreeModel::writeScore(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config, PMMLDocument::ConstFieldDescriptionPtr recordNumberAccumulator, const char * defaultScore)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    size_t blockSize = 0;
    const char * score = defaultScore ? defaultScore : node->Attribute("score");
    const tinyxml2::XMLElement * scoreDistribution = node->FirstChildElement("ScoreDistribution");
    const tinyxml2::XMLAttribute * totalRecords = node->FindAttribute("recordCount");
    
    if (recordNumberAccumulator)
    {
        if (totalRecords)
        {
            builder.constant(totalRecords->Value(), PMMLDocument::TYPE_NUMBER);
            builder.field(recordNumberAccumulator);
            builder.defaultValue("0");
            builder.function(Function::functionTable.names.plus, 2);
            builder.assign(recordNumberAccumulator);
            blockSize++;
        }
        else
        {
            builder.parsingError("Missing value strategies \"aggregateNodes\" and \"weightedConfidence\" both require a recordCount", node->GetLineNum());
            return false;
        }
    }
    
    if (config.outputValueName)
    {
        if (score)
        {
            builder.constant(score, config.outputType);
            builder.assign(config.outputValueName);
            blockSize++;
        }
        else if (scoreDistribution)
        {
            const char * bestValue = nullptr;
            double bestRecordCount = 0;
            for (const tinyxml2::XMLElement * iter = scoreDistribution; iter; iter = iter->NextSiblingElement("ScoreDistribution"))
            {
                const char * value = iter->Attribute("value");
                double elementRecordCount;
                if (node->QueryDoubleAttribute("recordCount", &elementRecordCount) || value == nullptr)
                {
                    builder.parsingError("ScoreDistribution requires a recordCount and a value", node->GetLineNum());
                    return false;
                }
                
                if (elementRecordCount > bestRecordCount)
                {
                    bestRecordCount = elementRecordCount;
                    bestValue = value;
                }
            }
            if (bestValue)
            {
                builder.constant(bestValue, config.outputType);
                assignOrIncrement(builder, config.outputValueName, recordNumberAccumulator != nullptr);
                blockSize++;
            }
        }
    }
    
    if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
    {
        double higestProbability = 0;
        if (scoreDistribution)
        {
            // This seems wrong here, but the standards specify record count "NUMBER" i.e. double.
            double totalRecordCount;
            // Try to obtain the record count from the tree node
            if (totalRecords == nullptr || totalRecords->QueryDoubleValue(&totalRecordCount))
            {
                // If that is missing, add up the total from score distribution elements
                totalRecordCount = 0;
                for (const tinyxml2::XMLElement * iter = scoreDistribution; iter; iter = iter->NextSiblingElement("ScoreDistribution"))
                {
                    double elementRecordCount;
                    if (iter->QueryDoubleAttribute("recordCount", &elementRecordCount) == tinyxml2::XML_SUCCESS)
                    {
                        // Failures are dealt with after this... just ignore for now.
                        totalRecordCount += elementRecordCount;
                    }
                }
            }
            
            for (const tinyxml2::XMLElement * iter = scoreDistribution; iter; iter = iter->NextSiblingElement("ScoreDistribution"))
            {
                const char * value = iter->Attribute("value");
                const tinyxml2::XMLAttribute * elementRecords = iter->FindAttribute("recordCount");
                double elementRecordCount;
                if (elementRecords->QueryDoubleValue(&elementRecordCount) || value == nullptr)
                {
                    builder.parsingError("ScoreDistribution requires a recordCount and a value", elementRecords->GetLineNum());
                    return false;
                }
                
                // probability is optional, otherwise, use the record count.
                double probDouble;
                if (const tinyxml2::XMLAttribute * probability = iter->FindAttribute("probability"))
                {
                    probDouble = probability->QueryDoubleValue(&probDouble);
                    if (recordNumberAccumulator)
                    {
                        probDouble *= totalRecordCount;
                        builder.constant(probDouble);
                    }
                    else
                    {
                        builder.constant(probability->Value(), PMMLDocument::TYPE_NUMBER);
                    }
                }
                else
                {
                    if (recordNumberAccumulator)
                    {
                        probDouble = elementRecordCount;
                        builder.constant(elementRecords->Value(), PMMLDocument::TYPE_NUMBER);
                    }
                    else
                    {
                        probDouble = totalRecordCount > 0 ? (elementRecordCount / totalRecordCount) : 0;
                        builder.constant(probDouble);
                    }
                }
                
                higestProbability = std::max(higestProbability, probDouble);
                
                PMMLDocument::ConstFieldDescriptionPtr outputField = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, value);
                assignOrIncrement(builder, outputField, recordNumberAccumulator != nullptr);
                blockSize++;
                
                if (const tinyxml2::XMLAttribute * confidence = iter->FindAttribute("confidence"))
                {
                    if (recordNumberAccumulator)
                    {
                        builder.constant(confidence->DoubleValue() * totalRecordCount);
                    }
                    else
                    {
                        builder.constant(confidence->Value(), PMMLDocument::TYPE_NUMBER);
                    }
                    
                    PMMLDocument::ConstFieldDescriptionPtr outputField = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.confidenceValues, "confidence", PMMLDocument::TYPE_NUMBER, value);
                    assignOrIncrement(builder, outputField, recordNumberAccumulator != nullptr);
                    blockSize++;
                }
            }
        }
        else if (score)
        {
            if (recordNumberAccumulator)
            {
                totalRecords->QueryDoubleValue(&higestProbability);
                builder.constant(totalRecords->Value(), PMMLDocument::TYPE_NUMBER);
            }
            else
            {
                higestProbability = 1;
                builder.constant(1);
            }
            
            PMMLDocument::ConstFieldDescriptionPtr outputField = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, score);
            assignOrIncrement(builder, outputField, recordNumberAccumulator != nullptr);
            blockSize++;
        }
        
        if (config.bestProbabilityValueName)
        {
            builder.constant(higestProbability);
            assignOrIncrement(builder, config.bestProbabilityValueName, recordNumberAccumulator != nullptr);
            blockSize++;
        }
    }
    
    // Should only be able to return an ID if there is a score available to this node.
    if (config.idValueName && (score != nullptr || scoreDistribution != nullptr))
    {
        if (const tinyxml2::XMLAttribute * nodeID = node->FindAttribute("id"))
        {
            builder.constant(nodeID->Value(), PMMLDocument::TYPE_STRING);
            builder.assign(config.idValueName);
            blockSize++;
        }
    }
    
    builder.block(blockSize);
    
    return true;
}

bool TreeModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node,
                      PMMLDocument::ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    TreeConfig treeConfig(config);
    const tinyxml2::XMLAttribute * mvs = node->FindAttribute("missingValueStrategy");
    if (mvs)
    {
        treeConfig.missingValueStrategy = getMissingValueStrategyFromString(mvs->Value());
        if (treeConfig.missingValueStrategy == MVS_INVALID)
        {
            builder.parsingError("Unknown missingValueStrategy", mvs->Value(), node->GetLineNum());
            return false;
        }
    }
    
    const tinyxml2::XMLAttribute * ntc = node->FindAttribute("noTrueChildStrategy");
    
    if (ntc)
    {
        if (strcmp(ntc->Value(), "returnLastPrediction") == 0)
        {
            treeConfig.returnLastPrediction = true;
        }
        else if (strcmp(ntc->Value(), "returnNullPrediction") == 0)
        {
            treeConfig.returnLastPrediction = false;
        }
        else
        {
            builder.parsingError("Unknown noTrueChildStrategy", ntc->Value(), ntc->GetLineNum());
            return false;
        }
    }

    size_t blockSize = 0;
    if (config.outputValueName)
    {
        builder.declare(config.outputValueName, AstBuilder::NO_INITIAL_VALUE);
        blockSize++;
    }
    
    if (treeConfig.missingValueStrategy == MVS_AGGREGATENODES ||
        treeConfig.missingValueStrategy == MVS_WEIGHTEDCONFIDENCE)
    {
        treeConfig.totalNumberOfRecords = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "totalRecords");
        builder.declare(treeConfig.totalNumberOfRecords, AstBuilder::NO_INITIAL_VALUE);
        blockSize++;
    }
    
    if ((treeConfig.missingValuePenalty = node->Attribute("missingValuePenalty")))
    {
        treeConfig.totalMissingValuePenalty = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "missingValuePenalty");
        builder.constant("1", PMMLDocument::TYPE_NUMBER);
        builder.declare(treeConfig.totalMissingValuePenalty, AstBuilder::HAS_INITIAL_VALUE);
        blockSize++;
    }
    
    if (!parseTreeNode(builder, node, treeConfig))
    {
        return false;
    }
    
    // We want to declare the probability variables before the tree, but based on what we've actually seen referenced in the tree, so pop off the tree and define thenm first
    AstNode treeNode = builder.popNode();
    
    for (const auto & probOutput : config.probabilityValueName)
    {
        builder.declare(probOutput.second, AstBuilder::NO_INITIAL_VALUE);
        blockSize++;
    }
    
    for (const auto & confOutput : config.confidenceValues)
    {
        builder.declare(confOutput.second, AstBuilder::NO_INITIAL_VALUE);
        blockSize++;
    }
    
    builder.pushNode(std::move(treeNode));
    blockSize++;
    
    if (treeConfig.missingValueStrategy == MVS_AGGREGATENODES ||
        treeConfig.missingValueStrategy == MVS_WEIGHTEDCONFIDENCE)
    {
        if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
        {
            blockSize += pickWinner(builder, config, treeConfig.missingValueStrategy == MVS_AGGREGATENODES ? config.probabilityValueName : config.confidenceValues);
            
            builder.field(treeConfig.totalNumberOfRecords);
            AstNode factor = builder.popNode();
            blockSize += PMMLDocument::normalizeProbabilityArrayAccordingToFactor(builder, config.probabilityValueName, "normalized_probability", factor);
            blockSize += PMMLDocument::normalizeProbabilityArrayAccordingToFactor(builder, config.confidenceValues, "normalized_confidence", factor);
        }
        else if (config.function == PMMLDocument::FUNCTION_REGRESSION)
        {
            if (config.outputValueName)
            {
                builder.field(config.outputValueName);
                builder.field(treeConfig.totalNumberOfRecords);
                builder.function(Function::functionTable.names.divide, 2);
                config.outputValueName = builder.context().createVariable(config.outputValueName->field.dataType, "normalized_result");
                builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
            }
        }
    }
    
    // Multiply confidence with the penalty if needed
    if (treeConfig.missingValuePenalty)
    {
        PMMLDocument::ProbabilitiesOutputMap normalizedConfidenceOutputMap;
        for (auto & pair : config.confidenceValues)
        {
            builder.field(pair.second);
            builder.field(treeConfig.totalMissingValuePenalty);
            builder.function(Function::functionTable.names.times, 2);
            
            PMMLDocument::ConstFieldDescriptionPtr normalizedProbability = getOrAddCategoryInOutputMap(builder.context(), normalizedConfidenceOutputMap, "scaled_confidence", PMMLDocument::TYPE_NUMBER, pair.first);
            builder.declare(normalizedProbability, AstBuilder::HAS_INITIAL_VALUE );
            blockSize++;
        }
        config.confidenceValues = std::move(normalizedConfidenceOutputMap);
    }
    
    builder.block(blockSize);
    
    return true;
}
