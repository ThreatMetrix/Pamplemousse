//  Copyright 2018-2021 Lexis Nexis Risk Solutions
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
//  Created by Caleb Moore on 29/3/21.
//

#include <math.h>
#include "generalregressionmodel.hpp"
#include "conversioncontext.hpp"
#include "ast.hpp"
#include "regressionmodel.hpp"
#include "transformation.hpp"

static std::unordered_set<std::string>
readPredictorSet(const tinyxml2::XMLElement * list)
{
    std::unordered_set<std::string> predictorSet;
    if (list)
    {
        for (const tinyxml2::XMLElement * element = list->FirstChildElement("Predictor");
             element != nullptr; element = element->NextSiblingElement("Predictor"))
        {
            if (const char * name = element->Attribute("name"))
            {
                predictorSet.insert(name);
            }
        }
    }
    return predictorSet;
}

static bool
readPPCells(std::map<std::string, std::vector<AstNode>> & parameterFactors, std::map<std::string, std::vector<AstNode>> & parameterCovariates,
                 AstBuilder & builder, const tinyxml2::XMLElement * ppMatrix, const std::vector<std::pair<std::string, PMMLDocument::ConstFieldDescriptionPtr>> & parameters,
                 const std::unordered_set<std::string> & factors, const std::unordered_set<std::string> & covariates)
{
    for (const tinyxml2::XMLElement * element = ppMatrix->FirstChildElement("PPCell");
         element != nullptr; element = element->NextSiblingElement("PPCell"))
    {
        const char * parameterName = element->Attribute("parameterName");
        if (!parameterName)
        {
            builder.parsingError("No parameterName specified", element->GetLineNum());
            return false;
        }
        
        if (std::find_if(parameters.begin(), parameters.end(),
                         [&parameterName](const std::pair<std::string, PMMLDocument::ConstFieldDescriptionPtr> & pair)
                         { return pair.first == parameterName; }) == parameters.end())
        {
            builder.parsingError("parameterName not found in ParameterList", parameterName, element->GetLineNum());
            return false;
        }
        
        const char * predictorName = element->Attribute("predictorName");
        if (!predictorName)
        {
            builder.parsingError("No predictorName specified", element->GetLineNum());
            return false;
        }
        
        const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(predictorName);
        if (fieldDefinition == nullptr)
        {
            builder.parsingError("Unknown field specified", predictorName, element->GetLineNum());
            return false;
        }
        
        if (factors.find(predictorName) != factors.end())
        {
            const char * value = element->Attribute("value");
            if (value == nullptr)
            {
                builder.parsingError("No value specified", element->GetLineNum());
                return false;
            }
            builder.field(fieldDefinition);
            builder.constant(value, fieldDefinition->variable->field.dataType);
            builder.function(Function::functionTable.names.equal, 2);
            parameterFactors[parameterName].push_back(builder.popNode());
        }
        else if (covariates.find(predictorName) != covariates.end())
        {
            builder.field(fieldDefinition);
            parameterCovariates[parameterName].push_back(builder.popNode());
        }
        else
        {
            builder.parsingError("Predictor is neither a factor nor a covariate", predictorName, element->GetLineNum());
            return false;
        }
    }
    return true;
}

static void transferNodesToBuilder(AstBuilder & builder, std::vector<AstNode> && src, const Function::Definition & function)
{
    for (auto & astNode : src)
    {
        builder.pushNode(std::move(astNode));
    }
    
    if (src.size() > 1)
    {
        builder.function(function, src.size());
    }
    
    src.clear();
}

typedef std::vector<std::pair<std::string, PMMLDocument::ConstFieldDescriptionPtr>> ParametersVector;

static bool
readPCell(AstBuilder & builder, const tinyxml2::XMLElement * element, const ParametersVector & parameters)
{
    const char * parameterName = element->Attribute("parameterName");
    if (parameterName == nullptr)
    {
        builder.parsingError("parameterName not found", element->GetLineNum());
        return false;
    }
    
    auto iterator = std::find_if(parameters.begin(), parameters.end(),
                                 [&parameterName](const std::pair<std::string, PMMLDocument::ConstFieldDescriptionPtr> & pair)
                                 { return pair.first == parameterName; });
    if (iterator == parameters.end())
    {
        builder.parsingError("parameterName not found in ParameterList", parameterName, element->GetLineNum());
        return false;
    }
    
    const char * beta = element->Attribute("beta");
    if (beta == nullptr)
    {
        builder.parsingError("beta not found", element->GetLineNum());
        return false;
    }
    
    builder.constant(beta, PMMLDocument::TYPE_NUMBER);
    if (iterator->second)
    {
        builder.field(iterator->second);
        builder.function(Function::functionTable.names.times, 2);
    }
    return true;
}

