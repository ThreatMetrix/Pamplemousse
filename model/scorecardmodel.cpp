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
//  Created by Caleb Moore on 29/9/18.
//

#include "scorecardmodel.hpp"
#include "conversioncontext.hpp"
#include "predicate.hpp"
#include "transformation.hpp"

// This generates Lua based on a scorecard, the simplest model (and possibly least useful) type.
bool ScorecardModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    const double initialScore = node->DoubleAttribute("initialScore", 0);
    double defaultBaseline = 0;
    const bool hasDefaultBaseline = node->QueryDoubleAttribute("baselineScore", &defaultBaseline) == tinyxml2::XML_SUCCESS;
    const bool useReasonCodes = node->BoolAttribute("useReasonCodes", true);
    bool reasonCodeAlgorithmIsAbove = false;
    if (const char * reasonCodeAlgorithm = node->Attribute("reasonCodeAlgorithm"))
    {
        if (strcmp(reasonCodeAlgorithm, "pointsAbove") == 0)
        {
            reasonCodeAlgorithmIsAbove = true;
        }
        else if (strcmp(reasonCodeAlgorithm, "pointsBelow"))
        {
            fprintf(stderr, "unrecognised reasonCodeAlgorithm: %s at %i\n", reasonCodeAlgorithm, node->GetLineNum());
            return false;
        }
    }

    builder.constant(initialScore);
    builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
    size_t blockSize = 1;
    
    const tinyxml2::XMLElement * characteristics = node->FirstChildElement("Characteristics");
    if (characteristics == nullptr)
    {
        fprintf(stderr, "no characteristics in scorecard at %i\n", node->GetLineNum());
        return false;
    }

    for (const tinyxml2::XMLElement * characteristic = characteristics->FirstChildElement("Characteristic");
         characteristic; characteristic = characteristic->NextSiblingElement("Characteristic"))
    {
        size_t ifChainLength = 0;
        double baseline = defaultBaseline;
        const char * masterReasonCode = characteristic->Attribute("reasonCode");
        if (useReasonCodes)
        {
            if (characteristic->QueryDoubleAttribute("baselineScore", &baseline) != tinyxml2::XML_SUCCESS && !hasDefaultBaseline)
            {
                fprintf(stderr, "Characteristic with no baseline at %i\n", node->GetLineNum());
                return false;
            }
        }
        for (const tinyxml2::XMLElement * attribute = characteristic->FirstChildElement("Attribute");
             attribute; attribute = attribute->NextSiblingElement("Attribute"))
        {
            const char * reasonCode = attribute->Attribute("reasonCode");
            if (reasonCode == nullptr)
            {
                if (useReasonCodes && masterReasonCode == nullptr)
                {
                    fprintf(stderr, "Attribute with no reason code at %i\n", attribute->GetLineNum());
                    return false;
                }
                reasonCode = masterReasonCode;
            }

            double partialScore;
            if (attribute->QueryDoubleAttribute("partialScore", &partialScore) == tinyxml2::XML_SUCCESS)
            {
                // using "partialScore" attribute is easy, since we can calculate the difference here.
                builder.field(config.outputValueName);
                builder.constant(partialScore);
                builder.function(Function::functionTable.names.plus, 2);
                builder.assign(config.outputValueName);
                double thisDiff = reasonCodeAlgorithmIsAbove ? partialScore - baseline : baseline - partialScore;
                if (useReasonCodes && config.reasonCodeValueName != nullptr && thisDiff > 0)
                {
                    builder.field(config.reasonCodeValueName);
                    builder.constant(thisDiff);
                    builder.constant(reasonCode, PMMLDocument::TYPE_STRING);
                    builder.function(Function::makeTuple, 2);
                    builder.function(Function::insertToTableDef, 2);
                    
                    builder.block(2);
                }
            }
            else if (const tinyxml2::XMLElement * complex = attribute->FirstChildElement("ComplexPartialScore"))
            {
                // using ComplexPartialScore element is less easy, since we need to calculate it in the resulting script.
                PMMLDocument::FieldType fieldType = config.outputType == PMMLDocument::TYPE_INVALID ? PMMLDocument::TYPE_NUMBER : config.outputType;
                const tinyxml2::XMLElement * transform = PMMLDocument::skipExtensions(complex->FirstChildElement());
                if (transform == nullptr)
                {
                    fprintf(stderr, "ComplexPartialScore with no transformation at %i\n", attribute->GetLineNum());
                    return false;
                }
                if (!Transformation::parse(builder, transform))
                {
                    return false;
                }
                if (!builder.coerceToSpecificTypes(1, &fieldType))
                {
                    fprintf(stderr, "ComplexPartialScore with wrong type at %i\n", attribute->GetLineNum());
                    return false;
                }
                
                builder.defaultValue("0");
                
                size_t innerBlockSize = 1;
                if (useReasonCodes && config.reasonCodeValueName != nullptr)
                {
                    // Shove result into tempVariable
                    auto tempVariable = builder.context().createVariable(fieldType, "partial_result");
                    builder.declare(tempVariable, AstBuilder::HAS_INITIAL_VALUE);
                    
                    builder.field(config.reasonCodeValueName);
                    if (reasonCodeAlgorithmIsAbove)
                    {
                        // tempVariable - baseline
                        builder.field(tempVariable);
                        builder.constant(baseline);
                    }
                    else
                    {
                        // baseline - tempVariable
                        builder.constant(baseline);
                        builder.field(tempVariable);
                    }
                    builder.function(Function::functionTable.names.minus, 2);
                    
                    // {insert(config.reasonCodeValueName, {baseline - tempVariable, reasonCode}}
                    builder.constant(reasonCode, PMMLDocument::TYPE_STRING);
                    builder.function(Function::makeTuple, 2);
                    builder.function(Function::insertToTableDef, 2);
                    
                    // If expression above will be positive
                    builder.field(tempVariable);
                    builder.constant(baseline);
                    builder.function(reasonCodeAlgorithmIsAbove ? Function::functionTable.names.greaterThan : Function::functionTable.names.lessThan, 2);
                    builder.ifChain(2);
                    
                    innerBlockSize = 3; // 1 for the variable declaration, 1 for the insert, 1 for the assign
                    
                    builder.field(tempVariable);
                }
                
                // config.outputValueName = config.outputValueName + tempVariable
                builder.field(config.outputValueName);
                builder.function(Function::functionTable.names.plus, 2);
                builder.assign(config.outputValueName);

                builder.block(innerBlockSize);
            }
            else
            {
                fprintf(stderr, "Attribute with no score at %i\n", attribute->GetLineNum());
                return false;
            }

            const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(attribute->FirstChildElement());
            if (predicate == nullptr)
            {
                fprintf(stderr, "Attribute with no predicate at %i\n", attribute->GetLineNum());
                return false;
            }
            if (!Predicate::parse(builder, predicate))
            {
                return false;
            }
            ifChainLength += 2;
        }
        if (ifChainLength > 0)
        {
            builder.ifChain(ifChainLength);
            blockSize++;
        }
    }
    // This last bit sorts the reason codes from most to least important
    if (useReasonCodes && config.reasonCodeValueName != nullptr)
    {
        // Reason codes are stored as {key, value} pairs inside the list.
        builder.field(config.reasonCodeValueName);
        // function(a,b) return a[1] > b[1] end
        PMMLDocument::ScopedVariableDefinitionStackGuard scope(builder.context());
        auto aParam = scope.addDataField("a", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);
        auto bParam = scope.addDataField("b", PMMLDocument::TYPE_NUMBER, PMMLDocument::ORIGIN_PARAMETER, PMMLDocument::OPTYPE_CONTINUOUS);

        builder.field(aParam);
        builder.field(bParam);
        builder.constant(1);
        builder.fieldIndirect(aParam, 1);
        builder.constant(1);
        builder.fieldIndirect(bParam, 1);
        builder.function(Function::functionTable.names.greaterThan, 2);
        builder.lambda(2);
        
        builder.function(Function::sortTableDef, 2);
        blockSize++;
    }
    builder.block(blockSize);

    return true;
}
