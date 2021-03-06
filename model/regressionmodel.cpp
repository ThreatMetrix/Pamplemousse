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
//  Created by Caleb Moore on 27/9/18.
//

#include "regressionmodel.hpp"
#include "ast.hpp"
#include "conversioncontext.hpp"
#include <algorithm>


namespace
{
    bool parseRegressionTable(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        double intercept;
        if (node->QueryDoubleAttribute("intercept", &intercept))
        {
            builder.parsingError("Intercept required", node->GetLineNum());
            return false;
        }

        size_t termsToAdd = 0;
        if (intercept != 0)
        {
            // Now start building the expression
            builder.constant(intercept);
            termsToAdd++;
        }
        for (const tinyxml2::XMLElement * element = PMMLDocument::skipExtensions(node->FirstChildElement());
             element; element = PMMLDocument::skipExtensions(element->NextSiblingElement()))
        {
            double coefficient;
            if (element->QueryDoubleAttribute("coefficient", &coefficient))
            {
                builder.parsingError("coefficient required", element->GetLineNum());
                return false;
            }

            if (coefficient == 0)
            {
                // Sometimes we get useless terms with a coefficient of zero.
            }
            else if (std::strcmp(element->Name(), "NumericPredictor") == 0)
            {
                const char * name = element->Attribute("name");
                if (name == nullptr)
                {
                    builder.parsingError("name required", element->GetLineNum());
                    return false;
                }
                const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(name);
                if (fieldDefinition == nullptr)
                {
                    builder.parsingError("Unknown field referenced in NumericPredictor", name, element->GetLineNum());
                    return false;
                }
                
                // PMML restricts exponent to an INT, but I can't see any problem with allowing floating points here.
                double exponent;
                if (element->QueryDoubleAttribute("exponent", &exponent))
                {
                    exponent = 1;
                }
                
                builder.field(fieldDefinition);
                // Default on a term-by-term basis, rather than invalidating the whole thing.
                builder.defaultValue("0");

                if (exponent != 1)
                {
                    builder.constant(exponent);
                    builder.function(Function::functionTable.names.pow, 2);
                }
                
                if (coefficient != 1)
                {
                    builder.constant(coefficient);
                    builder.function(Function::functionTable.names.times, 2);
                }
                termsToAdd++;
            }
            else if (std::strcmp(element->Name(), "CategoricalPredictor") == 0)
            {
                if (!RegressionModel::buildCatagoricalPredictor(builder, element, coefficient))
                {
                    return false;
                }
                termsToAdd++;
            }
            else if (std::strcmp(element->Name(), "PredictorTerm") == 0)
            {
                size_t factors = 0;
                if (coefficient != 1)
                {
                    builder.constant(coefficient);
                    factors++;
                }
                
                for (const tinyxml2::XMLElement * fieldRef = element->FirstChildElement("FieldRef");
                     fieldRef; fieldRef = fieldRef->NextSiblingElement("FieldRef"))
                {
                    const char * name = fieldRef->Attribute("field");
                    if (name == nullptr)
                    {
                        builder.parsingError("field required", element->GetLineNum());
                        return false;
                    }
                    const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(name);
                    if (fieldDefinition == nullptr)
                    {
                        builder.parsingError("Unknown field referenced in PredictorTerm", name, element->GetLineNum());
                        return false;
                    }

                    builder.field(fieldDefinition);
                    factors++;
                }
                
                if (factors > 0)
                {
                    builder.function(Function::functionTable.names.product, factors);
                    termsToAdd++;
                }
            }
        }
        
        if (termsToAdd > 1)
        {
            builder.function(Function::functionTable.names.sum, termsToAdd);
        }
        else if (termsToAdd == 0)
        {
            builder.constant(0);
        }
        else
        {
            // already have the right number of items.
        }
        
        return true;
    }
    
    bool doRegressionClassificationMax(AstBuilder & builder, const tinyxml2::XMLElement * node,
                                       PMMLDocument::ModelConfig & config, RegressionModel::RegressionNormalizationMethod normMethod, size_t & blockSize)
    {
        std::vector<std::string> categoryNames;
        for (const tinyxml2::XMLElement * regressionTable = node->FirstChildElement("RegressionTable");
             regressionTable; regressionTable = regressionTable->NextSiblingElement("RegressionTable"))
        {
            if (const char * targetCategory = regressionTable->Attribute("targetCategory"))
            {
                if (!parseRegressionTable(builder, regressionTable))
                {
                    return false;
                }
                normalizeTable(builder, normMethod == RegressionModel::METHOD_SOFTMAX ? RegressionModel::METHOD_EXP : RegressionModel::METHOD_NONE, false);
                builder.declare(PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, targetCategory), AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
                categoryNames.push_back(targetCategory);
            }
            else
            {
                builder.parsingError("targetCategory required", regressionTable->GetLineNum());
                return false;
            }
        }
        
        blockSize += PMMLDocument::normaliseProbabilitiesAndPickWinner(builder, config);
        
        return true;
    }

