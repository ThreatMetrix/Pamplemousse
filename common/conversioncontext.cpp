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
//  Created by Caleb Moore on 6/9/18.
//

#include "conversioncontext.hpp"
#include "analyser.hpp"
#include "document.hpp"
#include "ast.hpp"
#include "model/output.hpp"
#include "functiondispatch.hpp"
#include <cstring>
#include <string>
#include <algorithm>
#include <assert.h>

namespace PMMLDocument
{
    unsigned int FieldDescription::next_id = 0;
    
    void ConversionContext::setupInputs(const DataFieldVector & inputs, const std::unordered_set<std::string> & activeFields, const std::unordered_set<std::string> & outFields)
    {
        for (const auto & input : inputs)
        {
            if (activeFields.count(input.first))
            {
                m_inputs.emplace(input.first, addUnscopedDataField(input.first, input.second, ORIGIN_DATA_DICTIONARY));
            }
            else
            {
                m_variableNames.insert(input.first);
                if (outFields.count(input.first))
                {
                    m_outputs.emplace(input.first, addUnscopedDataField(input.first, input.second, ORIGIN_OUTPUT));
                }
            }
        }
    }
    
    void ConversionContext::setupOutputs(const DataFieldVector & outputs)
    {
        for (const auto & output : outputs)
        {
            m_variableNames.insert(output.first);
            auto addedField = addUnscopedDataField(output.first, output.second, ORIGIN_OUTPUT);
            if (output.second.dataType != PMMLDocument::TYPE_INVALID)
            {
                m_outputs.emplace(output.first, addedField);
            }
        }
    }

    void ConversionContext::declareCustomFunction(std::string && pmmlFunction, ConstFieldDescriptionPtr definition, FieldType type, const Function::Definition * ld, std::vector<PMMLDocument::FieldType> && parameterList)
    {
        // constructor to CustomDefinition requires a c string and a type (more than one argument), so we need to use the piecewise_construct to implace it here.
        auto inserted = m_customFunctionDefinitions.emplace(std::piecewise_construct,
                                                            std::forward_as_tuple(pmmlFunction),
                                                            std::forward_as_tuple(definition, type, ld, std::move(parameterList)));
    }

    const Function::CustomDefinition * ConversionContext::findCustomFunction(const std::string & pmmlFunction) const
    {
        auto iterator = m_customFunctionDefinitions.find(pmmlFunction);
        if (iterator != m_customFunctionDefinitions.end())
        {
            return &iterator->second;
        }
        return nullptr;
    }
    
    ConstFieldDescriptionPtr ConversionContext::createVariable(FieldType type, const std::string & name, FieldOrigin origin)
    {
        std::string variableName = makeSaneAndUniqueVariable(name);
        return std::make_shared<FieldDescription>(type, origin, OPTYPE_INVALID, variableName);
    }

    void ConversionContext::setTransformationDictionary(const std::shared_ptr<const TransformationDictionary> & dictionary)
    {
        m_transformationDictionary = dictionary;
    }

    std::string ConversionContext::makeSaneAndUniqueVariable(const std::string & key)
    {
        std::string sanitised;
        sanitised.reserve(key.length());

        if (key.empty() || isdigit(key.at(0)))
        {
            sanitised.push_back('_');
        }
        std::replace_copy_if(key.begin(), key.end(), std::back_inserter(sanitised), [](char a){return !isalnum(a);}, '_');

        // We're now using this identifier, don't use it again.
        if (m_variableNames.insert(std::string(sanitised)).second)
        {
            // Nobody was using it, we can use it.
            return sanitised;
        }

        // Find a unique identifer that hasn't been used yet by sticking various numbers after it.
        // This has been optimised since it is known to take a _long_ time on Gradient Boosted Trees
        size_t offset = sanitised.length();
        sanitised.push_back('_');

        for (uint8_t depth = 1; ; depth++)
        {
            sanitised.push_back('0');
            sanitised.at(offset + 1) = '1';
        newvalue:
            if (m_variableNames.insert(sanitised).second)
            {
                // Nobody was using it, we can use it.
                return sanitised;
            }

            // increment the bottom column and carry.
            for (uint8_t iterator = depth; iterator > 0; iterator--)
            {
                char & value = sanitised.at(offset + iterator);
                if (value < '9')
                {
                    value++;
                    goto newvalue;
                }
                else
                {
                    value = '0';
                }
            }
        }
    }

    // This loads up the mining schema into the context scope.
    MiningSchemaStackGuard::MiningSchemaStackGuard(ConversionContext & context, const tinyxml2::XMLElement * miningSchema) :
        m_context(context),
        m_targetName(nullptr),
        m_isValid(true)
    {
        MiningSchema & activeSchema = context.m_miningSchema;
        
        // Build the new schema in its own m_savedMiningSchema
        for (const tinyxml2::XMLElement * iter = miningSchema ? miningSchema->FirstChildElement("MiningField") : nullptr;
             iter; iter = iter->NextSiblingElement("MiningField"))
        {
            const char * fieldName = iter->Attribute("name");
            if (fieldName == nullptr)
            {
                fprintf(stderr, "Missing name attribute MiningField at %i\n", iter->GetLineNum());
                continue;
            }
            
            auto description = context.getFieldDescription(fieldName);
            MiningFieldUsage usage = getMiningFieldUsage(iter);
            if (usage == USAGE_OUT)
            {
                m_targetName = description;
            }
            else if (usage == USAGE_IN)
            {
                if (description == nullptr)
                {
                    fprintf(stderr, "Cannot find mining field %s at %i\n", fieldName, iter->GetLineNum());
                    m_isValid = false;
                    continue;
                }
                
                MiningField & newField = m_savedMiningSchema.emplace(fieldName, description).first->second;
                // If this is some sort of ensemble, the parent mining schema may have set maximums, minimums or replacement values.
                auto parentSchemaField = activeSchema.find(fieldName);
                if (parentSchemaField != activeSchema.end())
                {
                    newField = parentSchemaField->second;
                }
                const char * outlierTreatmentString = iter->Attribute("outliers");
                if (iter->QueryDoubleAttribute("lowValue", &newField.minValue) == tinyxml2::XML_SUCCESS &&
                    iter->QueryDoubleAttribute("highValue", &newField.maxValue) == tinyxml2::XML_SUCCESS &&
                    outlierTreatmentString != nullptr)
                {
                    newField.outlierTreatment = outlierTreatmentFromString(outlierTreatmentString);
                }

                if (const char * missingValueReplacement = iter->Attribute("missingValueReplacement"))
                {
                    newField.hasReplacementValue = true;
                    newField.replacementValue.assign(missingValueReplacement);
                }
            }
        }
        // Now, swap it into the context. It will swap them back when this guard goes out of scope.
        activeSchema.swap(m_savedMiningSchema);
    }
}