static const tinyxml2::XMLElement *
findPCellForTarget(const tinyxml2::XMLElement * element, const char * targetCategory)
{
    while (element)
    {
        if (const char * thisTargetCategory = element->Attribute("targetCategory"))
        {
            if (targetCategory && strcmp(targetCategory, thisTargetCategory) == 0)
            {
                break;
            }
        }
        else
        {
            if (targetCategory == nullptr)
            {
                break;
            }
        }
        element = element->NextSiblingElement("PCell");
    }
    
    return element;
}

static bool
buildPRow(AstBuilder & builder, const tinyxml2::XMLElement * startElement, const ParametersVector & parameters, const char * targetCategory, PMMLDocument::ConstFieldDescriptionPtr common)
{
    size_t numberOfTerms = 0;
    if (common)
    {
        builder.field(common);
        ++numberOfTerms;
    }
    
    for (const tinyxml2::XMLElement * element = startElement; element; element = findPCellForTarget(element->NextSiblingElement("PCell"), targetCategory))
    {
        if (!readPCell(builder, element, parameters))
        {
            return false;
        }
        ++numberOfTerms;
    }

    builder.function(Function::functionTable.names.sum, numberOfTerms);
    return true;
}

static uint32_t
buildPPMatrix(AstBuilder & builder, ParametersVector & parameters, std::map<std::string, std::vector<AstNode>> && parameterFactors, std::map<std::string, std::vector<AstNode>> && parameterCovariates)
{
    size_t blockSize = 0;
    for (auto & parameter : parameters)
    {
        // Put the predicate of a ternary operator if there are any factors
        auto foundFactor = parameterFactors.find(parameter.first);
        auto foundCovariate = parameterCovariates.find(parameter.first);
        
        if (foundFactor != parameterFactors.end())
        {
            transferNodesToBuilder(builder, std::move(foundFactor->second), Function::functionTable.names.fnAnd);
        }
        else if (foundCovariate == parameterCovariates.end())
        {
            // This is an intercept. We don't need to do anything here.
            continue;
        }
        
        // If there are any covariates, then the value is the product.
        if (foundCovariate != parameterCovariates.end())
        {
            transferNodesToBuilder(builder, std::move(foundCovariate->second), Function::functionTable.names.times);
        }
        else
        {
            builder.constant(1);
        }
        
        // Finish the ternary if there are factors
        if (foundFactor != parameterFactors.end())
        {
            builder.constant(0);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        
        PMMLDocument::ConstFieldDescriptionPtr variable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, parameter.first);
        parameter.second = variable;
        builder.declare(variable, AstBuilder::HAS_INITIAL_VALUE);
        ++blockSize;
    }
    return blockSize;
}

