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

#include "miningmodel.hpp"
#include "predicate.hpp"
#include "conversioncontext.hpp"
#include "output.hpp"
#include "analyser.hpp"
#include <algorithm>

namespace MiningModel
{
    // These two lists must be kept in sync and ordered
    enum MultipleModelMethod
    {
        AVERAGE,
        MAJORITYVOTE,
        MAX,
        MEDIAN,
        MODELCHAIN,
        SELECTALL,
        SELECTFIRST,
        SUM,
        WEIGHTEDAVERAGE,
        WEIGHTEDMAJORITYVOTE,
        INVALID
    };
    namespace
    {
        const char * const MUTLIPLE_MODEL_METHOD_NAME[]
        {
            "average",
            "majorityVote",
            "max",
            "median",
            "modelChain",
            "selectAll",
            "selectFirst",
            "sum",
            "weightedAverage",
            "weightedMajorityVote"
        };

        MultipleModelMethod getMiningModelFromString(const char * name)
        {
            auto found = std::equal_range(MUTLIPLE_MODEL_METHOD_NAME, MUTLIPLE_MODEL_METHOD_NAME + static_cast<int>(INVALID), name, PMMLDocument::stringIsBefore);
            if (found.first != found.second)
            {
                return static_cast<MultipleModelMethod>(found.first - MUTLIPLE_MODEL_METHOD_NAME);
            }
            else
            {
                return INVALID;
            }
        }
    }
    
    
    size_t copyResultsFromSubModel(AstBuilder & builder, const PMMLDocument::ModelConfig & config, const PMMLDocument::ModelConfig & subModelConfig)
    {
        size_t blockSize = 0;
        if (config.idValueName)
        {
            builder.field(subModelConfig.idValueName);
            builder.assign(config.idValueName);
            ++blockSize;
        }
        
        if (config.outputValueName)
        {
            builder.field(subModelConfig.outputValueName);
            builder.assign(config.outputValueName);
            ++blockSize;
        }
        
        if (config.reasonCodeValueName)
        {
            builder.field(subModelConfig.reasonCodeValueName);
            builder.assign(config.reasonCodeValueName);
            ++blockSize;
        }
        
        if (config.bestProbabilityValueName)
        {
            builder.field(subModelConfig.bestProbabilityValueName);
            builder.assign(config.bestProbabilityValueName);
            ++blockSize;
        }
        
        // Propagate the probabilities
        for (const auto & pair : config.probabilityValueName)
        {
            auto foundTemp = subModelConfig.probabilityValueName.find(pair.first);
            if (foundTemp != subModelConfig.probabilityValueName.end())
            {
                builder.field(foundTemp->second);
                builder.defaultValue("0");
                builder.assign(pair.second);
                blockSize++;
            }
        }
        
        return blockSize;
    }
    
    size_t sumProbabilitiesFromSubModel(AstBuilder & builder, const PMMLDocument::ProbabilitiesOutputMap & probabilityValueName, const PMMLDocument::ProbabilitiesOutputMap & subProbabilityValueName, const char * weight)
    {
        size_t blockSize = 0;
        for (const auto & pair : probabilityValueName)
        {
            auto foundTemp = subProbabilityValueName.find(pair.first);
            if (foundTemp == subProbabilityValueName.end())
            {
                continue;
            }
            
            builder.field(pair.second);
            builder.defaultValue("0");
            
            builder.field(foundTemp->second);
            builder.defaultValue("0");
            
            if (weight)
            {
                // * weight
                builder.constant(weight, PMMLDocument::TYPE_NUMBER);
                builder.function(Function::functionTable.names.times, 2);
            }
            builder.function(Function::functionTable.names.plus, 2);
            
            builder.assign(pair.second);
            blockSize++;
        }
        return blockSize;
    }
    
