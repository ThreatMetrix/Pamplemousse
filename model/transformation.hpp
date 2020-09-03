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
//  Created by Caleb Moore on 12/9/18.
//

#ifndef transformation_hpp
#define transformation_hpp

#include "document.hpp"
#include "conversioncontext.hpp"

namespace Transformation
{
    // These two lists must be kept in sync and ordered
    enum ExpressionType
    {
        EXPRESSION_AGGREGATE,
        EXPRESSION_APPLY,
        EXPRESSION_CONSTANT,
        EXPRESSION_DISCRETIZE,
        EXPRESSION_FIELD_REF,
        EXPRESSION_LAG,
        EXPRESSION_MAP_VALUES,
        EXPRESSION_NORM_CONTINUOUS,
        EXPRESSION_NORM_DISCRETE,
        EXPRESSION_TEXT_INDEX,
        EXPRESSION_INVALID
    };
    
    ExpressionType getExpressionTypeFromString(const char * name);

    // This function converts a transformation into an AST
    bool parse(AstBuilder & builder, const tinyxml2::XMLElement * node);

    bool parseTransformationDictionary(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ScopedVariableDefinitionStackGuard & scope, size_t & blockSize);
    void importTransformationDictionary(AstBuilder & builder, PMMLDocument::ScopedVariableDefinitionStackGuard & scope, size_t & blockSize);
    bool parseLocalTransformations(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ScopedVariableDefinitionStackGuard & scope, size_t & blocksize);
    
    struct Interval
    {
        enum Closure
        {
            NONE,
            OPEN,
            CLOSED
        };
        
        Closure leftClosure = NONE;
        Closure rightClosure = NONE;
        double leftMargin;
        double rightMargin;
        bool parse(const tinyxml2::XMLElement * interval);
        void addLeftCondition(AstBuilder & builder, const AstNode & field) const;
        void addRightCondition(AstBuilder & builder, const AstNode & field) const;
        bool isIn(double val) const;
    };
    
    struct DiscretizeBin
    {
        Interval interval;
        std::string binValue;
        bool parse(const tinyxml2::XMLElement * interval);
    };
    
    bool parseDiscretizeBins(std::vector<DiscretizeBin> & bins, const tinyxml2::XMLElement * interval);
    void findHolesInDiscretizeBins(AstBuilder & builder, const std::vector<DiscretizeBin> & bins, const AstNode & field);

    enum NormContinuousMode
    {
        NORMALIZE,
        DENORMALIZE
    };
    bool parseNormContinuousBody(AstBuilder & builder, const tinyxml2::XMLElement * node, AstNode field, NormContinuousMode mode);
}

#endif /* transformation_hpp */
