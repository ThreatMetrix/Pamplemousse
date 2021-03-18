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
//  Created by Caleb Moore on 13/11/18.
//

#include "neuralnetworkmodel.hpp"
#include "ast.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "transformation.hpp"
#include <algorithm>
#include <cmath>
#include <stdlib.h>

namespace NeuralNetworkModel
{
namespace
{
    // These two lists must be kept in sync and ascii ordered (the first two elements are capitalised in the spec).
    enum ActivationFunction
    {
        ACTIVATION_ELLIOTT,
        ACTIVATION_GAUSS,
        ACTIVATION_ARCTAN,
        ACTIVATION_COSINE,
        ACTIVATION_EXPONENTIAL,
        ACTIVATION_IDENTITY,
        ACTIVATION_LOGISTIC,
        ACTIVATION_RADIAL_BASIS,
        ACTIVATION_RECIPROCAL,
        ACTIVATION_RECTIFIER,
        ACTIVATION_SINE,
        ACTIVATION_SQUARE,
        ACTIVATION_TANH,
        ACTIVATION_THRESHOLD,
        ACTIVATION_INVALID
    };
    
    const char * const ACTIVATION_FUNCTION_NAME[]
    {
        "Elliott",
        "Gauss",
        "arctan",
        "cosine",
        "exponential",
        "identity",
        "logistic",
        "radialBasis",
        "reciprocal",
        "rectifier",
        "sine",
        "square",
        "tanh",
        "threshold"
    };
    
    enum NormalizationMethod
    {
        NORMALIZATION_METHOD_NONE,
        NORMALIZATION_METHOD_SIMPLEMAX,
        NORMALIZATION_METHOD_SOFTMAX,
        NORMALIZATION_METHOD_INVALID
    };
    
    const char * const NORMALIZATION_METHOD_NAME[] =
    {
        "none",
        "simplemax",
        "softmax",
        nullptr
    };
    
    ActivationFunction getActivationFunctionFromString(const char * name)
    {
        auto found = std::equal_range(ACTIVATION_FUNCTION_NAME, ACTIVATION_FUNCTION_NAME + static_cast<int>(ACTIVATION_INVALID), name, PMMLDocument::stringIsBefore);
        if (found.first != found.second)
        {
            return static_cast<ActivationFunction>(found.first - ACTIVATION_FUNCTION_NAME);
        }
        else
        {
            return ACTIVATION_INVALID;
        }
    }

    NormalizationMethod getNormalizationMethodFromString(const char * name)
    {
        const char * const * method;
        for (method = NORMALIZATION_METHOD_NAME; *method != nullptr; ++method)
        {
            if (strcmp(*method, name) == 0)
                break;
        }

        return static_cast<NormalizationMethod>(method - NORMALIZATION_METHOD_NAME);
    }
    
    // This function is not defined by default. It will need to be defined in the prologue.
    const Function::Definition elliottFunction = {"elliott", Function::RUN_LAMBDA, PMMLDocument::TYPE_INVALID, LuaOutputter::PRECEDENCE_TOP, Function::MISSING_IF_ANY_ARGUMENT_IS_MISSING};
    
