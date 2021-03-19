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
//  Created by Caleb Moore on 4/1/19.
//

#include "modeloutput.hpp"
#include <iostream>
#include <algorithm>

const std::string NEURON_PREFIX = "neuron:";

void PMMLExporter::printPossibleOutputs(const PMMLDocument::DataDictionary & outputDictionary, bool onlyNumeric)
{
    bool hasSuitableOutputs = false;
    for (auto & output : outputDictionary)
    {
        if (!onlyNumeric || output.second->field.dataType == PMMLDocument::TYPE_NUMBER)
        {
            hasSuitableOutputs = true;
        }
    }

    if (!hasSuitableOutputs)
    {
        std::cout << "No suitable outputs found. Model should have at least one numerical output." << std::endl;
    }
    else
    {
        std::cout << "Possible outputs in model:" << std::endl;
        for (auto & output : outputDictionary)
        {
            if (!onlyNumeric || output.second->field.dataType == PMMLDocument::TYPE_NUMBER)
            {
                std::cout << "\t * " << output.first << std::endl;
            }
        }
    }
}

PMMLExporter::ModelOutput::ModelOutput(const std::string & mo, const std::string & voa, PMMLDocument::ConstFieldDescriptionPtr f) :
    modelOutput(mo),
    variableOrAttribute(voa),
    field(f)
{
}

bool PMMLExporter::ModelOutput::tryToBind(PMMLDocument::ConversionContext & context)
{
    // The score might be found in a neuron, which requires a special case.
    if (std::strncmp(modelOutput.c_str(), NEURON_PREFIX.c_str(), NEURON_PREFIX.length()) == 0)
    {
        std::string name = modelOutput.substr(NEURON_PREFIX.length());
        if ((field = context.findNeuron(name)))
        {
            return true;
        }
    }
    
    // Try the name directly first... in case we have an oddly named output.
    if (auto found = context.getFieldDescription(modelOutput.c_str()))
    {
        if (found->origin == PMMLDocument::ORIGIN_OUTPUT)
        {
            field = found;
            return true;
        }
    }
    return false;
}

bool PMMLExporter::ModelOutput::bindToModel(PMMLDocument::ConversionContext & context)
{
    if (tryToBind(context))
    {
        return true;
    }
    
    // Maybe we have a few mathematical operations in there? We support +, -, * and / after the model output, but not before.
    // This parsing is done much later than you may expect as we want to know what the outputs are called, just in case
    // we have some of these characters in the name of the output itself.
    size_t opPos;
    while ((opPos = modelOutput.find_last_of("+-*/,")) != std::string::npos)
    {
        char * endOfStr;
        double newTerm = strtod(modelOutput.c_str() + opPos + 1, &endOfStr);
        if (*endOfStr != '\0')
        {
            // Last part of the expression isn't numeric
            break;
        }
        
        const char op = modelOutput[opPos];
        switch (op)
        {
            case '+':
                coefficient += newTerm * factor;
                factor = 1.0;
                break;
                
            case '-':
                coefficient -= newTerm * factor;
                factor = 1.0;
                break;
                
            case '/':
                factor /= newTerm;
                break;
                
            case '*':
                factor *= newTerm;
                break;
                
            case ',':
                decimalPoints = int(newTerm);
                break;
                
            default:
                break;
        }
        
        modelOutput.resize(opPos);
        // Try to bind now! Try this each time, just in case there are mathematical operations in the output name.
        if (tryToBind(context))
        {
            return true;
        }
    }
    
    return false;
}
