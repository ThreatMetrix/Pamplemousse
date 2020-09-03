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

#ifndef output_hpp
#define output_hpp

#include "tinyxml2.h"
#include "../common/pmmldocumentdefs.hpp"

#include <set>
#include <string>

namespace Output
{
    PMMLDocument::DataFieldVector findAllOutputs(const tinyxml2::XMLElement * element);
    const char * findOutputForFeature(const tinyxml2::XMLElement * element, const char * featureName, bool requireNoValue);
    bool addOutputValues(AstBuilder & builder, const tinyxml2::XMLElement * element, const PMMLDocument::ModelConfig & modelConfig, size_t & blocksize);
    void doTargetPostprocessing(AstBuilder & builder, const tinyxml2::XMLElement * targets, const PMMLDocument::ModelConfig & modelConfig, size_t & blockSize);
    void mapDisplayValue(AstBuilder & builder, const tinyxml2::XMLElement * targets, const PMMLDocument::ModelConfig & modelConfig);
}
#endif /* output_hpp */