    void applyActivationFunction(AstBuilder & builder, ActivationFunction activationFunction, double threshold, double altitude, size_t fanIn, double width)
    {
        switch(activationFunction)
        {
            case ACTIVATION_ELLIOTT:
                // activation(Z) = Z/(1+|Z|)
                builder.function(elliottFunction, 1);
                break;
            case ACTIVATION_GAUSS:
                // activation(Z) = exp(-(Z*Z))
                builder.constant(2);
                builder.function(Function::functionTable.names.pow, 2);
                builder.function(Function::unaryMinus, 1);
                builder.function(Function::functionTable.names.exp, 1);
                break;
            case ACTIVATION_ARCTAN:
                // activation(Z) = 2 * arctan(Z)/Pi
                builder.function(Function::functionTable.names.atan, 1);
                // This will need to be fixed if we ever support output to anything but Lua
                builder.constant("math.pi", PMMLDocument::TYPE_NUMBER);
                builder.function(Function::functionTable.names.divide, 2);
                builder.constant(2);
                builder.function(Function::functionTable.names.times, 2);
                break;
            case ACTIVATION_COSINE:
                // activation(Z) = cos(Z)
                builder.function(Function::functionTable.names.cos, 1);
                break;
            case ACTIVATION_EXPONENTIAL:
                // activation(Z) = exp(Z)
                builder.function(Function::functionTable.names.exp, 1);
                break;
            case ACTIVATION_IDENTITY:
                // activation(Z) = Z
                break;
            case ACTIVATION_LOGISTIC:
                // activation(Z) = 1 / (1 + exp(-Z))
                builder.function(Function::unaryMinus, 1);
                builder.function(Function::functionTable.names.exp, 1);
                builder.constant(1);
                builder.function(Function::functionTable.names.plus, 2);
                builder.constant(1);
                builder.swapNodes(-1, -2);
                builder.function(Function::functionTable.names.divide, 2);
                break;
            case ACTIVATION_RADIAL_BASIS:
                // Width is checked to be nonzero before this is called
                builder.constant(1.0 / (2.0 * width * width));
                builder.function(Function::functionTable.names.times, 2);
                // activation = exp( f * log(altitude) - Z )
                builder.constant(double(fanIn) * std::log(altitude));
                builder.swapNodes(-1, -2);
                builder.function(Function::functionTable.names.exp, 1);
                break;
            case ACTIVATION_RECIPROCAL:
                // activation(Z) = 1/Z
                builder.constant(1);
                builder.swapNodes(-1, -2);
                builder.function(Function::functionTable.names.divide, 2);
                break;
            case ACTIVATION_RECTIFIER:
                // activation(Z) = max(0,Z)
                builder.constant(0);
                builder.function(Function::functionTable.names.max, 2);
                break;
            case ACTIVATION_SINE:
                // activation(Z) = sin(Z)
                builder.function(Function::functionTable.names.sin, 1);
                break;
            case ACTIVATION_SQUARE:
                // activation(Z) = Z*Z
                builder.constant(2);
                builder.function(Function::functionTable.names.pow, 2);
                break;
            case ACTIVATION_TANH:
                // activation(Z) = (1-exp(-2Z)/(1+exp(-2Z))
                builder.function(Function::functionTable.names.tanh, 1);
                break;
            case ACTIVATION_THRESHOLD:
                builder.constant(threshold);
                builder.function(Function::functionTable.names.greaterThan, 2);
                builder.constant(1);
                builder.constant(0);
                builder.function(Function::functionTable.names.ternary, 3);
                break;
            case ACTIVATION_INVALID:
                // impossible
                break;
        }
    }
    
    struct NeuralNetParseState
    {
        NeuralNetParseState(PMMLDocument::ConversionContext &,
                            ActivationFunction activationFunction, NormalizationMethod normalizationMethod,
                            double threshold, double altitude, double width) :
            defaultActivationFunction(activationFunction),
            defaultNormalizationMethod(normalizationMethod),
            defaultThreshold(threshold),
            defaultAltitude(altitude),
            defaultWidth(width),
            blockSize(0)
        {}
        const ActivationFunction defaultActivationFunction;
        const NormalizationMethod defaultNormalizationMethod;
        const double defaultThreshold;
        const double defaultAltitude;
        const double defaultWidth;
        
        std::unordered_map<std::string, PMMLDocument::ConstFieldDescriptionPtr> nodeIDToVariableMap;
        size_t blockSize;
    
