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
//  Created by Caleb Moore on 25/3/20.
//

#include "pmmldocumentdefs.hpp"
#include <tinyxml2.h>
#include <algorithm>

PMMLDocument::MiningFieldUsage PMMLDocument::getMiningFieldUsage(const tinyxml2::XMLElement * field)
{
    if (const char * usageType = field->Attribute("usageType"))
    {
        if (strcmp(usageType, "target") == 0 || strcmp(usageType, "predicted") == 0)
        {
            return USAGE_OUT;
        }
        if (strcmp(usageType, "active") != 0)
        {
            return USAGE_IGNORED;
        }
    }
    // Default to "active" if no usageType
    return USAGE_IN;
}

PMMLDocument::FieldType PMMLDocument::dataTypeFromString(const char * type)
{
    if (std::strcmp(type, "double") == 0 || std::strcmp(type, "float") == 0 ||
        std::strcmp(type, "long") == 0 || std::strcmp(type, "int") == 0 || std::strcmp(type, "integer") == 0 || std::strcmp(type, "short") == 0 ||
        std::strcmp(type, "byte") == 0 || std::strcmp(type, "unsignedLong") == 0 || std::strcmp(type, "unsignedInt") == 0 ||
        std::strcmp(type, "unsignedShort") == 0 || std::strcmp(type, "unsignedByte") == 0)
    {
        return TYPE_NUMBER;
    }
    else if ( std::strcmp(type, "boolean") == 0 )
    {
        return TYPE_BOOL;
    }
    else if ( std::strcmp(type, "string") == 0 )
    {
        return TYPE_STRING;
    }
    else
    {
        return TYPE_INVALID;
    }
}

const char * const OUTLIER_TREATMENT_METHOD_NAMES[] = {
    "asExtremeValues",
    "asIs",
    "asMissingValues"
};

PMMLDocument::OutlierTreatment PMMLDocument::outlierTreatmentFromString(const char * outlierTreatmentString)
{
    auto found = std::equal_range(OUTLIER_TREATMENT_METHOD_NAMES, OUTLIER_TREATMENT_METHOD_NAMES + static_cast<int>(OUTLIER_TREATMENT_INVALID), outlierTreatmentString, stringIsBefore);
    if (found.first != found.second)
    {
        return static_cast<OutlierTreatment>(found.first - OUTLIER_TREATMENT_METHOD_NAMES);
    }
    else
    {
        return OUTLIER_TREATMENT_INVALID;
    }
}

const char * const OPTYPE_NAMES[] = {
    "categorical",
    "continuous",
    "ordinal"
};

PMMLDocument::OpType PMMLDocument::optypeFromString(const char * optypeString)
{
    auto found = std::equal_range(OPTYPE_NAMES, OPTYPE_NAMES + static_cast<int>(OPTYPE_INVALID), optypeString, stringIsBefore);
    if (found.first != found.second)
    {
        return static_cast<OpType>(found.first - OPTYPE_NAMES);
    }
    else
    {
        return OPTYPE_INVALID;
    }
}

