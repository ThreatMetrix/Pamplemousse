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

#include "document.hpp"
#include "ast.hpp"
#include "conversioncontext.hpp"
#include "model/generalregressionmodel.hpp"
#include "model/miningmodel.hpp"
#include "model/naivebayesmodel.hpp"
#include "model/neuralnetworkmodel.hpp"
#include "model/output.hpp"
#include "model/regressionmodel.hpp"
#include "model/rulesetmodel.hpp"
#include "model/scorecardmodel.hpp"
#include "model/supportvectormachine.hpp"
#include "model/transformation.hpp"
#include "model/treemodel.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/optimiser.hpp"

#include <iostream>

namespace PMMLDocument
{
namespace
{
bool parseDataDictionary(const AstBuilder & builder, const tinyxml2::XMLElement * dataDictionary, DataFieldVector & dataDictionaryOut)
{
     for (const tinyxml2::XMLElement * element = dataDictionary->FirstChildElement("DataField");
             element; element = element->NextSiblingElement("DataField"))
    {
        const char * type = element->Attribute("dataType");
        const char * name = element->Attribute("name");
        const char * optype = element->Attribute("optype");
        if (type == nullptr ||
            name == nullptr||
            optype == nullptr)
        {
            builder.parsingError("DataField missing name or dataType", element->GetLineNum());
            return false;
        }

        FieldType fieldType = dataTypeFromString(type);
        if (fieldType == TYPE_INVALID)
        {
            builder.parsingError("DataField has unknown data type", type, element->GetLineNum());
            return false;
        }

        PMMLDocument::OpType opType = PMMLDocument::optypeFromString(optype);
        if (opType == OPTYPE_INVALID)
        {
            builder.parsingError("DataField has unknown optype", optype, element->GetLineNum());
            return false;
        }
        PMMLDocument::DataField dataField(fieldType, opType);
        for (const tinyxml2::XMLElement * value = element->FirstChildElement("Value");
             value; value = value->NextSiblingElement("Value"))
        {
            if (const char * stringVar = value->Attribute("value"))
            {
                dataField.values.emplace_back(stringVar);
            }
        }
        dataDictionaryOut.emplace_back(name, dataField);
    }
    return true;
}


void findAllInputs(const tinyxml2::XMLElement * element, std::unordered_set<std::string> & names, std::unordered_set<std::string> & outputs)
{
    if (const tinyxml2::XMLElement * miningSchema = element->FirstChildElement("MiningSchema"))
    {
        for (const tinyxml2::XMLElement * miningField = miningSchema->FirstChildElement("MiningField");
             miningField; miningField = miningField->NextSiblingElement("MiningField"))
        {
            // Note, this first pass doesn't care about errors... they will be silently ignored
            if (const char * name = miningField->Attribute("name"))
            {
                MiningFieldUsage usage = getMiningFieldUsage(miningField);
                if (usage == USAGE_IN)
                {
                    names.insert(name);
                }
                else if (usage == USAGE_OUT)
                {
                    outputs.insert(name);
                }
            }
        }
    }

    for (const tinyxml2::XMLElement * iterator = element->FirstChildElement();
         iterator; iterator = iterator->NextSiblingElement())
    {
        findAllInputs(iterator, names, outputs);
    }
}
}
}