        bool parseNeuralInputs(AstBuilder & builder, const tinyxml2::XMLElement * neuralInputs)
        {
            for (const tinyxml2::XMLElement * neuralInput = neuralInputs->FirstChildElement("NeuralInput"); neuralInput; neuralInput = neuralInput->NextSiblingElement("NeuralInput"))
            {
                const tinyxml2::XMLElement * derivedField = neuralInput->FirstChildElement("DerivedField");
                if (derivedField == nullptr)
                {
                    builder.parsingError("No DerivedField specified at %i\n", neuralInput->GetLineNum());
                    return false;
                }
                
                const tinyxml2::XMLElement * expression = PMMLDocument::skipExtensions(derivedField->FirstChildElement());
                if (expression == nullptr)
                {
                    builder.parsingError("No expression in DerivedField at %i\n", neuralInput->GetLineNum());
                    return false;
                }

                PMMLDocument::ConstFieldDescriptionPtr thisVariable;
                if (!Transformation::parse(builder, expression))
                {
                    return false;
                }
                
                if (builder.topNode().function().functionType == Function::FIELD_REF)
                {
                    // If this is just a plain field ref, don't bother creating a new variable.
                    thisVariable = builder.popNode().fieldDescription;
                }
                else
                {
                    // Default to zero... propagating missing values throughout this doesn't make much sense.
                    builder.defaultValue("0");
                    
                    thisVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "neuron");
                    builder.declare(thisVariable, AstBuilder::HAS_INITIAL_VALUE);
                    blockSize++;
                }
                
                const char * idString = neuralInput->Attribute("id");
                if (nodeIDToVariableMap.emplace(idString, thisVariable).second == false)
                {
                    builder.parsingError("Duplicate node ID: at %i\n", idString, neuralInput->GetLineNum());
                    return false;
                }
            }
            return true;
        }
        
