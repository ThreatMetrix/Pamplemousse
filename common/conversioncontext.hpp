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
//  This file contains the ConversionContext, which holds various data dictionary, field and variable declarations

#ifndef conversioncontext_hpp
#define conversioncontext_hpp

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <memory>
#include "pmmldocumentdefs.hpp"
#include "function.hpp"

namespace PMMLDocument
{
    typedef std::unordered_map<std::string, MiningField> MiningSchema;
    typedef std::unordered_map<std::string, AstNode> TransformationDictionary;

    class ConversionContext
    {
    public:
        void setupInputs(const DataFieldVector & inputs, const std::unordered_set<std::string> & activeFields, const std::unordered_set<std::string> & outFields);
        void setupOutputs(const DataFieldVector & inputs);

        // Declare a new field without scope.
        ConstFieldDescriptionPtr addUnscopedDataField(const std::string & key, const DataField & field, FieldOrigin origin)
        {
            std::shared_ptr<FieldDescription> out = std::make_shared<FieldDescription>(field, origin, makeSaneAndUniqueVariable(key));
            m_dataDictionary.emplace(key, out);
            return out;
        }

        std::string makeSaneAndUniqueVariable(const std::string & key);

        const ConstFieldDescriptionPtr getFieldDescription(const char * field) const
        {
            auto found = m_dataDictionary.find(field);
            if (found != m_dataDictionary.end())
            {
                return found->second;
            }
            return nullptr;
        }
        
        // This returns a mining field of the current model matching a field reference.
        const MiningField * getMiningField(const std::string & field) const
        {
            auto found = m_miningSchema.find(field);
            if (found != m_miningSchema.end())
            {
                return &found->second;
            }
            return nullptr;
        }
        void addDefaultMiningField(const std::string & field, const ConstFieldDescriptionPtr & variable)
        {
            m_miningSchema.emplace(field, MiningField(variable));
        }
        void declareCustomFunction(std::string && functionName, ConstFieldDescriptionPtr definition, FieldType type, Function::MissingValueRule nullityType, std::vector<PMMLDocument::FieldType> && parameterList);
        const Function::CustomDefinition * findCustomFunction(const std::string & functionName) const;

        ConstFieldDescriptionPtr createVariable(FieldType type, const std::string & name, FieldOrigin origin = PMMLDocument::ORIGIN_TEMPORARY);
        
        // This is a short cut to change the behaviour of transforms inside models to go through the mining schema, while allowing
        // things like user defined functions and derived fields to skip them.
        bool isLoadingTransformationDictionary() const
        {
            return m_loadingTransformationDictionary;
        }

        void setLoadingTransformationDictionary(bool loading)
        {
            m_loadingTransformationDictionary = loading;
        }

        // This sets whatever is in the current mining schema to be the global schema
        void setTransformationDictionary(const std::shared_ptr<const TransformationDictionary> & dictionary);

        const TransformationDictionary * transformationDictionary() const
        {
            return m_transformationDictionary.get();
        }
        
        // This marks a neuron as found and returns true if we were looking for it
        bool markNeuron(const std::string & id, ConstFieldDescriptionPtr & field)
        {
            return m_neurons.emplace(id, field).second;
        }
        
        const ConstFieldDescriptionPtr findNeuron(const std::string & name) const
        {
            auto found = m_neurons.find(name);
            if (found != m_neurons.end())
            {
                return found->second;
            }
            return nullptr;
        }
        
        const DataDictionary & getInputs() { return m_inputs; }
        const DataDictionary & getOutputs() { return m_outputs; }
        
        const std::string & getApplication() const { return m_application; }
        void setApplication(const std::string & application) { m_application = application; }
        
        bool hasVariableNamed(const std::string & name) const
        {
            return m_variableNames.find(name) != m_variableNames.end();
        }
    private:
        
        DataDictionary m_inputs;
        DataDictionary m_outputs;
        DataDictionary m_neurons;
        
        // This is a multimap so that we can more easily insert
        typedef std::multimap<std::string, std::shared_ptr<FieldDescription>> Fields;
        Fields m_dataDictionary;

        MiningSchema m_miningSchema;
        // DerivedFields from TransformationDictionary are accessable throughout the model.
        std::shared_ptr<const TransformationDictionary> m_transformationDictionary;
        bool m_loadingTransformationDictionary = false;
        std::unordered_set<std::string> m_variableNames;
        
        std::string m_application;

        friend class ScopedVariableDefinitionStackGuard;
        friend class MiningSchemaStackGuard;
        std::unordered_map<std::string, Function::CustomDefinition> m_customFunctionDefinitions;
    };

    class ScopedVariableDefinitionStackGuard
    {
        std::vector<ConversionContext::Fields::iterator> m_fieldIterators;
        ConversionContext & m_context;
    public:
        ScopedVariableDefinitionStackGuard(ConversionContext & context) :
            m_context(context)
        {}
        ~ScopedVariableDefinitionStackGuard()
        {
            for (ConversionContext::Fields::iterator element : m_fieldIterators)
            {
                m_context.m_dataDictionary.erase(element);
            }
        }
        ConstFieldDescriptionPtr addDataField(const std::string & variable, FieldType type, FieldOrigin origin, OpType optype)
        {
            std::string luaRepr = m_context.makeSaneAndUniqueVariable(variable);
            std::shared_ptr<FieldDescription> field = std::make_shared<FieldDescription>(type, origin, optype, luaRepr);
            ConversionContext::Fields::iterator iter = m_context.m_dataDictionary.emplace(variable, field);
            m_fieldIterators.push_back(iter);
            return field;
        }
    };

    // This represents the mining schema of a model, which is the set of values that the model might use and various properties of those values.
    // The position of this guard on the call stack represents the scope of the model.
    // As models may include other models, but mining schemas are not heirachical, this is merely required to swap in and out schemas as a whole.
    class MiningSchemaStackGuard
    {
        ConversionContext & m_context;
        // This is the parent model's mining schema
        MiningSchema m_savedMiningSchema;
        // The target value is a special input during learning that here actually represents the output of a prediction
        // it sometimes affects the behaviour of the prediction.
        ConstFieldDescriptionPtr m_targetName;
        bool m_isValid;
    public:
        MiningSchemaStackGuard(ConversionContext & context, const tinyxml2::XMLElement * miningSchema);
        ~MiningSchemaStackGuard()
        {
            m_context.m_miningSchema.swap(m_savedMiningSchema);
        }
        ConstFieldDescriptionPtr getTargetName() const
        {
            return m_targetName;
        }
        bool isValid() const
        {
            return m_isValid;
        }
    };
}

#endif /* conversioncontext_hpp */