bool PMMLDocument::convertPMML(AstBuilder & builder, const tinyxml2::XMLElement * documentRoot)
{
    const tinyxml2::XMLElement * header = documentRoot->FirstChildElement();
    if (header == nullptr)
    {
        builder.parsingError("Header is not present", documentRoot->GetLineNum());
        return false;
    }
    if (strcmp(header->Name(), "Header"))
    {
        builder.parsingError("Instead of header, found", header->Name(), documentRoot->GetLineNum());
        return false;
    }
    if (const tinyxml2::XMLElement * applicationField = header->FirstChildElement("Application"))
    {
        if (const char * applicationName = applicationField->Attribute("name"))
        {
            builder.context().setApplication(applicationName);
        }
    }
    
    const tinyxml2::XMLElement * dataDictionary = header->NextSiblingElement();
    while (dataDictionary)
    {
        if (strcmp(dataDictionary->Name(), "DataDictionary") == 0)
        {
            break;
        }
        dataDictionary = dataDictionary->NextSiblingElement();
    }
    
    if (dataDictionary)
    {
        DataFieldVector dataDictionaryOut;
        if (!parseDataDictionary(builder, dataDictionary, dataDictionaryOut))
        {
            return false;
        }
        std::unordered_set<std::string> activeFields;
        std::unordered_set<std::string> outFields;
        findAllInputs(documentRoot, activeFields, outFields);
        builder.context().setupInputs(dataDictionaryOut, activeFields, outFields);
    }
    else
    {
        builder.parsingError("Data dictionary is not present", documentRoot->GetLineNum());
        return false;
    }
    
    DataFieldVector outputs = Output::findAllOutputs(documentRoot);
    if (!outputs.empty())
    {
        builder.context().setupOutputs(outputs);
    }

    const tinyxml2::XMLElement * model = dataDictionary->NextSiblingElement();

    PMMLDocument::ScopedVariableDefinitionStackGuard scopedGuard(builder.context());

    size_t blockSize = 0;
    if (model != nullptr && std::strcmp("TransformationDictionary", model->Name()) == 0)
    {
        if (!Transformation::parseTransformationDictionary(builder, model, scopedGuard, blockSize))
        {
            return false;
        }
        model = model->NextSiblingElement();
    }

    if (model == nullptr)
    {
        builder.parsingError("Model is not present\n", documentRoot->GetLineNum());
        return false;
    }
    
    ModelConfig config;
    {
        // This thing is only used to get the target / predicted value from the mining schema.
        MiningSchemaStackGuard miningSchema(builder.context(), model->FirstChildElement("MiningSchema"));
        if (!miningSchema.isValid())
        {
            return false;
        }
        if (auto target = miningSchema.getTargetName())
        {
            config.outputType = target->field.dataType;
            config.outputValueName = target;
        }
    }
    
    if (!parseModel(builder, model, config))
    {
        return false;
    }
    blockSize++;
    builder.block(blockSize);
    
    if (Function::prologue(builder))
    {
        builder.swapNodes(-1, -2);
        builder.block(2);
    }
    
    return true;
}