bool GeneralRegressionModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    ParametersVector parameters;
    if (const tinyxml2::XMLElement * parameterList = node->FirstChildElement("ParameterList"))
    {
        for (const tinyxml2::XMLElement * element = parameterList->FirstChildElement("Parameter");
             element != nullptr; element = element->NextSiblingElement("Parameter"))
        {
            if (const char * name = element->Attribute("name"))
            {
                parameters.emplace_back(name, nullptr);
            }
        }
    }
    
    size_t blockSize = 0;
    if (const tinyxml2::XMLElement * ppMatrix = node->FirstChildElement("PPMatrix"))
    {
        std::unordered_set<std::string> factors = readPredictorSet(node->FirstChildElement("FactorList"));
        std::unordered_set<std::string> covariates = readPredictorSet(node->FirstChildElement("CovariateList"));
        std::map<std::string, std::vector<AstNode>> parameterFactors;
        std::map<std::string, std::vector<AstNode>> parameterCovariates;
        if (!readPPCells(parameterFactors, parameterCovariates, builder, ppMatrix, parameters, factors, covariates))
        {
            return false;
        }
        blockSize = buildPPMatrix(builder, parameters, std::move(parameterFactors), std::move(parameterCovariates));
    }
    
    RegressionModel::RegressionNormalizationMethod linkFunction = RegressionModel::METHOD_NONE;
    if (const char * linkFunctionC = node->Attribute("linkFunction"))
    {
        linkFunction = RegressionModel::getRegressionNormalizationMethodFromString(linkFunctionC);
        if (linkFunction == RegressionModel::METHOD_INVALID)
        {
            builder.parsingError("Invalid linkFunction", linkFunctionC, node->GetLineNum());
            return false;
        }
    }
    
    RegressionModel::RegressionNormalizationMethod cumulativeLink = RegressionModel::METHOD_NONE;
    if (const char * cumulativeLinkC = node->Attribute("cumulativeLink"))
    {
        cumulativeLink = RegressionModel::getRegressionNormalizationMethodFromString(cumulativeLinkC);
        if (cumulativeLink == RegressionModel::METHOD_INVALID)
        {
            builder.parsingError("Invalid cumulativeLink", cumulativeLinkC, node->GetLineNum());
            return false;
        }
    }
    
    if (const tinyxml2::XMLElement * pMatrix = node->FirstChildElement("ParamMatrix"))
    {
        if (config.function == PMMLDocument::FUNCTION_REGRESSION)
        {
            if (const tinyxml2::XMLElement * startElement = findPCellForTarget(pMatrix->FirstChildElement("PCell"), nullptr))
            {
                if (!buildPRow(builder, startElement, parameters, nullptr, PMMLDocument::ConstFieldDescriptionPtr()))
                {
                    return false;
                }
                RegressionModel::normalizeTable(builder, linkFunction, false);
                builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                ++blockSize;
            }
        }
        else
        {
            std::set<std::string> foundCategories;
            for (const tinyxml2::XMLElement * startElement = pMatrix->FirstChildElement("PCell");
                 startElement != nullptr; startElement = startElement->NextSiblingElement("PCell"))
            {
                if (const char * targetCategory = startElement->Attribute("targetCategory"))
                {
                    foundCategories.emplace(targetCategory);
                }
            }
                        
            const char * targetReferenceCategory = node->Attribute("targetReferenceCategory");
            // There may be another category that is 1 - SUM(others)
            // It is either absent or
            std::vector<std::string> missing_categories;
            std::set_difference(config.targetField->field.values.begin(), config.targetField->field.values.end(),
                                foundCategories.begin(), foundCategories.end(),
                                std::back_inserter(missing_categories));
            const bool has_reference_category = targetReferenceCategory != nullptr || !missing_categories.empty();
            
            PMMLDocument::ConstFieldDescriptionPtr common;
            // Find any common section
            if (const tinyxml2::XMLElement * startElement = findPCellForTarget(pMatrix->FirstChildElement("PCell"), nullptr))
            {
                if (!buildPRow(builder, startElement, parameters, nullptr, PMMLDocument::ConstFieldDescriptionPtr()))
                {
                    return false;
                }
                common = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "common");
                builder.declare(common, AstBuilder::HAS_INITIAL_VALUE);
                ++blockSize;
            }
            
            std::vector<PMMLDocument::ConstFieldDescriptionPtr> fields;
            // Now build several of these, one for each unique target category
            for (const auto & targetCategory : foundCategories)
            {
                if (!buildPRow(builder, findPCellForTarget(pMatrix->FirstChildElement("PCell"), targetCategory.c_str()), parameters, targetCategory.c_str(), common))
                {
                    return false;
                }
                PMMLDocument::ConstFieldDescriptionPtr field = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, targetCategory);
                fields.push_back(field);
                
                RegressionModel::normalizeTable(builder, linkFunction, has_reference_category);
                
                builder.declare(field, AstBuilder::HAS_INITIAL_VALUE);
                ++blockSize;
            }
            
            if (has_reference_category)
            {
                const char * reference = targetReferenceCategory ? targetReferenceCategory : missing_categories.front().c_str();
                builder.constant(1);
                for (const auto & field : fields)
                {
                    builder.field(field);
                }
                builder.function(Function::functionTable.names.minus, fields.size() + 1);
                builder.declare(PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities",
                                                                          PMMLDocument::TYPE_NUMBER, reference), AstBuilder::HAS_INITIAL_VALUE);
                ++blockSize;
            }
            
            blockSize += PMMLDocument::pickWinner(builder, config, config.probabilityValueName);
        }
    }
    
    builder.block(blockSize);
    
    return true;
}
