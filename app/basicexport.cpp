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
//  Created by Caleb Moore on 2/9/20.
//

#include "basicexport.hpp"
#include "document.hpp"
#include "conversioncontext.hpp"
#include "modeloutput.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/optimiser.hpp"
#include <iostream>

namespace
{
// Take a CSV input and a dataDictionary from a model and work out which columns can be inputted/verified
bool bindInputColumns(std::vector<PMMLExporter::ModelOutput> & inputColumns, const PMMLDocument::DataDictionary & dataDictionary)
{
    PMMLDocument::DataDictionary unmappedColumns = dataDictionary;
    
    for (auto & columnName : inputColumns)
    {
        auto found = unmappedColumns.find(columnName.modelOutput);
        if (found != unmappedColumns.end())
        {
            columnName.field = found->second;
            unmappedColumns.erase(found);
        }
        else
        {
            // Only warn if this is the second binding... we don't care if the CSV file has extra data.
            if (dataDictionary.find(columnName.modelOutput) != dataDictionary.end())
            {
                std::cerr << "Field: " << columnName.modelOutput << " is specified multiple times." << std::endl;
            }
        }
    }
    
    for (const auto & inputField : unmappedColumns)
    {
        std::cerr << "Field: " << inputField.first << " is not specified in test data." << std::endl;
    }
    return true;
}


const Function::Definition ReturnStatement =
{
    nullptr,
    Function::RETURN_STATEMENT,
    PMMLDocument::TYPE_VOID,
    LuaOutputter::PRECEDENCE_TOP, Function::NEVER_MISSING
};


void addFunctionHeader(LuaOutputter & output, const std::vector<PMMLExporter::ModelOutput> & inputColumns)
{
    output.function("func");
    bool first = true;
    if (output.nOverflowedVariables())
    {
        // Overflow variables are passed in as an array because they may contain input parameters.
        output.keyword("overflow");
        first = false;
    }

    for (const auto & input : inputColumns)
    {
        if (const auto field = input.field)
        {
            if (field->overflowAssignment == 0)
            {
                if (!first)
                {
                    output.comma();
                }
                first = false;
                output.field(field);
            }
        }
    }
    output.finishedArguments();
}

void addMultiReturnStatement(AstBuilder & builder, std::vector<PMMLExporter::ModelOutput> & customOutputs)
{
    size_t goodOutputs = std::count_if(customOutputs.begin(), customOutputs.end(), [&builder](const PMMLExporter::ModelOutput & output){
        if (output.field)
        {
            builder.field(output.field);
            return true;
        }
        return false;
    });
    builder.function(ReturnStatement, goodOutputs);
    builder.block(2);
}

void populateIOWithDictionary(std::vector<PMMLExporter::ModelOutput> & io, const PMMLDocument::DataDictionary & dictionary)
{
    // If no special inputs/outputs are specified, try to get all inputs/outputs.
    if (io.empty())
    {
        for (const auto & column : dictionary)
        {
            io.emplace_back(column.first, column.first);
        }
    }
}
}

bool PMMLExporter::createScript(const char * sourceFile, LuaOutputter & luaOutputter,
                                std::vector<PMMLExporter::ModelOutput> & inputs, std::vector<PMMLExporter::ModelOutput> & outputs)
{
    tinyxml2::XMLDocument doc(sourceFile);
    if (doc.LoadFile(sourceFile) != tinyxml2::XML_SUCCESS)
    {
        printf("Failed to load file \"%s\": %s\n", sourceFile, doc.ErrorStr());
        return false;
    }
    
    AstBuilder builder;
    if (!PMMLDocument::convertPMML( builder, doc.RootElement() ))
    {
        return false;
    }
    
    populateIOWithDictionary(inputs, builder.context().getInputs());
    populateIOWithDictionary(outputs, builder.context().getOutputs());
    
    if (luaOutputter.lowercase())
    {
        const PMMLDocument::DataDictionary & dataDictionary = builder.context().getInputs();
        PMMLDocument::DataDictionary lowercaseDictionary;
        for (const auto & input : dataDictionary)
        {
            std::string lower;
            lower.resize(input.first.size());
            std::transform(input.first.begin(), input.first.end(), lower.begin(), ::tolower);
            lowercaseDictionary.emplace(lower, input.second);
        }
          
        if (!bindInputColumns(inputs, lowercaseDictionary))
        {
            return false;
        }
    }
    else
    {
        if (!bindInputColumns(inputs, builder.context().getInputs()))
        {
            return false;
        }
    }

    size_t countBound = std::count_if(outputs.begin(), outputs.end(), [&builder](PMMLExporter::ModelOutput & output)
    {
        if (output.bindToModel(builder.context()))
            return true;
        std::cerr << "Output \"" <<  output.modelOutput << "\" was not found in the model." << std::endl;
        return false;
    });
    
    // If we can find ANYTHING useful from the model to bind, then that's probably good enough. If not, it's probably not.
    if (countBound == 0)
    {
        std::cerr << "No outputs were successfully bound." << std::endl;
        return false;
    }
    
    addMultiReturnStatement(builder, outputs);
    
    AstNode astTree = builder.popNode();
    PMMLDocument::optimiseAST(astTree, luaOutputter);
    
    addFunctionHeader(luaOutputter, inputs);
    
    LuaConverter::convertAstToLua(astTree, luaOutputter);
    luaOutputter.endBlock();
    return true;
}
