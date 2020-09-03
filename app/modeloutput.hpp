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

#ifndef modeloutput_hpp
#define modeloutput_hpp

#include "document.hpp"
#include "conversioncontext.hpp"

namespace PMMLExporter
{
    struct ModelOutput
    {
        std::string modelOutput;
        std::string variableOrAttribute;
        PMMLDocument::ConstFieldDescriptionPtr field;
        double factor = 1;
        double coefficient = 0;
        int decimalPoints = -1;
        
        ModelOutput(const std::string & mo, const std::string & voa,
                    PMMLDocument::ConstFieldDescriptionPtr field = PMMLDocument::ConstFieldDescriptionPtr());
        bool tryToBind(PMMLDocument::ConversionContext & context);
        bool bindToModel(PMMLDocument::ConversionContext & context);
    };
    
    void printPossibleOutputs(const PMMLDocument::DataDictionary & outputDictionary, bool onlyNumeric);
}

#endif /* modeloutput_hpp */
