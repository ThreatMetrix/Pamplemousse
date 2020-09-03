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

#ifndef document_hpp
#define document_hpp

#include "tinyxml2.h"
#include <ostream>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "ast.hpp"

class LuaOutputter;
class AstBuilder;

namespace PMMLDocument
{
    class ConversionContext;
    struct FieldDescription;

    const char * findPredictedValueOutput(const tinyxml2::XMLElement * element);

    typedef std::map<std::string, ConstFieldDescriptionPtr> ProbabilitiesOutputMap;
    
    // This structure is for specifying where you want which output
    struct ModelConfig
    {
        // This represents the functionName (regression or classification) that is exprected
        MiningFunction function;
        // These four values represent the variable names where the model should write its immediate output. They are all optional; if they are empty, that means this model is not required to output this value.
        // The variable where the predicted value should be written to.
        ConstFieldDescriptionPtr outputValueName;
        // The varaible where a map of probabilities of predicted values shoudl be written to (in classification model)
        ProbabilitiesOutputMap probabilityValueName;
        // The confidences for classification models
        ProbabilitiesOutputMap confidenceValues;
        // The variable where the entity ID should be written to
        ConstFieldDescriptionPtr idValueName;
        // This variable is where the reason codes should be written to.
        ConstFieldDescriptionPtr reasonCodeValueName;
        // This variable is where the best probability should be written to.
        ConstFieldDescriptionPtr bestProbabilityValueName;
        // This represents the type of output this model is exprected to deliver, or TYPE_INVALID if it doesn't matter.
        PMMLDocument::FieldType outputType;
        ConstFieldDescriptionPtr targetField;
        ModelConfig() :
        function(FUNCTION_ANY), outputType(TYPE_INVALID), targetField(nullptr)
        {}
    };

    // This function ignores the "Extention" nodes when traversing child nodes. Extentions are never supported here.
    inline const tinyxml2::XMLElement * skipExtensions(const tinyxml2::XMLElement * node)
    {
        while (node != nullptr && strcmp("Extension", node->Name()) == 0)
        {
            node = node->NextSiblingElement();
        }
        return node;
    }
    
    ProbabilitiesOutputMap
    buildProbabilityOutputMap(PMMLDocument::ConversionContext & context, const char * name, PMMLDocument::FieldType type,
                              const std::vector<std::string> & values);
    
    ConstFieldDescriptionPtr
    getOrAddCategoryInOutputMap(PMMLDocument::ConversionContext & context, PMMLDocument::ProbabilitiesOutputMap & probsOutput, const char * name,
                                PMMLDocument::FieldType type, const std::string & value);
    
    size_t pickWinner(AstBuilder & builder, ModelConfig & config, const ProbabilitiesOutputMap & probabilitiesOutputMap);
    // This function is used by classification models that generate a map of probabilities of different values and picks the highest.
    // It can create AST code normalize them (makes sure they add to zero), choose the higest, or both.
    size_t normaliseProbabilitiesAndPickWinner(AstBuilder & builder, ModelConfig & config);
    
    // This is like the one above, but assuming we already know what the probabilities add up to
    size_t normalizeProbabilityArrayAccordingToFactor(AstBuilder & builder, ProbabilitiesOutputMap & probabilityValueName, const char * varName, AstNode factor);
    
    // This is a function that takes the root node of a PMML model and pushes equivilent code to the top of builder.
    bool parseModel(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & modelConfig);
    
    
    // This is a high level function that takes a root node of a PMML document and emits Lua source code.
    bool convertPMML(AstBuilder & builder, const tinyxml2::XMLElement * documentRoot);
}

#endif /* document_hpp */