namespace  PMMLDocument
{
bool parseModelInternal(AstBuilder & builder, const tinyxml2::XMLElement * node,
                        PMMLDocument::ModelConfig & modelConfig)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    const char * name = node->Name();
    if (strcmp(name, "MiningModel") == 0)
    {
        if (!MiningModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "TreeModel") == 0)
    {
        if (!TreeModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "RegressionModel") == 0)
    {
        if (!RegressionModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "Scorecard") == 0)
    {
        if (!ScorecardModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "NeuralNetwork") == 0)
    {
        if (!NeuralNetworkModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "SupportVectorMachineModel") == 0)
    {
        if (!SupportVectorMachine::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "RuleSetModel") == 0)
    {
        if (!RulesetModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "NaiveBayesModel") == 0)
    {
        if (!NaiveBayesModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else if (strcmp(name, "GeneralRegressionModel") == 0)
    {
        if (!GeneralRegressionModel::parse(builder, node, modelConfig))
        {
            return false;
        }
    }
    else
    {
        builder.parsingError("Unknown or unsupported model type", name, node->GetLineNum());
        return false;
    }
    return true;
}
}

bool PMMLDocument::parseModel(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & modelConfig)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);

    const char * functionName = node->Attribute("functionName");
    if (functionName == nullptr)
    {
        builder.parsingError("No function name specified", node->GetLineNum());
        return false;
    }

    // Work out what function this model is
    MiningFunction function;
    if (strcmp("regression", functionName) == 0)
    {
        function = FUNCTION_REGRESSION;
    }
    else if (strcmp("classification", functionName) == 0)
    {
        function = FUNCTION_CLASSIFICATION;
    }
    else
    {
        builder.parsingError("Unknown or unsupported model function", functionName, node->GetLineNum());
        return false;
    }

    // Note that model is passed by value and modified heavily by this function.
    // This is the variable name where the model being passed will be output
    if (modelConfig.outputValueName == nullptr)
    {
        if (const char * predictedValue = Output::findOutputForFeature(node, "predictedValue", false))
        {
            modelConfig.outputValueName = builder.context().getFieldDescription(predictedValue);
        }

        if (modelConfig.outputValueName == nullptr &&
            function == FUNCTION_REGRESSION)
        {
            // There is no guarentee that this will do anything useful. But regression models assume this to be populated.
            modelConfig.outputValueName = builder.context().createVariable(TYPE_NUMBER, "output", ORIGIN_OUTPUT);
            return false;
        }
    }

    // Same for entity ID
    if (modelConfig.idValueName == nullptr)
    {
        if (const char * entityId = Output::findOutputForFeature(node, "entityId", false))
        {
            modelConfig.idValueName = builder.context().getFieldDescription(entityId);
        }
    }

    // Same for probability map
    if (modelConfig.bestProbabilityValueName == nullptr)
    {
        if (const char * entityId = Output::findOutputForFeature(node, "probability", true))
        {
            modelConfig.bestProbabilityValueName = builder.context().getFieldDescription(entityId);
        }
    }

    // Has the caller constrained this model?
    if (modelConfig.function == FUNCTION_ANY)
    {
        modelConfig.function = function;
    }
    else if (modelConfig.function != function)
    {
        builder.parsingError("Unexpected functionName", functionName, node->GetLineNum());
        return false;
    }

    // Parse mining schema for this scope.
    MiningSchemaStackGuard miningSchema(builder.context(), node->FirstChildElement("MiningSchema"));
    if (!miningSchema.isValid())
    {
        return false;
    }
    
    ScopedVariableDefinitionStackGuard scope(builder.context());

    size_t blocksize = 0;
    Transformation::importTransformationDictionary(builder, scope, blocksize);

    if (const tinyxml2::XMLElement * transformations = node->FirstChildElement("LocalTransformations"))
    {
        if (!Transformation::parseLocalTransformations(builder, transformations, scope, blocksize))
        {
            return false;
        }
    }

    if (auto target = miningSchema.getTargetName())
    {
        modelConfig.outputType = target->field.dataType;
        modelConfig.targetField = target;
    }

    if (modelConfig.outputType == TYPE_INVALID)
    {
        if (function == FUNCTION_REGRESSION)
        {
            modelConfig.outputType = TYPE_NUMBER;
        }
        else
        {
            modelConfig.outputType = TYPE_STRING;
        }
    }

    if (modelConfig.reasonCodeValueName == nullptr &&
        Output::findOutputForFeature(node, "reasonCode", false) != nullptr)
    {
        // Reason codes cannot directly write into an output, so create a temp variable to store them.
        modelConfig.reasonCodeValueName = builder.context().createVariable(TYPE_STRING_TABLE, "reason_codes");
        builder.declare(modelConfig.reasonCodeValueName, AstBuilder::NO_INITIAL_VALUE);
        blocksize++;
    }

    if (!parseModelInternal(builder, node, modelConfig))
    {
        return false;
    }
    blocksize++;
    
    if (!Output::addOutputValues(builder, node, modelConfig, blocksize))
    {
        return false;
    }
    
    builder.block(blocksize);
    return true;
}

PMMLDocument::ProbabilitiesOutputMap
PMMLDocument::buildProbabilityOutputMap(PMMLDocument::ConversionContext & context, const char * name, PMMLDocument::FieldType type,
                                        const std::vector<std::string> & values)
{
    PMMLDocument::ProbabilitiesOutputMap out;
    for (const auto & value : values)
    {
        out.emplace(value, context.createVariable(type, std::string(name) + "_" + value));
    }
    return out;
}

PMMLDocument::ConstFieldDescriptionPtr
PMMLDocument::getOrAddCategoryInOutputMap(PMMLDocument::ConversionContext & context, PMMLDocument::ProbabilitiesOutputMap & probsOutput,
                                          const char * name, PMMLDocument::FieldType, const std::string & value)
{
    auto categoryOutput = probsOutput.emplace(value, nullptr);
    // If there was nothing looking for this value, create a new variable.
    if (categoryOutput.second)
    {
        categoryOutput.first->second = context.createVariable(PMMLDocument::TYPE_NUMBER, std::string(name) + "_" + value);
    }
    return categoryOutput.first->second;
}

