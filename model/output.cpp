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
//  Created by Caleb Moore on 14/9/18.
//

#include <assert.h>
#include "output.hpp"
#include "ast.hpp"
#include "conversioncontext.hpp"
#include "document.hpp"
#include "transformation.hpp"
#include <algorithm>

namespace
{
void collectOutputs(const tinyxml2::XMLElement * element, PMMLDocument::DataFieldVector & names)
{
    if (const tinyxml2::XMLElement * outputs = element->FirstChildElement("Output"))
    {
        for (const tinyxml2::XMLElement * iterator = outputs->FirstChildElement("OutputField");
             iterator; iterator = iterator->NextSiblingElement("OutputField"))
        {
            // Note, this first pass doesn't care about errors... they will be silently ignored
            if (const char * name = iterator->Attribute("name"))
            {
                PMMLDocument::FieldType fieldType = PMMLDocument::TYPE_INVALID;
                if (const char * type = iterator->Attribute("dataType"))
                {
                    fieldType = PMMLDocument::dataTypeFromString(type);
                }
                PMMLDocument::OpType opType = PMMLDocument::OPTYPE_INVALID;
                if (const char * type = iterator->Attribute("optype"))
                {
                    opType = PMMLDocument::optypeFromString(type);
                }
                PMMLDocument::DataField dataField = {fieldType, opType};
                names.emplace_back(name, dataField);
            }
        }
    }

    for (const tinyxml2::XMLElement * iterator = PMMLDocument::skipExtensions(element->FirstChildElement());
         iterator; iterator = PMMLDocument::skipExtensions(iterator->NextSiblingElement()))
    {
        collectOutputs(iterator, names);
    }
}
}

PMMLDocument::DataFieldVector Output::findAllOutputs(const tinyxml2::XMLElement * element)
{
    PMMLDocument::DataFieldVector names;
    collectOutputs(element, names);

    std::stable_sort(names.begin(), names.end(),
                     [&](const PMMLDocument::DataFieldVector::value_type & a, const PMMLDocument::DataFieldVector::value_type & b) {
        return a.first < b.first;
    });
    const auto last = std::unique(names.begin(), names.end(),
                                  [&](const PMMLDocument::DataFieldVector::value_type & a, const PMMLDocument::DataFieldVector::value_type & b) {
        return a.first == b.first;
    });
    names.erase(last, names.end());
    return names;
}


const char * Output::findOutputForFeature(const tinyxml2::XMLElement * element, const char * featureName, bool requireNoValue)
{
    if (const tinyxml2::XMLElement * outputs = element->FirstChildElement("Output"))
    {
        for (const tinyxml2::XMLElement * iterator = outputs->FirstChildElement("OutputField");
             iterator; iterator = iterator->NextSiblingElement("OutputField"))
        {
            if (requireNoValue && iterator->Attribute("value"))
            {
                continue;
            }
            const char * name = iterator->Attribute("name");
            const char * feature = iterator->Attribute("feature");
            if (name != nullptr && feature != nullptr &&
                strcmp(featureName, feature) == 0)
            {
                return name;
            }
        }
    }
    return nullptr;
}