    bool doRegressionClassificationNonmax(AstBuilder & builder, const tinyxml2::XMLElement * node,
                                          PMMLDocument::ModelConfig & config, RegressionModel::RegressionNormalizationMethod normMethod, bool binaryField, size_t & blockSize)
    {
        std::vector<std::string> categoryNames;
        for (const tinyxml2::XMLElement * regressionTable = node->FirstChildElement("RegressionTable");
             regressionTable; regressionTable = regressionTable->NextSiblingElement("RegressionTable"))
        {
            if (const char * targetCategory = regressionTable->Attribute("targetCategory"))
            {
                // For methods that are not soft/simple max the final table is never really evaluated, but is generated from the others.
                if (regressionTable->NextSiblingElement("RegressionTable"))
                {
                    if (!parseRegressionTable(builder, regressionTable))
                    {
                        return false;
                    }
                    normalizeTable(builder, normMethod, binaryField);
                }
                else
                {
                    // Create a special final entry that is derived from subtracting the others from one
                    builder.constant(1);
                    size_t nElements = 1;
                    for (const std::string & category : categoryNames)
                    {
                        auto found = config.probabilityValueName.find(category);
                        if (found != config.probabilityValueName.end())
                        {
                            builder.field(found->second);
                            builder.defaultValue("0");
                            nElements++;
                        }
                    }
                    builder.function(Function::functionTable.names.minus, nElements);
                }
                
                builder.declare(PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities", PMMLDocument::TYPE_NUMBER, targetCategory), AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
                categoryNames.push_back(targetCategory);
            }
            else
            {
                builder.parsingError("targetCategory required", regressionTable->GetLineNum());
                return false;
            }
        }

        blockSize += PMMLDocument::pickWinner(builder, config, config.probabilityValueName);
        
        return true;
    }

    bool doRegressionClassification(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ProbabilitiesOutputMap &,
                                    PMMLDocument::ModelConfig & config, RegressionModel::RegressionNormalizationMethod normMethod, size_t & blockSize)
    {
        if (normMethod != RegressionModel::METHOD_SIMPLEMAX && normMethod != RegressionModel::METHOD_SOFTMAX)
        {
            // PMML specified special treatment for "binary fields", i.e. catagorical fields with two values.
            bool binaryField = config.targetField != nullptr && config.targetField->field.opType == PMMLDocument::OPTYPE_CATEGORICAL && config.targetField->field.values.size() == 2;
            return doRegressionClassificationNonmax(builder, node, config, normMethod, binaryField, blockSize);
        }
        else
        {
            return doRegressionClassificationMax(builder, node, config, normMethod, blockSize);
        }
    }


    const char * const REGRESSIONNORMALIZATIONMETHOD[static_cast<int>(RegressionModel::METHOD_INVALID)] =
    {
        "cauchit",
        "cloglog",
        "exp",
        "identity",
        "log",
        "logc",
        "logit",
        "loglog",
        "none",
        "probit",
        "simplemax",
        "softmax"
    };

}

RegressionModel::RegressionNormalizationMethod RegressionModel::getRegressionNormalizationMethodFromString(const char * name)
{
    auto found = std::equal_range(REGRESSIONNORMALIZATIONMETHOD, REGRESSIONNORMALIZATIONMETHOD + static_cast<int>(METHOD_INVALID), name, PMMLDocument::stringIsBefore);
    if (found.first != found.second)
    {
        return static_cast<RegressionNormalizationMethod>(found.first - REGRESSIONNORMALIZATIONMETHOD);
    }
    else
    {
        return METHOD_INVALID;
    }
}

// Add the normalization function to a regression table.
void RegressionModel::normalizeTable(AstBuilder & builder, RegressionModel::RegressionNormalizationMethod normMethod, bool clamp)
{
    switch (normMethod)
    {
        case METHOD_CAUCHIT:
            // predictedValue = 0.5 + (1/π) arctan(y1)
            builder.function(Function::functionTable.names.atan, 1);
            // HACK: this will need to be done properly if this ever outputs anything but Lua.
            builder.constant("(1 / math.pi)", PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.times, 2);
            builder.constant(0.5);
            builder.function(Function::functionTable.names.plus, 2);
            break;
            
        case METHOD_CLOGLOG:
            // predictedValue = 1 - exp( -exp( y1))
            builder.function(Function::functionTable.names.exp, 1);
            builder.function(Function::unaryMinus, 1);
            builder.function(Function::functionTable.names.exp, 1);
            builder.constant(1);
            builder.swapNodes(-1, -2);
            builder.function(Function::functionTable.names.minus, 2);
            break;
            
        case METHOD_LOGC:
            // predictedValue = 1 - exp(y1)
            builder.function(Function::functionTable.names.exp, 1);
            builder.constant(1);
            builder.swapNodes(-1, -2);
            builder.function(Function::functionTable.names.minus, 2);
            break;
            
        case METHOD_EXP:
        case METHOD_LOG:
            // predictedValue = exp(y1)
            builder.function(Function::functionTable.names.exp, 1);
            break;
            
            // Softmax is treated like logit in regression models.
            // in classification models, its treated as "METHOD_EXP"
        case METHOD_SOFTMAX:
        case METHOD_LOGIT:
            // predictedValue = 1/(1+exp(-y1))
            builder.function(Function::unaryMinus, 1);
            builder.function(Function::functionTable.names.exp, 1);
            builder.constant(1);
            builder.function(Function::functionTable.names.plus, 2);
            builder.constant(1);
            builder.swapNodes(-1, -2);
            builder.function(Function::functionTable.names.divide, 2);
            break;
            
        case METHOD_LOGLOG:
            // predictedValue = exp( -exp( -y1))
            builder.function(Function::unaryMinus, 1);
            builder.function(Function::functionTable.names.exp, 1);
            builder.function(Function::unaryMinus, 1);
            builder.function(Function::functionTable.names.exp, 1);
            break;
            
        case METHOD_PROBIT:
            builder.function(Function::functionTable.names.stdNormalCDF, 1);
            break;
            
            // These are all no-ops or unsupported.
        case METHOD_NONE:
        case METHOD_IDENTITY:
        case METHOD_SIMPLEMAX:
        case METHOD_INVALID:
            // predictedValue = y1
            break;
    }
    
    if (clamp)
    {
        builder.constant(1);
        builder.function(Function::functionTable.names.min, 2);
        builder.constant(0);
        builder.function(Function::functionTable.names.max, 2);
    }
}


bool RegressionModel::buildCatagoricalPredictor(AstBuilder & builder, const tinyxml2::XMLElement * element, double coefficient)
{
    const char * name = element->Attribute("name");
    const char * value = element->Attribute("value");
    if (name == nullptr || value == nullptr)
    {
        builder.parsingError("name and value required", element->GetLineNum());
        return false;
    }
    const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(name);
    if (fieldDefinition == nullptr)
    {
        builder.parsingError("Unknown field referenced in CategoricalPredictor", name, element->GetLineNum());
        return false;
    }
    
    builder.field(fieldDefinition);
    builder.constant(value, builder.topNode().type);
    builder.function(Function::functionTable.names.equal, 2);
    builder.constant(coefficient);
    builder.constant(0);
    builder.function(Function::functionTable.names.ternary, 3);
    return true;
}

bool RegressionModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    RegressionNormalizationMethod normMethod = METHOD_NONE;
    if (const char * methodName = node->Attribute("normalizationMethod"))
    {
        normMethod = getRegressionNormalizationMethodFromString(methodName);
        if (normMethod == METHOD_INVALID)
        {
            builder.parsingError("Unknown normalizationMethod", methodName, node->GetLineNum());
            return false;
        }
    }

    if (config.function == PMMLDocument::FUNCTION_REGRESSION)
    {
        if (const tinyxml2::XMLElement * regressionTable = node->FirstChildElement("RegressionTable"))
        {
            if (!parseRegressionTable(builder, regressionTable))
            {
                return false;
            }
            normalizeTable(builder, normMethod, false);
            builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
        }
        else
        {
            builder.parsingError("No regression table", node->GetLineNum());
            return false;
        }
    }
    else
    {
        size_t blockSize = 0;
        
        if (!doRegressionClassification(builder, node, config.probabilityValueName, config, normMethod, blockSize))
        {
            return false;
        }
        
        builder.block(blockSize);
    }
    
    
    return true;
}
