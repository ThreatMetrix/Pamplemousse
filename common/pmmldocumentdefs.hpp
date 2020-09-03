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

#ifndef pmmldocumentdefs_hpp
#define pmmldocumentdefs_hpp

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <cstring>

namespace tinyxml2
{
    class XMLElement;
}

namespace PMMLDocument
{
    // These are specified from most permissive to least permissive. I.e. a value may be implicitly cast upwards but not downwards.
    enum FieldType
    {
        TYPE_STRING,
        TYPE_NUMBER,
        TYPE_BOOL,
        TYPE_INVALID,
        TYPE_VOID,
        TYPE_LAMBDA,
        TYPE_TABLE,
        TYPE_STRING_TABLE
    };
    
    // This specifies where a field was defined, as outputted code may have to treat them differently.
    enum FieldOrigin
    {
        ORIGIN_DATA_DICTIONARY,
        ORIGIN_TEMPORARY,
        ORIGIN_OUTPUT,
        ORIGIN_TRANSFORMED_VALUE,
        ORIGIN_PARAMETER,
        ORIGIN_SPECIAL
    };
    
    enum MiningFunction
    {
        FUNCTION_REGRESSION,
        FUNCTION_CLASSIFICATION,
        FUNCTION_ANY // dummy value only used for constraints.
    };
    
    enum OpType
    {
        OPTYPE_CATEGORICAL,
        OPTYPE_CONTINUOUS,
        OPTYPE_ORDINAL,
        OPTYPE_INVALID
    };
    
    struct DataField
    {
        DataField(FieldType dt, OpType ot) : dataType(dt), opType(ot) {}
        FieldType dataType;
        OpType opType;
        std::vector<std::string> values;
    };
    typedef std::vector<std::pair<std::string, DataField>> DataFieldVector;
    
    enum OutlierTreatment
    {
        OUTLIER_TREATMENT_AS_EXTREME_VALUES,
        OUTLIER_TREATMENT_AS_IS,
        OUTLIER_TREATMENT_AS_MISSING_VALUES,
        OUTLIER_TREATMENT_INVALID
    };
    
    struct FieldDescription
    {
        static unsigned int next_id;
        FieldDescription(const DataField & t, FieldOrigin o, const std::string & name) :
            field(t),
            origin(o),
            luaName(name),
            id(next_id++)
        {}
        FieldDescription(FieldType t, FieldOrigin o, OpType ot, const std::string & name) :
            field(t, ot),
            origin(o),
            luaName(name),
            id(next_id++)
        {}
        const DataField field;
        FieldOrigin origin;
        std::string luaName;
        unsigned int id;
        mutable size_t overflowAssignment = 0;
    };
    typedef std::shared_ptr<const FieldDescription> ConstFieldDescriptionPtr;
    typedef std::unordered_map<std::string, ConstFieldDescriptionPtr> DataDictionary;
    
    // This is an entry in the mining field list of a model.
    struct MiningField
    {
        ConstFieldDescriptionPtr variable;
        bool hasReplacementValue;
        std::string replacementValue;
        OutlierTreatment outlierTreatment;
        double minValue;
        double maxValue;
        MiningField(const ConstFieldDescriptionPtr & var) :
            variable(var),
            hasReplacementValue(false),
            outlierTreatment(OUTLIER_TREATMENT_AS_IS)
        {
        }
    };
    
    // This is used for binary searching in binary string lists.
    inline bool stringIsBefore(const char * a, const char * b)
    {
        return std::strcmp(a, b) < 0;
    }
    
    enum MiningFieldUsage
    {
        USAGE_IN,
        USAGE_OUT,
        USAGE_IGNORED
    };
    
    PMMLDocument::MiningFieldUsage getMiningFieldUsage(const tinyxml2::XMLElement * field);
    FieldType dataTypeFromString(const char * string);
    OpType optypeFromString(const char * optypeString);
    OutlierTreatment outlierTreatmentFromString(const char * string);
    
    class ConversionContext;
    struct ModelConfig;
}
class AstBuilder;
class LuaOutputter;

#endif /* pmmldocumentdefs_hpp */