    size_t setupAccumulatorsForProbabilities(AstBuilder & builder, PMMLDocument::ModelConfig & config, const MultipleModelMethod modelMethod)
    {
        size_t blockSize = 0;
        config.probabilityValueName = PMMLDocument::buildProbabilityOutputMap(builder.context(), "probabilities", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
        for (const auto & pair : config.probabilityValueName)
        {
            builder.constant(0);
            builder.declare(pair.second, AstBuilder::HAS_INITIAL_VALUE);
            blockSize++;
        }
        
        if (modelMethod != MAJORITYVOTE && modelMethod != WEIGHTEDMAJORITYVOTE)
        {
            config.confidenceValues = PMMLDocument::buildProbabilityOutputMap(builder.context(), "confidences", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
            for (const auto & pair : config.confidenceValues)
            {
                builder.constant(0);
                builder.declare(pair.second, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
            }
        }
        return blockSize;
    }
    
    // This deals with multiple blocks using "select first" It works whether it is regression or classification
    bool doSelectFirst(AstBuilder & builder, PMMLDocument::ModelConfig & config,
                       const tinyxml2::XMLElement * segmentation, const MultipleModelMethod)
    {
        ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
        
        size_t outerBlockSize = 0;
        if (config.idValueName)
        {
            builder.declare(config.idValueName, AstBuilder::NO_INITIAL_VALUE);
            outerBlockSize++;
        }
        
        if (config.outputValueName)
        {
            builder.declare(config.outputValueName, AstBuilder::NO_INITIAL_VALUE);
            outerBlockSize++;
        }
        
        if (config.reasonCodeValueName)
        {
            builder.declare(config.reasonCodeValueName, AstBuilder::NO_INITIAL_VALUE);
            outerBlockSize++;
        }
        
        if (config.bestProbabilityValueName)
        {
            builder.declare(config.bestProbabilityValueName, AstBuilder::NO_INITIAL_VALUE);
            outerBlockSize++;
        }
        
        if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
        {
            outerBlockSize += setupAccumulatorsForProbabilities( builder, config, SELECTFIRST);
        }
        
        int count = 0;
        for (const tinyxml2::XMLElement * segment = segmentation->FirstChildElement("Segment");
             segment != nullptr; segment = segment->NextSiblingElement("Segment"))
        {
            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(segment->FirstChildElement());
            if (predicate == nullptr)
            {
                fprintf(stderr, "Empty segment at %i\n", segment->GetLineNum());
                return false;
            }
            
            const tinyxml2::XMLElement * model = PMMLDocument::skipExtensions(predicate->NextSiblingElement());
            if (model == nullptr)
            {
                fprintf(stderr, "Segment has no model at %i\n", segment->GetLineNum());
                return false;
            }
            
            PMMLDocument::ModelConfig subModelConfig;
            if (config.idValueName)
            {
                subModelConfig.idValueName = builder.context().createVariable(config.idValueName->field.dataType, "id");
            }
            
            if (config.outputValueName)
            {
                subModelConfig.outputValueName = builder.context().createVariable(config.outputValueName->field.dataType, "outputValue");
            }
            
            if (config.reasonCodeValueName)
            {
                subModelConfig.reasonCodeValueName = builder.context().createVariable(config.reasonCodeValueName->field.dataType, "reasonCode");
            }
            
            if (config.bestProbabilityValueName)
            {
                config.bestProbabilityValueName = builder.context().createVariable(config.bestProbabilityValueName->field.dataType, "bestProbabilityValue");
            }
            
            subModelConfig.outputType = config.outputType;
            subModelConfig.function = config.function;
            
            // The child model outputs in the same way as the parent.
            if (!PMMLDocument::parseModel(builder, model, subModelConfig))
            {
                return false;
            }
            size_t innerBlockSize = 1 + copyResultsFromSubModel(builder, config, subModelConfig);
            
            builder.block(innerBlockSize);
            
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            
            count++;
        }

        builder.ifChain(count * 2); // 1 body + 1 predicate
        outerBlockSize++;
        builder.block(outerBlockSize);

        return true;
    }
    
    // This deals with multiple blocks using "select first", "model chain" or "select all". It works whether it is regression or classification
    bool doNonCombiningSegments(AstBuilder & builder, PMMLDocument::ModelConfig & config,
                                const tinyxml2::XMLElement * segmentation, const MultipleModelMethod modelMethod)
    {
        ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
        int count = 0;
        for (const tinyxml2::XMLElement * segment = segmentation->FirstChildElement("Segment");
             segment != nullptr; segment = segment->NextSiblingElement("Segment"))
        {
            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(segment->FirstChildElement());
            if (predicate == nullptr)
            {
                fprintf(stderr, "Empty segment at %i\n", segment->GetLineNum());
                return false;
            }
            
            const tinyxml2::XMLElement * model = PMMLDocument::skipExtensions(predicate->NextSiblingElement());
            if (model == nullptr)
            {
                fprintf(stderr, "Segment has no model at %i\n", segment->GetLineNum());
                return false;
            }
            
            if (modelMethod == MODELCHAIN && segment->NextSiblingElement("Segment") != nullptr)
            {
                PMMLDocument::ModelConfig subModuleConfig;
                // This model is used purely for chaining.
                if (!PMMLDocument::parseModel(builder, model, subModuleConfig))
                {
                    return false;
                }
            }
            else
            {
                // The child model outputs in the same way as the parent.
                if (!PMMLDocument::parseModel(builder, model, config))
                {
                    return false;
                }
            }
            
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            
            builder.ifChain(2);
            count++;
        }
        
        builder.block(count);
        
        return true;
    }
    
    size_t addCountBit(AstBuilder & builder, const AstNode & predicateNode, MultipleModelMethod modelMethod, PMMLDocument::ConstFieldDescriptionPtr countName, const char * weight, double & constCount)
    {
        if (modelMethod == WEIGHTEDAVERAGE || modelMethod == AVERAGE || modelMethod == MAJORITYVOTE || modelMethod == WEIGHTEDMAJORITYVOTE)
        {
            Analyser::AnalyserContext context;
            Analyser::TrivialValue trivValue = context.checkIfTrivial(predicateNode);
            if (trivValue == Analyser::ALWAYS_TRUE)
            {
                if (modelMethod == WEIGHTEDAVERAGE || modelMethod == WEIGHTEDMAJORITYVOTE)
                {
                    constCount += strtod(weight, nullptr);
                }
                else
                {
                    constCount++;
                }
            }
            else if (trivValue == Analyser::RUNTIME_EVALUATION_NEEDED)
            {
                // count = count
                builder.field(countName);
                builder.constant(modelMethod == WEIGHTEDAVERAGE ? weight : "1", countName->field.dataType);
                builder.function(Function::functionTable.names.plus, 2);
                builder.assign(countName);
                return 1;
            }
        }
        return 0;
    }

    // This handles all other multiple model methods for regression models.
    bool doRegressionSegments(AstBuilder & builder, PMMLDocument::ConstFieldDescriptionPtr outputValueName, PMMLDocument::FieldType outputType,
                              PMMLDocument::ConstFieldDescriptionPtr countName, const tinyxml2::XMLElement * segmentation, const MultipleModelMethod modelMethod,
                              double & constCount)
    {
        ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
        size_t blockSize = 0;
        for (const tinyxml2::XMLElement * segment = segmentation->FirstChildElement("Segment");
             segment != nullptr; segment = segment->NextSiblingElement("Segment"))
        {
            ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(segment->FirstChildElement());
            if (predicate == nullptr)
            {
                fprintf(stderr, "Empty segment at %i\n", segment->GetLineNum());
                return false;
            }
            
            const tinyxml2::XMLElement * model = PMMLDocument::skipExtensions(predicate->NextSiblingElement());
            if (model == nullptr)
            {
                fprintf(stderr, "Segment has no model at %i\n", segment->GetLineNum());
                return false;
            }

            const tinyxml2::XMLAttribute * weightAttr = segment->FindAttribute("weight");
            const char * weight = weightAttr == nullptr ? "1" : weightAttr->Value();

            PMMLDocument::ModelConfig subModelConfig;
            subModelConfig.outputValueName = builder.context().createVariable(outputType, "model_output");
            subModelConfig.outputType = outputType;
            subModelConfig.function = PMMLDocument::FUNCTION_REGRESSION;
            if (!PMMLDocument::parseModel(builder, model, subModelConfig))
            {
                return false;
            }
            size_t innerBlockSize = 1;
            
            if (modelMethod == SUM ||
                modelMethod == WEIGHTEDAVERAGE ||
                modelMethod == AVERAGE)
            {
                // outputValueName = (outputValueName or 0) + (tempVariable or 0)
                builder.field(outputValueName);
                builder.defaultValue("0");
                builder.field(subModelConfig.outputValueName);
                if (modelMethod == WEIGHTEDAVERAGE)
                {
                    // * weight
                    builder.constant(weight, outputType);
                    builder.function(Function::functionTable.names.times, 2);
                }
                builder.defaultValue("0"); // Applying default value to tempVariable
                builder.function(Function::functionTable.names.plus, 2);
                builder.assign(outputValueName);
                innerBlockSize++;
            }
            else if (modelMethod == MEDIAN)
            {
                builder.field(outputValueName);
                builder.field(subModelConfig.outputValueName);
                builder.function(Function::insertToTableDef, 2);
                innerBlockSize++;
            }
            else if (modelMethod == MAX)
            {
                builder.field(outputValueName);
                builder.defaultValue("0");
                builder.field(subModelConfig.outputValueName);
                builder.defaultValue("0");
                builder.function(Function::functionTable.names.max, 2);
                builder.assign(outputValueName);
                innerBlockSize++;
            }
            
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            
            AstNode predicateNode = builder.popNode();
            
            innerBlockSize += addCountBit(builder, predicateNode, modelMethod, countName, weight, constCount);
            
            if (innerBlockSize != 1)
            {
                builder.block(innerBlockSize);
            }
            
            // Add predicate back
            builder.pushNode(std::move(predicateNode));
            builder.ifChain(2); // A body and a predicate
            
            blockSize++;
        }
        
        builder.block(blockSize);
        
        return true;
    }
    
    // This handles all other multiple model methods for classification models.
    bool doClassificationSegments(AstBuilder & builder, PMMLDocument::ModelConfig & config, const tinyxml2::XMLElement * segmentation, const MultipleModelMethod modelMethod)
    {
        ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
        if (config.targetField == nullptr)
        {
            fprintf(stderr, "Cannot build mining model without target field %i\n", segmentation->GetLineNum());
            return false;
        }
        size_t outerBlockSize = 0;
        
        config.probabilityValueName = PMMLDocument::buildProbabilityOutputMap(builder.context(), "probabilities", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
        
        config.confidenceValues = PMMLDocument::buildProbabilityOutputMap(builder.context(), "confidence", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
        
        auto countVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "count");
        builder.constant(0);
        builder.declare(countVariable, AstBuilder::HAS_INITIAL_VALUE);
        outerBlockSize++;
        
        if (modelMethod == MAX)
        {
            if (config.bestProbabilityValueName == nullptr)
            {
                config.bestProbabilityValueName = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "bestProbabilityValueName");
            }
            builder.constant(0);
            builder.declare(config.bestProbabilityValueName, AstBuilder::HAS_INITIAL_VALUE);
            outerBlockSize++;
        }
        
        outerBlockSize += setupAccumulatorsForProbabilities(builder, config, modelMethod);
        
        double constCount = 0;
        
        
        for (const tinyxml2::XMLElement * segment = segmentation->FirstChildElement("Segment");
             segment != nullptr; segment = segment->NextSiblingElement("Segment"))
        {
            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(segment->FirstChildElement());
            if (predicate == nullptr)
            {
                fprintf(stderr, "Empty segment at %i\n", segment->GetLineNum());
                return false;
            }
            
            const tinyxml2::XMLElement * model = PMMLDocument::skipExtensions(predicate->NextSiblingElement());
            if (model == nullptr)
            {
                fprintf(stderr, "Segment has no model at %i\n", segment->GetLineNum());
                return false;
            }

            const tinyxml2::XMLAttribute * weightAttr = segment->FindAttribute("weight");
            const char * weight = weightAttr == nullptr ? "1" : weightAttr->Value();

            PMMLDocument::ScopedVariableDefinitionStackGuard variableScope(builder.context());
            
            PMMLDocument::ModelConfig subModelConfig;
            subModelConfig.outputType = config.outputType;
            subModelConfig.function = PMMLDocument::FUNCTION_CLASSIFICATION;

            size_t blockSize = 0;
            
            if (modelMethod == WEIGHTEDAVERAGE ||
                modelMethod == AVERAGE)
            {
                auto tempVariable = PMMLDocument::buildProbabilityOutputMap(builder.context(), "results", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
                
                subModelConfig.probabilityValueName = tempVariable;
                if (!PMMLDocument::parseModel(builder, model, subModelConfig))
                {
                    return false;
                }
                blockSize++;
                
                blockSize += sumProbabilitiesFromSubModel(builder, config.probabilityValueName, subModelConfig.probabilityValueName, modelMethod == WEIGHTEDAVERAGE ? weight : nullptr);
                blockSize += sumProbabilitiesFromSubModel(builder, config.confidenceValues, subModelConfig.confidenceValues, modelMethod == WEIGHTEDAVERAGE ? weight : nullptr);
            }
            else if (modelMethod == MAX)
            {
                subModelConfig.bestProbabilityValueName = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "best_prob");
                subModelConfig.probabilityValueName = PMMLDocument::buildProbabilityOutputMap(builder.context(), "results", PMMLDocument::TYPE_NUMBER, config.targetField->field.values);
                if (config.outputValueName)
                {
                    subModelConfig.outputValueName = builder.context().createVariable(config.outputValueName->field.dataType, "value");
                }
                
                if (!PMMLDocument::parseModel(builder, model, subModelConfig))
                {
                    return false;
                }
                
                blockSize++;
                
                size_t ltBlockSize = copyResultsFromSubModel(builder, config, subModelConfig);
                
                // Set the count of contributing scores to 1
                builder.constant(1);
                builder.assign(countVariable);
                ltBlockSize++;
                
                builder.block(ltBlockSize);
                
                // Condition for this (currentBest < this)
                builder.field(config.bestProbabilityValueName);
                builder.field(subModelConfig.bestProbabilityValueName);
                builder.function(Function::functionTable.names.lessThan, 2);
                
                size_t eqBlockSize = sumProbabilitiesFromSubModel(builder, config.probabilityValueName, subModelConfig.probabilityValueName, nullptr) +
                    sumProbabilitiesFromSubModel(builder, config.confidenceValues, subModelConfig.confidenceValues, nullptr);
                
                // Add together the number of variables that have contributed
                // count = count + 1
                builder.field(countVariable);
                builder.constant(1);
                builder.function(Function::functionTable.names.plus, 2);
                builder.assign(countVariable);
                eqBlockSize++;
                builder.block(eqBlockSize);
                
                // Condition for this (currentBest == this)
                builder.field(config.bestProbabilityValueName);
                builder.field(subModelConfig.bestProbabilityValueName);
                builder.function(Function::functionTable.names.equal, 2);
                
                if (subModelConfig.outputValueName)
                {
                    builder.field(config.outputValueName);
                    builder.field(subModelConfig.outputValueName);
                    builder.function(Function::functionTable.names.equal, 2);
                    builder.function(Function::functionTable.names.fnAnd, 2);
                }
                
                builder.ifChain(4);
                
                
                blockSize++;
            }
            else if (modelMethod == MAJORITYVOTE || modelMethod == WEIGHTEDMAJORITYVOTE)
            {
                auto tempVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "results");
                
                subModelConfig.outputValueName = tempVariable;
                if (!PMMLDocument::parseModel(builder, model, subModelConfig))
                {
                    return false;
                }
                blockSize++;
                
                // Increment the corresponding category score
                for (const auto & pair : config.probabilityValueName)
                {
                    // if notmissing(field) then
                    //    outputs[field] = outputs[field] + 1
                    builder.field(pair.second);
                    builder.defaultValue("0");
                    
                    if (modelMethod == MAJORITYVOTE)
                    {
                        builder.constant(1);
                    }
                    else
                    {
                        builder.constant(weight, PMMLDocument::TYPE_NUMBER);
                    }
                    builder.function(Function::functionTable.names.plus, 2);
                    builder.assign(pair.second);
                    
                    builder.field(tempVariable);
                    builder.constant(pair.first,  config.targetField->field.dataType);
                    builder.function(Function::functionTable.names.equal, 2);
                }
                    
                builder.ifChain(config.probabilityValueName.size() * 2); // 1 for body, 1 for predicate
                
                blockSize++;
            }
            
            
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            
            AstNode predicateNode = builder.popNode();
            blockSize += addCountBit(builder, predicateNode, modelMethod, countVariable, weight, constCount);
            
            builder.block(blockSize);
            
            // Add predicate back
            builder.pushNode(std::move(predicateNode));
            
            builder.ifChain(2);
            outerBlockSize++;
        }
    
        if (modelMethod != MAX)
        {
            // The unnormalized probabilities have been calculated, pick a winner from them
            // Max actually maintains a winner all along
            outerBlockSize += PMMLDocument::pickWinner(builder, config, config.probabilityValueName);
        }
        
        // And normalise
        if (modelMethod == WEIGHTEDAVERAGE || modelMethod == AVERAGE || modelMethod == MAJORITYVOTE || modelMethod == WEIGHTEDMAJORITYVOTE || modelMethod == MAX)
        {
            
            builder.field(countVariable);
            if (constCount != 0)
            {
                builder.constant(constCount);
                builder.function(Function::functionTable.names.plus, 2);
            }
            AstNode totalCount = builder.popNode();
            
            outerBlockSize += PMMLDocument::normalizeProbabilityArrayAccordingToFactor(builder, config.probabilityValueName, "normalized_probability", totalCount);
            outerBlockSize += PMMLDocument::normalizeProbabilityArrayAccordingToFactor(builder, config.confidenceValues, "normalized_confidence", totalCount);
        }
        
        
        builder.block(outerBlockSize);

        return true;
    }

    bool parseRegression(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config,
                         const char * method, const tinyxml2::XMLElement * segmentation)
    {
        const MultipleModelMethod modelMethod = getMiningModelFromString(method);
        switch(modelMethod)
        {
            case INVALID:
                fprintf(stderr, "Unknown multipleModelMethod: %s at %i\n", method, node->GetLineNum());
                return false;
            case MAJORITYVOTE:
            case WEIGHTEDMAJORITYVOTE:
                fprintf(stderr, "Method %s is not applicable to regression models at %i\n", method, node->GetLineNum());
                return false;
            case AVERAGE:
            case WEIGHTEDAVERAGE:
            {
                auto accumVariable = builder.context().createVariable(config.outputType, "accumulator");
                builder.constant(0);
                builder.declare(accumVariable, AstBuilder::HAS_INITIAL_VALUE);
                auto countVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "count");
                builder.constant(0);
                builder.declare(countVariable, AstBuilder::HAS_INITIAL_VALUE);
            
                double constCount = 0;
                if (!doRegressionSegments(builder, accumVariable, config.outputType, countVariable, segmentation, modelMethod, constCount))
                {
                    return false;
                }
                
                builder.field(accumVariable);
                builder.field(countVariable);
                // Add a constant count to the calculated count
                if (constCount > 0)
                {
                    builder.constant(constCount);
                    builder.function(Function::functionTable.names.sum, 2);
                }
                builder.function(Function::functionTable.names.divide, 2);
                builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                
                builder.block(4);
                return true;
            }
            case MEDIAN:
            {
                auto accumVariable = builder.context().createVariable(PMMLDocument::TYPE_TABLE, "accumulator");
                // Create a list of values
                builder.declare(accumVariable, AstBuilder::NO_INITIAL_VALUE);
                
                double constCount = 0;
                if (!doRegressionSegments(builder, accumVariable, config.outputType, nullptr, segmentation, modelMethod, constCount))
                {
                    return false;
                }
                
                // Sort the list
                builder.field(accumVariable);
                builder.function(Function::sortTableDef, 1);
                
                // Item below the centre (or centre itself if odd number)
                builder.field(accumVariable);
                builder.function(Function::listLengthDef, 1);
                builder.constant(1);
                builder.function(Function::functionTable.names.plus, 2);
                builder.constant(0.5);
                builder.function(Function::functionTable.names.times, 2);
                builder.function(Function::functionTable.names.floor, 1);
                builder.fieldIndirect(accumVariable, 1);
                
                // Item above the centre (or centre itself if odd number)
                builder.field(accumVariable);
                builder.function(Function::listLengthDef, 1);
                builder.constant(1);
                builder.function(Function::functionTable.names.plus, 2);
                builder.constant(0.5);
                builder.function(Function::functionTable.names.times, 2);
                builder.function(Function::functionTable.names.ceil, 1);
                builder.fieldIndirect(accumVariable, 1);
                
                // Take average.
                builder.function(Function::functionTable.names.plus, 2);
                builder.constant(0.5);
                builder.function(Function::functionTable.names.times, 2);
                builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                builder.block(4);
                return true;
            }
            case MAX:
            case SUM:
            {
                builder.constant(0);
                builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                double constCount = 0;
                if (!doRegressionSegments(builder, config.outputValueName, config.outputType, nullptr, segmentation, modelMethod, constCount))
                {
                    return false;
                }
                builder.block(2);
                return true;
            }
                
            case SELECTFIRST:
                return doSelectFirst(builder, config, segmentation, modelMethod);
                
            case SELECTALL:
            case MODELCHAIN:
                return doNonCombiningSegments(builder, config, segmentation, modelMethod);
        }
    }

    bool parseClassification(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config,
                             const char * method, const tinyxml2::XMLElement * segmentation)
    {
        const MultipleModelMethod modelMethod = getMiningModelFromString(method);
        switch(modelMethod)
        {
            case INVALID:
                fprintf(stderr, "Unknown multipleModelMethod: %s at %i\n", method, node->GetLineNum());
                return false;
            case SUM:
                fprintf(stderr, "Method %s is not applicable to classification models at %i\n", method, node->GetLineNum());
                return false;
            case MEDIAN:
                fprintf(stderr, "Method %s is not currently not supported for classification models at %i\n", method, node->GetLineNum());
                return false;

            case MAJORITYVOTE:
            case WEIGHTEDMAJORITYVOTE:
            case AVERAGE:
            case WEIGHTEDAVERAGE:
            case MAX:
                return doClassificationSegments(builder, config, segmentation, modelMethod);
                
            case SELECTFIRST:
                return doSelectFirst(builder, config, segmentation, modelMethod);
                
            case SELECTALL:
            case MODELCHAIN:
                return doNonCombiningSegments(builder, config, segmentation, modelMethod);
        }
    }
}

bool MiningModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    if (const tinyxml2::XMLElement * segmentation = node->FirstChildElement("Segmentation"))
    {
        const char * method = segmentation->Attribute("multipleModelMethod");
        if (method == nullptr)
        {
            fprintf(stderr, "No multipleModelMethod at %i\n", node->GetLineNum());
            return false;
        }

        if (config.function == PMMLDocument::FUNCTION_REGRESSION)
        {
            return parseRegression(builder, node, config, method, segmentation);
        }
        else
        {
            return parseClassification(builder, node, config, method, segmentation);
        }
    }

    fprintf(stderr, "No segmentation element in MiningModel at %i\n", node->GetLineNum());
    return false;
}