size_t PMMLDocument::pickWinner(AstBuilder & builder, ModelConfig & config, const ProbabilitiesOutputMap & probabilitiesOutputMap)
{
    size_t blockSize = 0;
    // Pick winner:
    if (config.outputValueName || config.bestProbabilityValueName)
    {
        // Assign the best probability to the first.
        auto bestProb = config.bestProbabilityValueName;
        ProbabilitiesOutputMap::const_iterator iter = probabilitiesOutputMap.begin();
        builder.field(iter->second);
        builder.defaultValue("0");
        if (bestProb)
        {
            builder.assign(bestProb);
        }
        else
        {
            bestProb = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "best_probability");
            builder.declare(bestProb, AstBuilder::HAS_INITIAL_VALUE);
        }
        ++blockSize;
        
        if (config.outputValueName && iter != probabilitiesOutputMap.end())
        {
            // The winner gets selected as not null if its probability is not null
            builder.field(iter->second);
            builder.function(Function::functionTable.names.isNotMissing, 1);
            builder.constant(iter->first, config.outputType);
            builder.function(Function::boundFunction, 2);
            builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
            blockSize++;
        }
        ++iter;
        // Create an if statement for each subsequent value.
        // best = prob[first]
        // if prob[second] > best
        //   best = prob[second]
        // etc.
        for (; iter != probabilitiesOutputMap.end(); ++iter)
        {
            builder.field(iter->second);
            builder.assign(bestProb);
            if (config.outputValueName)
            {
                builder.constant(iter->first, config.outputType);
                builder.assign(config.outputValueName);
                builder.block(2); // Block this assign together with "builder.assign(bestProb)"
            }
            builder.field(bestProb);
            builder.field(iter->second);
            builder.function(Function::functionTable.names.lessThan, 2);
            builder.ifChain(2);
            blockSize++;
        }
    }
    return blockSize;
}

// This uses the gathered raw probabilities to select a winner and/or normalise the probabilities.
size_t PMMLDocument::normaliseProbabilitiesAndPickWinner(AstBuilder & builder, ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    // If nobody wants a winner or normalization to happen, nothing needs to be done.
    
    size_t blockSize = pickWinner(builder, config, config.probabilityValueName);
    
    auto totalScore = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "total_score");
    for (const auto & pair : config.probabilityValueName)
    {
        builder.field(pair.second);
    }
    builder.function(Function::functionTable.names.sum, config.probabilityValueName.size());
    builder.declare(totalScore, AstBuilder::HAS_INITIAL_VALUE);
    blockSize++;
    
    builder.field(totalScore);
    AstNode totalScoreNode = builder.popNode();
    
    blockSize += normalizeProbabilityArrayAccordingToFactor(builder, config.probabilityValueName, "normalized_probability", totalScoreNode);
    
    return blockSize;
}

size_t PMMLDocument::normalizeProbabilityArrayAccordingToFactor(AstBuilder & builder, ProbabilitiesOutputMap & probabilityValueName, const char * varName, AstNode factor)
{
    size_t blockSize = 0;
    ProbabilitiesOutputMap normalizedProbabilitiesOutputMap;
    for (auto & pair : probabilityValueName)
    {
        builder.field(pair.second);
        builder.pushNode(factor);
        builder.function(Function::functionTable.names.divide, 2);
        
        PMMLDocument::ConstFieldDescriptionPtr normalizedProbability = getOrAddCategoryInOutputMap(builder.context(), normalizedProbabilitiesOutputMap, varName, PMMLDocument::TYPE_NUMBER, pair.first);
        builder.declare(normalizedProbability, AstBuilder::HAS_INITIAL_VALUE );
        blockSize++;
    }
    probabilityValueName = std::move(normalizedProbabilitiesOutputMap);
    return blockSize;
}
