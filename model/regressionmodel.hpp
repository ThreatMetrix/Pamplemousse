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
//  Created by Caleb Moore on 27/9/18.
//

#ifndef regression_hpp
#define regression_hpp

#include "document.hpp"
#include "tinyxml2.h"

namespace RegressionModel
{
    enum RegressionNormalizationMethod
    {
        METHOD_CAUCHIT,
        METHOD_CLOGLOG,
        METHOD_EXP,
        METHOD_IDENTITY,
        METHOD_LOG,
        METHOD_LOGC,
        METHOD_LOGIT,
        METHOD_LOGLOG,
        METHOD_NONE,
        METHOD_PROBIT,
        METHOD_SIMPLEMAX,
        METHOD_SOFTMAX,
        METHOD_INVALID
    };

    bool parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config);
    bool buildCatagoricalPredictor(AstBuilder & builder, const tinyxml2::XMLElement * node, double coefficient);
    RegressionNormalizationMethod getRegressionNormalizationMethodFromString(const char * name);
    void normalizeTable(AstBuilder & builder, RegressionNormalizationMethod normMethod, bool clamp);
}

#endif /* regression_hpp */