void Output::doTargetPostprocessing(AstBuilder & builder, const tinyxml2::XMLElement * targets,
                                    const PMMLDocument::ModelConfig & modelConfig, size_t & blockSize)
{
    for (const tinyxml2::XMLElement * target = targets->FirstChildElement("Target");
         target; target = target->NextSiblingElement("Target"))
    {
        bool continuous;
        if (auto targetField = modelConfig.targetField)
        {
            continuous = targetField->field.opType == PMMLDocument::OPTYPE_CONTINUOUS;
        }
        else
        {
            continuous = modelConfig.outputType == PMMLDocument::TYPE_NUMBER;
        }
        if (continuous)
        {
            bool doneAnythingUseful = false;
            builder.field(modelConfig.outputValueName);
            if (const tinyxml2::XMLElement * targetValue = targets->FirstChildElement("TargetValue"))
            {
                if (const char * defaultValue = targetValue->Attribute("defaultValue"))
                {
                    builder.defaultValue(defaultValue);
                    doneAnythingUseful = true;
                }
            }
            
            double val;
            if (target->QueryAttribute("max", &val) == tinyxml2::XML_SUCCESS)
            {
                builder.constant(val);
                builder.function(Function::functionTable.names.min, 2);
                doneAnythingUseful = true;
            }
            if (target->QueryAttribute("min", &val) == tinyxml2::XML_SUCCESS)
            {
                builder.constant(val);
                builder.function(Function::functionTable.names.max, 2);
                doneAnythingUseful = true;
            }
            if (target->QueryAttribute("rescaleFactor", &val) == tinyxml2::XML_SUCCESS)
            {
                builder.constant(val);
                builder.function(Function::functionTable.names.times, 2);
                doneAnythingUseful = true;
            }
            if (target->QueryAttribute("rescaleConstant", &val) == tinyxml2::XML_SUCCESS)
            {
                builder.constant(val);
                builder.function(Function::functionTable.names.plus, 2);
                doneAnythingUseful = true;
            }
            
            if (const char * castInteger = target->Attribute("castInteger"))
            {
                if (strcmp(castInteger, "round") == 0)
                {
                    builder.function(Function::functionTable.names.round, 1);
                }
                else if (strcmp(castInteger, "ceiling") == 0)
                {
                    builder.function(Function::functionTable.names.ceil, 1);
                }
                else if (strcmp(castInteger, "floor") == 0)
                {
                    builder.function(Function::functionTable.names.floor, 1);
                }
                doneAnythingUseful = true;
            }
            
            if (doneAnythingUseful)
            {
                builder.declare(modelConfig.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
            }
            else
            {
                // Get rid of the field ref.
                builder.popNode();
            }
        }
    }
}

void Output::mapDisplayValue(AstBuilder & builder, const tinyxml2::XMLElement * element,
                             const PMMLDocument::ModelConfig & modelConfig)
{
    size_t displayValues = 0;
    if (const tinyxml2::XMLElement * targets = element->FirstChildElement("Targets"))
    {
        if (const tinyxml2::XMLElement * target = targets->FirstChildElement("Target"))
        {
            for (const tinyxml2::XMLElement * targetValue = target->FirstChildElement("TargetValue");
                 targetValue; targetValue = targetValue->NextSiblingElement("TargetValue"))
            {
                const char * value = targetValue->Attribute("value");
                const char * displayValue = targetValue->Attribute("displayValue");
                if (value != nullptr && displayValue != nullptr)
                {
                    builder.field(modelConfig.outputValueName);
                    builder.constant(value, modelConfig.outputType);
                    builder.function(Function::functionTable.names.equal, 2);
                    builder.constant(displayValue, modelConfig.outputType);
                    builder.function(Function::boundFunction, 2);
                    displayValues++;
                }
            }
        }
    }
    if (displayValues > 0)
    {
        builder.field(modelConfig.outputValueName);
        builder.function(Function::surrogateFunction, displayValues + 1);
    }
    else
    {
        builder.field(modelConfig.outputValueName);
    }
}


bool Output::addOutputValues(AstBuilder & builder, const tinyxml2::XMLElement * element,
                             const PMMLDocument::ModelConfig & modelConfig, size_t & blockSize)
{
    if (const tinyxml2::XMLElement * targets = element->FirstChildElement("Targets"))
    {
        doTargetPostprocessing(builder, targets, modelConfig, blockSize);
    }
    
    if (const tinyxml2::XMLElement * outputs = element->FirstChildElement("Output"))
    {
        for (const tinyxml2::XMLElement * iterator = outputs->FirstChildElement("OutputField");
             iterator; iterator = iterator->NextSiblingElement("OutputField"))
        {
            const char * name = iterator->Attribute("name");
            if (name == nullptr)
            {
                fprintf(stderr, "OutputField doesn't have a name at %i", iterator->GetLineNum());
                return false;
            }
            
            auto description = builder.context().getFieldDescription(name);
            // Outputs should have already had descriptions added in findAllOutputs.
            assert(description);
            
            const char * feature = iterator->Attribute("feature");
            bool gotValue = false;
            if (feature == nullptr)
            {
                fprintf(stderr, "OutputField doesn't have a feature at %i", iterator->GetLineNum());
                return false;
            }
        
            if (strcmp("transformedValue", feature) == 0)
            {
                const tinyxml2::XMLElement * child = PMMLDocument::skipExtensions(iterator->FirstChildElement());
                if (child == nullptr)
                {
                    fprintf(stderr, "transformed value expects a child node at %i", iterator->GetLineNum());
                    return false;
                }
                if (!Transformation::parse(builder, child))
                {
                    return false;
                }
                gotValue = true;
            }
            else if (strcmp("predictedValue", feature) == 0)
            {
                if (modelConfig.outputValueName != nullptr && modelConfig.outputValueName != description)
                {
                    builder.field(modelConfig.outputValueName);
                    gotValue = true;
                }
            }
            else if (strcmp("predictedDisplayValue", feature) == 0)
            {
                if (modelConfig.outputValueName != nullptr)
                {
                    mapDisplayValue(builder, element, modelConfig);
                    gotValue = true;
                }
            }
            else if (strcmp("entityId", feature) == 0)
            {
                if (modelConfig.idValueName != nullptr && modelConfig.idValueName != description)
                {
                    builder.field(modelConfig.idValueName);
                    gotValue = true;
                }
            }
            else if (strcmp("probability", feature) == 0)
            {
                if (!modelConfig.probabilityValueName.empty())
                {
                    if (const char * value = iterator->Attribute("value"))
                    {
                        auto found = modelConfig.probabilityValueName.find(value);
                        if (found != modelConfig.probabilityValueName.end())
                        {
                            builder.field(found->second);
                            builder.defaultValue("0");
                            gotValue = true;
                        }
                    }
                    else
                    {
                        if (modelConfig.bestProbabilityValueName != description)
                        {
                            if (modelConfig.bestProbabilityValueName != nullptr)
                            {
                                builder.field(modelConfig.bestProbabilityValueName);
                            }
                            else
                            {
                                for (const auto pair : modelConfig.probabilityValueName)
                                {
                                    builder.field(pair.second);
                                    builder.defaultValue("0");
                                }
                                builder.function(Function::functionTable.names.max, modelConfig.probabilityValueName.size());
                            }
                            gotValue = true;
                        }
                    }
                }
            }
            else if (strcmp("confidence", feature) == 0)
            {
                if (!modelConfig.probabilityValueName.empty())
                {
                    if (const char * value = iterator->Attribute("value"))
                    {
                        auto found = modelConfig.confidenceValues.find(value);
                        if (found != modelConfig.confidenceValues.end())
                        {
                            builder.field(found->second);
                            builder.defaultValue("0");
                            gotValue = true;
                        }
                        else
                        {
                            // Try to just use a probability
                            auto found = modelConfig.probabilityValueName.find(value);
                            if (found != modelConfig.probabilityValueName.end())
                            {
                                builder.field(found->second);
                                builder.defaultValue("0");
                                gotValue = true;
                            }
                        }
                    }
                    else
                    {
                        for (const auto pair : modelConfig.confidenceValues)
                        {
                            builder.field(pair.second);
                            builder.defaultValue("0");
                        }
                        builder.function(Function::functionTable.names.max, modelConfig.confidenceValues.size());
                        gotValue = true;
                    }
                }
            }

            else if (strcmp("reasonCode", feature) == 0)
            {
                if (modelConfig.reasonCodeValueName)
                {
                    int rank = iterator->IntAttribute("rank", 1);
                    builder.constant(rank);
                    // Reason codes are stored as {key, value} pairs inside the list.
                    builder.constant(2);
                    builder.fieldIndirect(modelConfig.reasonCodeValueName, 2);
                    gotValue = true;
                }
            }
            if (gotValue)
            {
                PMMLDocument::FieldType type = description->field.dataType;
                if (description->field.dataType == PMMLDocument::TYPE_INVALID)
                {
                    const_cast<PMMLDocument::DataField &>(description->field).dataType = builder.topNode().coercedType;
                }
                else
                {
                    builder.coerceToSpecificTypes(1, &type);
                }
                builder.declare(description, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
            }
            
            // outputs are accessable to subsequent outputs.
            builder.context().addDefaultMiningField(name, description);
        }
    }
    return true;
}