        bool parseNeuralLayer(AstBuilder & builder, const tinyxml2::XMLElement * neuralLayer)
        {
            ActivationFunction activationFunction = defaultActivationFunction;
            if (const char * activationFunctionName = neuralLayer->Attribute("activationFunction"))
            {
                activationFunction = getActivationFunctionFromString(activationFunctionName);
                if (activationFunction == ACTIVATION_INVALID)
                {
                    builder.parsingError("Unknown activationFunction", activationFunctionName, neuralLayer->GetLineNum());
                    return false;
                }
            }
            
            NormalizationMethod normalizationMethod = defaultNormalizationMethod;
            if (const char * normalizationMethodName = neuralLayer->Attribute("normalizationMethod"))
            {
                normalizationMethod = getNormalizationMethodFromString(normalizationMethodName);
                if (normalizationMethod == NORMALIZATION_METHOD_INVALID)
                {
                    builder.parsingError("Unknown normalizationMethodName", normalizationMethodName, neuralLayer->GetLineNum());
                    return false;
                }
            }
            
            double threshold = defaultThreshold;
            if (neuralLayer->QueryDoubleAttribute("threshold", &threshold) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
            {
                builder.parsingError("invalid threshold value", neuralLayer->GetLineNum());
            }
            
            double layerAltitude = defaultAltitude;
            if (neuralLayer->QueryDoubleAttribute("altitude", &layerAltitude) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
            {
                builder.parsingError("invalid altitude value", neuralLayer->GetLineNum());
            }
            
            double layerWidth = defaultWidth;
            if (neuralLayer->QueryDoubleAttribute("width", &layerWidth) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
            {
                builder.parsingError("invalid width value", neuralLayer->GetLineNum());
            }
            
            std::unordered_set<PMMLDocument::ConstFieldDescriptionPtr> thisLayerVars;
            for (const tinyxml2::XMLElement * neuron = neuralLayer->FirstChildElement("Neuron"); neuron; neuron = neuron->NextSiblingElement("Neuron"))
            {
                size_t terms = 0;
                double bias = 0;
                tinyxml2::XMLError retval = neuron->QueryDoubleAttribute("bias", &bias);
                if (retval == tinyxml2::XML_SUCCESS)
                {
                    builder.constant(bias);
                    terms++;
                }
                else if (retval == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
                {
                    builder.parsingError("Invalid bias at %i\n", neuron->GetLineNum());
                    return false;
                }
                
                double altitude = layerAltitude;
                if (neuron->QueryDoubleAttribute("altitude", &altitude) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
                {
                    builder.parsingError("invalid altitude value", neuron->GetLineNum());
                    return false;
                }
                
                double width = layerWidth;
                if (neuron->QueryDoubleAttribute("width", &width) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
                {
                    builder.parsingError("invalid width value", neuron->GetLineNum());
                    return false;
                }
                
                if (activationFunction == ACTIVATION_RADIAL_BASIS && width == 0)
                {
                    builder.parsingError("Width cannot be zero when using radialBasis activation, neuron", neuron->GetLineNum());
                    return false;
                }

                for (const tinyxml2::XMLElement * connection = neuron->FirstChildElement("Con");
                     connection; connection = connection->NextSiblingElement("Con"))
                {
                    // Avoid converting to and from a double to preserve precision
                    const char * weight = connection->Attribute("weight");
                    if (weight == nullptr)
                    {
                        builder.parsingError("Connection missing weight at %i\n", connection->GetLineNum());
                        return false;
                    }
                    
                    char * endp;
                    double weightAsDouble = strtod(weight, &endp);
                    if (*endp != '\0')
                    {
                        builder.parsingError("Invalid weight at %i\n", weight, connection->GetLineNum());
                        return false;
                    }
                    
                    const char * from = connection->Attribute("from");
                    if (from == nullptr)
                    {
                        builder.parsingError("Connection missing from at %i\n", connection->GetLineNum());
                        return false;
                    }
                    
                    auto found = nodeIDToVariableMap.find(from);
                    if (found == nodeIDToVariableMap.end() || thisLayerVars.count(found->second) > 0)
                    {
                        builder.parsingError("Connection to node which was not defined in previous layer at %i\n", from, connection->GetLineNum());
                        return false;
                    }
                    
                    if (activationFunction != ACTIVATION_RADIAL_BASIS)
                    {
                        // value[from] * weight
                        if (weightAsDouble != 0)
                        {
                            builder.field(found->second);
                            builder.defaultValue("0");
                            if (weightAsDouble != 1)
                            {
                                builder.constant(weight, PMMLDocument::TYPE_NUMBER);
                                builder.function(Function::functionTable.names.times, 2);
                            }
                            terms++;
                        }
                    }
                    else
                    {
                        // (value[from] - weight)^2
                        builder.field(found->second);
                        builder.defaultValue("0");
                        builder.constant(weight, PMMLDocument::TYPE_NUMBER);
                        builder.function(Function::functionTable.names.minus, 2);
                        builder.constant(2);
                        builder.function(Function::functionTable.names.pow, 2);
                        terms++;
                    }
                }
                
                if (terms > 1)
                {
                    builder.function(Function::functionTable.names.sum, terms);
                }
                else if (terms == 0)
                {
                    builder.constant(0);
                }
                else
                {
                    // Correct number of items on stack already.
                }
                
                applyActivationFunction(builder, activationFunction, threshold, altitude, terms, width);
                
                // Softmax is based on e^x[i], so take the exponent here.
                if (normalizationMethod == NORMALIZATION_METHOD_SOFTMAX)
                {
                    builder.function(Function::functionTable.names.exp, 1);
                }

                const char * idString = neuron->Attribute("id");
                PMMLDocument::ConstFieldDescriptionPtr thisVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "neuron");
                builder.declare(thisVariable, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
                // Sometimes we may want to directly recover the activation value of a neuron.
                // this will write it to an output variable if we have marked it.
                builder.context().markNeuron(idString, thisVariable);

                if (nodeIDToVariableMap.emplace(idString, thisVariable).second == false)
                {
                    builder.parsingError("Duplicate node ID at %i\n", idString, neuron->GetLineNum());
                    return false;
                }
                thisLayerVars.insert(thisVariable);
            }
            
            // Apply normalization to the layer.
            if (normalizationMethod != NORMALIZATION_METHOD_NONE && !thisLayerVars.empty())
            {
                // Take the reciprical of the sum of the layer to minimise division
                builder.constant(1);
                for (const auto & name : thisLayerVars)
                {
                    builder.field(name);
                }
                if (thisLayerVars.size() > 1)
                {
                    builder.function(Function::functionTable.names.sum, thisLayerVars.size());
                }
                builder.function(Function::functionTable.names.divide, 2);
                PMMLDocument::ConstFieldDescriptionPtr layerSumVariable = builder.context().createVariable(PMMLDocument::TYPE_NUMBER, "layer_sum");
                builder.declare(layerSumVariable, AstBuilder::HAS_INITIAL_VALUE);
                blockSize++;
                
                // Multiply with each element
                for (const auto & name : thisLayerVars)
                {
                    builder.field(name);
                    builder.field(layerSumVariable);
                    builder.function(Function::functionTable.names.times, 2);
                    builder.assign(name);
                    blockSize++;
                }
            }
            return true;
        }
        
        
        bool parseNeuralOutputs(AstBuilder & builder, std::vector<std::string> & values, PMMLDocument::ModelConfig & config, const tinyxml2::XMLElement * neuralOutputs)
        {
            PMMLDocument::ProbabilitiesOutputMap & probsOutputName = config.probabilityValueName;
            
            for (const tinyxml2::XMLElement * neuralOutput = neuralOutputs->FirstChildElement("NeuralOutput"); neuralOutput; neuralOutput = neuralOutput->NextSiblingElement("NeuralOutput"))
            {
                const char * outputNeuron = neuralOutput->Attribute("outputNeuron");
                if (outputNeuron == nullptr)
                {
                    builder.parsingError("No outputNeuron specified at %i\n", neuralOutput->GetLineNum());
                    return false;
                }
                
                auto found = nodeIDToVariableMap.find(outputNeuron);
                if (found == nodeIDToVariableMap.end())
                {
                    builder.parsingError("Connection to outputNeuron which was not defined at %i\n", outputNeuron, neuralOutput->GetLineNum());
                    return false;
                }
                
                const tinyxml2::XMLElement * derivedField = neuralOutput->FirstChildElement("DerivedField");
                if (derivedField == nullptr)
                {
                    builder.parsingError("No DerivedField specified at %i\n", neuralOutput->GetLineNum());
                    return false;
                }
                
                const tinyxml2::XMLElement * transform = PMMLDocument::skipExtensions(derivedField->FirstChildElement());
                if (transform == nullptr)
                {
                    builder.parsingError("No transformation specified at %i\n", derivedField->GetLineNum());
                    return false;
                }
                
                Transformation::ExpressionType expressionType = Transformation::getExpressionTypeFromString(transform->Name());
                if (expressionType == Transformation::EXPRESSION_NORM_CONTINUOUS)
                {
                    if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
                    {
                        builder.parsingError("Not sure how to denormalize NormContinuous in classification model at %i\n", transform->GetLineNum());
                        return false;
                    }
                    
                    builder.field(found->second);
                    AstNode node = builder.popNode();
                    
                    if (!Transformation::parseNormContinuousBody(builder, transform, node, Transformation::DENORMALIZE))
                    {
                        return false;
                    }
                    
                    builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                    blockSize++;
                }
                else if (expressionType == Transformation::EXPRESSION_FIELD_REF)
                {
                    if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
                    {
                        builder.parsingError("Not sure how to denormalize FieldRef in classification model at %i\n", transform->GetLineNum());
                        return false;
                    }
                    
                    builder.field(found->second);
                    builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
                    blockSize++;
                }
                else if (expressionType == Transformation::EXPRESSION_NORM_DISCRETE)
                {
                    if (config.function == PMMLDocument::FUNCTION_REGRESSION)
                    {
                        builder.parsingError("Not sure how to denormalize NormDiscrete in regression model at %i\n", transform->GetLineNum());
                        return false;
                    }
                    
                    const char * value = transform->Attribute("value");
                    if (value == nullptr)
                    {
                        builder.parsingError("No value specified at %i\n", transform->GetLineNum());
                        return false;
                    }
                    values.push_back(value);
                    
                    builder.field(found->second);
                    auto categoryOutput = PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), probsOutputName, "probabilities", config.outputType, value);
                    builder.declare(categoryOutput, AstBuilder::HAS_INITIAL_VALUE);
                    blockSize++;
                }
                else
                {
                    builder.parsingError("Not sure how to denormalize at %i\n", transform->Name(), transform->GetLineNum());
                    return false;
                }
            }
            
            if (config.function == PMMLDocument::FUNCTION_CLASSIFICATION)
            {
                blockSize += PMMLDocument::pickWinner(builder, config, config.probabilityValueName);
            }
            return true;
        }
    };
}
}

bool NeuralNetworkModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    const char * activationFunctionName = node->Attribute("activationFunction");
    if (activationFunctionName == nullptr)
    {
        builder.parsingError("No activationFunction specified at %i\n", node->GetLineNum());
        return false;
    }
    ActivationFunction defaultActivationFunction = getActivationFunctionFromString(activationFunctionName);
    if (defaultActivationFunction == ACTIVATION_INVALID)
    {
        builder.parsingError("Unknown activationFunction: at %i\n", activationFunctionName, node->GetLineNum());
        return false;
    }
    
    NormalizationMethod defaultNormalizationMethod = NORMALIZATION_METHOD_NONE;
    if (const char * normalizationMethodName = node->Attribute("normalizationMethod"))
    {
        defaultNormalizationMethod = getNormalizationMethodFromString(normalizationMethodName);
        if (defaultNormalizationMethod == NORMALIZATION_METHOD_INVALID)
        {
            builder.parsingError("Unknown normalizationMethodName: at %i\n", normalizationMethodName, node->GetLineNum());
            return false;
        }
    }
    
    double defaultThreshold = 0;
    if (node->QueryDoubleAttribute("threshold", &defaultThreshold) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
    {
        builder.parsingError("invalid threshold value at %i\n", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * neuralInputs = node->FirstChildElement("NeuralInputs");
    if (neuralInputs == nullptr)
    {
        builder.parsingError("No NeuralInputs specified at %i\n", node->GetLineNum());
        return false;
    }
    
    double defaultAltitude = 0;
    if (node->QueryDoubleAttribute("altitude", &defaultAltitude) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
    {
        builder.parsingError("invalid altitude value", node->GetLineNum());
    }
    
    double defaultWidth = 0;
    if (node->QueryDoubleAttribute("width", &defaultWidth) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
    {
        builder.parsingError("invalid width value", node->GetLineNum());
    }
    
    NeuralNetParseState parseState(builder.context(), defaultActivationFunction, defaultNormalizationMethod, defaultThreshold, defaultAltitude, defaultWidth);

    if (!parseState.parseNeuralInputs(builder, neuralInputs))
    {
        return false;
    }
    
    for (const tinyxml2::XMLElement * neuralLayer = node->FirstChildElement("NeuralLayer"); neuralLayer; neuralLayer = neuralLayer->NextSiblingElement("NeuralLayer"))
    {
        if (!parseState.parseNeuralLayer(builder, neuralLayer))
        {
            return false;
        }
    }
    
    const tinyxml2::XMLElement * neuralOutputs = node->FirstChildElement("NeuralOutputs");
    if (neuralOutputs == nullptr)
    {
        builder.parsingError("No NeuralOutputs specified at %i\n", node->GetLineNum());
        return false;
    }
    
    std::vector<std::string> values;
    if (!parseState.parseNeuralOutputs(builder, values, config, neuralOutputs))
    {
        return false;
    }
    
    builder.block(parseState.blockSize);
    
    return true;
}
