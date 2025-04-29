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

#include "transformation.hpp"
#include "ast.hpp"
#include "analyser.hpp"
#include "function.hpp"
#include "conversioncontext.hpp"
#include "luaconverter/luaoutputter.hpp"
#include <limits>
#include <algorithm>
#include <assert.h>

namespace Transformation
{
namespace
{
    const char * const EXPRESSION_NAME[]
    {
        "Aggregate",
        "Apply",
        "Constant",
        "Discretize",
        "FieldRef",
        "Lag",
        "MapValues",
        "NormContinuous",
        "NormDiscrete",
        "TextIndex"
    };
    
    bool getField(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * fieldName = node->Attribute("field");
        if (fieldName == nullptr)
        {
            builder.parsingError("Missing field attribute for", node->Name(), node->GetLineNum());
            return false;
        }
        
        if (builder.context().isLoadingTransformationDictionary())
        {
            if (auto description = builder.context().getFieldDescription(fieldName))
            {
                builder.field(description);
            }
            else
            {
                builder.parsingError("Unknown field", fieldName, node->GetLineNum());
                return false;
            }

        }
        else
        {
            const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(fieldName);
            if (fieldDefinition == nullptr)
            {
                builder.parsingError("Unknown miningfield", fieldName, node->GetLineNum());
                return false;
            }
            
            builder.field(fieldDefinition);
        }
        return true;
    }
    
    // sklearn models do not understand mapMissingTo... we use the default value here.
    const char * mapMissingToAttr(const AstBuilder & builder)
    {
        return builder.context().getApplication() == "JPMML-SkLearn" ? "defaultValue" : "mapMissingTo";
    }
}

    // This tells you what a value is from the type name
    ExpressionType getExpressionTypeFromString(const char * name)
    {
        auto found = std::equal_range(EXPRESSION_NAME, EXPRESSION_NAME + static_cast<int>(EXPRESSION_INVALID), name, PMMLDocument::stringIsBefore);
        if (found.first != found.second)
        {
            return static_cast<ExpressionType>(found.first - EXPRESSION_NAME);
        }
        else
        {
            return EXPRESSION_INVALID;
        }
    }
    
    // This tells you whether a string is a number or not from the value.
    PMMLDocument::FieldType typeFromText(const char * text)
    {
        char * end;
        strtod(text, &end);
        if (*end == '\0')
        {
            return PMMLDocument::TYPE_NUMBER;
        }
        else
        {
            return PMMLDocument::TYPE_STRING;
        }
    }

    // This reads a constant ref and pushes it to the top of the builder
    bool parseConstant(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * content = node->GetText();
        if (content == nullptr)
        {
            builder.parsingError("Empty constant field", node->GetLineNum());
            return false;
        }

        // Some exporters seem to output parameters as constants (not field refs)... check if this is that situation
        if (auto description = builder.context().getFieldDescription(content))
        {
            if (description->origin == PMMLDocument::ORIGIN_PARAMETER)
            {
                builder.field(description);
                return true;
            }
        }
        
        // Type is derived from content by default
        PMMLDocument::FieldType fieldType = typeFromText(node->GetText());
        if (const char * dataType = node->Attribute("dataType"))
        {
            // Type is given explicitly
            PMMLDocument::FieldType specifiedType = PMMLDocument::dataTypeFromString(dataType);
            if (specifiedType == PMMLDocument::TYPE_INVALID)
            {
                builder.parsingError("Invalid type name %s", dataType, node->GetLineNum());
                return false;
            }
            // FieldType will be set to string if it contains non numbers. Casting the other way won't work.
            if (fieldType == PMMLDocument::TYPE_STRING && specifiedType == PMMLDocument::TYPE_NUMBER)
            {
                builder.parsingError("Invalid numeric constant: %s", content, node->GetLineNum());
                return false;
            }
            fieldType = specifiedType;
        }

        builder.constant(content, fieldType);

        return true;
    }

    // This reads a field ref (with respect to the mining schema) and pushes it to the top of the builder
    bool parseFieldRef(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        if (!getField(builder, node))
        {
            return false;
        }
        
        if (const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder)))
        {
            // Use defaultValue rather than mapMissingValue, since it's not about whether the arguments are null, but the return value itself
            builder.defaultValue(mapMissingTo);
        }

        return true;
    }

    // This simply reads the value of a LinearNorm element as two doubles
    bool readLinearNorm(const AstBuilder & builder, const tinyxml2::XMLElement * node, double & orig, double & norm)
    {
        if (node->QueryDoubleAttribute("orig", &orig) ||
            node->QueryDoubleAttribute("norm", &norm))
        {
            builder.parsingError("Linear norm needs a orig and norm", node->GetLineNum());
            return false;
        }
        return true;
    }

    // This parses a NormContinuous transformation and pushes it to the top of builder
    bool parseNormContinuous(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        if (!getField(builder, node))
        {
            return false;
        }
        
        return parseNormContinuousBody(builder, node, builder.popNode(), NORMALIZE);
    }
    
    // Recursively build a binary search tree out of ternary instructions, to find the correct range in log(n) checks.
    // Note: indexes start at 1 so that they can be unsigned, but still have a potential range under the first interval... which is why you see a "-1" inside every single index
    void buildNormTable(AstBuilder & builder, const std::vector<double> & origins, const std::vector<double> & normals, const AstNode & field,
                        size_t bottom, size_t top)
    {
        size_t range = top - bottom;
        if (range > 1)
        {
            size_t cutoff = bottom + range / 2;
            builder.pushNode(field);
            builder.constant(origins[cutoff - 1]);
            builder.function(Function::functionTable.names.lessThan, 2);
            buildNormTable(builder, origins, normals, field, bottom, cutoff);
            buildNormTable(builder, origins, normals, field, cutoff, top);
            builder.function(Function::functionTable.names.ternary,  3);
        }
        else if (range == 1)
        {
            // These are extreme values, only ever used if outlierTreatment == PMMLDocument::OUTLIER_TREATMENT_AS_EXTREME_VALUES
            if (top == 1)
            {
                builder.constant(normals[top - 1]);
            }
            else if (bottom == origins.size())
            {
                builder.constant(normals[bottom - 1]);
            }
            else
            {
                double gradient = (normals[top - 1] - normals[bottom - 1]) / (origins[top - 1] - origins[bottom - 1]);

                builder.pushNode(field);
                builder.constant(origins[bottom - 1]);
                builder.function(Function::functionTable.names.minus, 2);
                builder.constant(gradient);
                builder.function(Function::functionTable.names.times, 2);
                builder.constant(normals[bottom - 1]);
                builder.function(Function::functionTable.names.plus, 2);
            }
        }
        else
        {
            assert(false);
        }
    }

    // This is an internal function to parse a norm continuous transformation and push it to the top of builder.
    // It is called by Transformation parsing and within Neural Networks.
    // mode may be NORMALIZE or DENORMALIZE. Changing it to DENORMALIZE causes it to work in reverse, flipping the line diagonally.
    bool parseNormContinuousBody(AstBuilder & builder, const tinyxml2::XMLElement * node, AstNode field, NormContinuousMode mode)
    {
        PMMLDocument::OutlierTreatment outlierTreatment = PMMLDocument::OUTLIER_TREATMENT_AS_IS;
        if (const char * outliers = node->Attribute("outliers"))
        {
            outlierTreatment = PMMLDocument::outlierTreatmentFromString(outliers);
            if (outlierTreatment == PMMLDocument::OUTLIER_TREATMENT_INVALID)
            {
                builder.parsingError("Invalid outlier treatment", outliers, node->GetLineNum());
                return false;
            }
        }

        std::vector<double> origins;
        std::vector<double> normals;
        for (const tinyxml2::XMLElement * nextLinearNorm = node->FirstChildElement("LinearNorm"); nextLinearNorm; nextLinearNorm = nextLinearNorm->NextSiblingElement("LinearNorm"))
        {
            double lastOrig;
            double lastNorm;
            if (!readLinearNorm(builder, nextLinearNorm, lastOrig, lastNorm))
            {
                return false;
            }
            if (mode == NORMALIZE)
            {
                origins.push_back(lastOrig);
                normals.push_back(lastNorm);
            }
            else
            {
                origins.push_back(lastNorm);
                normals.push_back(lastOrig);
            }
        }
        if (origins.size() < 2)
        {
            builder.parsingError("NormContinuous with less than two linear norms", node->GetLineNum());
            return false;
        }
        
        double missingReplacementFromField = 0;
        bool fieldMissingValueReplacementOutside = false;
        bool fieldHasValidMissingReplacement = false;
        // If there is a default specified, we can pull it out here and move it outside the rest of the statement.
        if (field.function().functionType == Function::DEFAULT_MACRO &&
            field.type == PMMLDocument::TYPE_NUMBER)
        {
            double asValue = strtod(field.content.c_str(), nullptr);
            
            // Remove the default from the field (the field itself is its child)
            field = field.children[0];
            
            // Find where this value fits
            size_t i = std::distance(origins.begin(), std::lower_bound(origins.begin(), origins.end(), asValue));
            if ((i == 0 || i == origins.size()) && outlierTreatment != PMMLDocument::OUTLIER_TREATMENT_AS_IS)
            {
                if (outlierTreatment == PMMLDocument::OUTLIER_TREATMENT_AS_EXTREME_VALUES)
                {
                    missingReplacementFromField = i == 0 ? normals.front() : normals.back();
                    fieldHasValidMissingReplacement = true;
                }
                else
                {
                    fieldMissingValueReplacementOutside = true;
                }
            }
            else
            {
                // Clamp values for OUTLIER_TREATMENT_AS_IS
                i = std::max(std::vector<double>::size_type(1lu), std::min(i, origins.size() - 1));
                // Work out the mapped value for this default ahead of time
                double gradient = (normals[i] - normals[i - 1]) / (origins[i] - origins[i - 1]);
                missingReplacementFromField = (asValue - origins[i - 1]) * gradient + normals[i - 1];
                fieldHasValidMissingReplacement = true;
            }
        }

        // Map missing to applies only if the root field is missing, so use another ternary to apply it
        builder.pushNode(field);
        builder.function(Function::functionTable.names.isNotMissing, 1);
        
        // The range will be larger when clamping to extreme values as we will need to put a new cutoff on the first and last interval
        switch (outlierTreatment)
        {
            case PMMLDocument::OUTLIER_TREATMENT_AS_EXTREME_VALUES:
                // Add 1 extra on each side to create a flat interval there.
                buildNormTable(builder, origins, normals, field, 0, normals.size() + 1);
                break;

            case PMMLDocument::OUTLIER_TREATMENT_AS_MISSING_VALUES:
                // Add a bounding expression of valid values (field >= firstOrigin and field <= lastOrigin)
                // field >= origins[0]
                builder.pushNode(field);
                builder.constant(origins[0]);
                builder.function(Function::functionTable.names.greaterOrEqual, 2);
                
                // field <= origins[origins.size() - 1]
                builder.pushNode(field);
                builder.constant(origins[origins.size() - 1]);
                builder.function(Function::functionTable.names.lessOrEqual, 2);
                builder.function(Function::functionTable.names.fnAnd, 2);
                
                buildNormTable(builder, origins, normals, field, 1, normals.size());
                
                builder.function(Function::boundFunction, 2);
                break;
                
            default:
                buildNormTable(builder, origins, normals, field, 1, normals.size());
        }

        const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder));
        if (fieldHasValidMissingReplacement)
        {
            builder.constant(missingReplacementFromField);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        else if (fieldMissingValueReplacementOutside || mapMissingTo == nullptr)
        {
            // If missing replacement on the field would have mapped to off the edge, it evaluates to none
            builder.function(Function::boundFunction, 2);
        }
        else
        {
            // The predicate of this ternary is near the top, checking that the field isn't null and the true clause is main body in the middle.
            // This is the false value.
            builder.constant(mapMissingTo, builder.topNode().type);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        
        return true;
    }

    // This parses a NormDiscrete transformation and pushes it to the top of builder
    bool parseNormDiscrete(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        if (!getField(builder, node))
        {
            return false;
        }
        
        const char * value = node->Attribute("value");
        if (value == nullptr)
        {
            builder.parsingError("Missing value attribute FieldRef", node->GetLineNum());
            return false;
        }
        
        const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder));
        if (mapMissingTo)
        {
            // The field is at the top of the stack from getField, take a copy so it can be still at the top of the stack
            AstNode fieldNode = builder.topNode();
            // Map missing to applies only if the root field is missing, so use another ternary to apply it
            builder.function(Function::functionTable.names.isNotMissing, 1);
            builder.pushNode(fieldNode);
        }
        
        builder.constant(value, builder.topNode().type);
        builder.function(Function::functionTable.names.equal, 2);
        builder.constant(1);
        builder.constant(0);
        builder.function(Function::functionTable.names.ternary, 3);
        
        if (mapMissingTo)
        {
            builder.constant(mapMissingTo, builder.topNode().type);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        
        return true;
    }

    // This converts an interval (within Discretize) and pushes it as a boolean predicate to the top of builder
    bool Interval::parse(const AstBuilder & builder, const tinyxml2::XMLElement * interval)
    {
        const char * closure = interval->Attribute("closure");
        if (closure == nullptr)
        {
            builder.parsingError("Missing closure", interval->GetLineNum());
            return false;
        }

        Closure leftClosureIfSpecfied = CLOSED;
        Closure rightClosureIfSpecfied = CLOSED;
        int startOfNextSection = 6; // Length of string "closed"
        if (strncmp(closure, "open", 4) == 0)
        {
            leftClosureIfSpecfied = OPEN;
            startOfNextSection = 4; // Length of string "open"
        }
        else if (strncmp(closure, "closed", 6))
        {
            builder.parsingError("Nonsence closure", closure, interval->GetLineNum());
            return false;
        }
        if (strncmp(closure + startOfNextSection, "Open", 4) == 0)
        {
            rightClosureIfSpecfied = OPEN;
        }
        else if (strncmp(closure + startOfNextSection, "Closed", 6))
        {
            builder.parsingError("Nonsence closure", closure, interval->GetLineNum());
            return false;
        }

        tinyxml2::XMLError leftReturn = interval->QueryAttribute("leftMargin", &leftMargin);
        if (leftReturn == tinyxml2::XML_SUCCESS)
        {
            leftClosure = leftClosureIfSpecfied;
        }
        else if (leftReturn == tinyxml2::XML_NO_ATTRIBUTE)
        {
            leftClosure = NONE;
        }
        else
        {
            builder.parsingError("Invalid leftMargin", interval->GetLineNum());
            return false;
        }
        
        tinyxml2::XMLError rightReturn = interval->QueryAttribute("rightMargin", &rightMargin);
        if (rightReturn == tinyxml2::XML_SUCCESS)
        {
            rightClosure = rightClosureIfSpecfied;
        }
        else if (rightReturn == tinyxml2::XML_NO_ATTRIBUTE)
        {
            rightClosure = NONE;
        }
        else
        {
            builder.parsingError("Invalid rightMargin", interval->GetLineNum());
            return false;
        }
        return true;
    }
    
    void Interval::addLeftCondition(AstBuilder & builder, const AstNode & field) const
    {
        if (leftClosure == NONE)
        {
            builder.constant("true", PMMLDocument::TYPE_BOOL);
        }
        else
        {
            builder.pushNode(field);
            builder.constant(leftMargin);
            builder.function(leftClosure == CLOSED ?
                             Function::functionTable.names.greaterOrEqual:
                             Function::functionTable.names.greaterThan, 2);
        }
    }
    
    void Interval::addRightCondition(AstBuilder & builder, const AstNode & field) const
    {
        if (rightClosure == NONE)
        {
            builder.constant("true", PMMLDocument::TYPE_BOOL);
        }
        else
        {
            builder.pushNode(field);
            builder.constant(rightMargin);
            builder.function(rightClosure == CLOSED ?
                             Function::functionTable.names.lessOrEqual :
                             Function::functionTable.names.lessThan, 2);
        }
    }
    
    bool Interval::isIn(double val) const
    {
        return ((leftClosure != CLOSED || val >= leftMargin) &&
                (leftClosure != OPEN || val > leftMargin) &&
                (rightClosure != CLOSED || val <= rightMargin) &&
                (rightClosure != OPEN || val < rightMargin));
    }
    
    bool DiscretizeBin::parse(const AstBuilder & builder, const tinyxml2::XMLElement * child)
    {
        const tinyxml2::XMLElement * intervalBin = child->FirstChildElement("Interval");
        if (intervalBin == nullptr)
        {
            builder.parsingError("Missing Interval", child->GetLineNum());
            return false;
        }
        
        if (!interval.parse(builder, intervalBin))
        {
            return false;
        }
        
        if (const char * value = child->Attribute("binValue"))
        {
            binValue.assign(value);
        }
        else
        {
            builder.parsingError("binValue required", child->GetLineNum());
            return false;
        }
        return true;
    }
    
    bool parseDiscretizeBins(const AstBuilder & builder, std::vector<DiscretizeBin> & bins, const tinyxml2::XMLElement * node)
    {
        for (const tinyxml2::XMLElement * child = node->FirstChildElement("DiscretizeBin"); child; child = child->NextSiblingElement("DiscretizeBin"))
        {
            bins.emplace_back();
            if (!bins.back().parse(builder, child))
            {
                return false;
            }
        }
        return true;
    }

    void findHolesInDiscretizeBins(AstBuilder & builder, const std::vector<DiscretizeBin> & bins, const AstNode & field)
    {
        if (bins.empty())
        {
            builder.constant("false", PMMLDocument::TYPE_BOOL);
            return;
        }
        
        size_t sizeOfStuff = 0;
        if (bins.front().interval.leftClosure != Interval::NONE)
        {
            bins.front().interval.addLeftCondition(builder, field);
            sizeOfStuff++;
        }
        
        if (bins.back().interval.rightClosure != Interval::NONE)
        {
            bins.back().interval.addRightCondition(builder, field);
            sizeOfStuff++;
        }
     
        // Find if there is a gap between any two intervals
        for (std::vector<DiscretizeBin>::const_iterator prev, iter = bins.begin(); (prev = iter++) != bins.end();)
        {
            if (prev->interval.rightMargin < iter->interval.leftMargin)
            {
                // A gap of a range
                prev->interval.addRightCondition(builder, field);
                iter->interval.addLeftCondition(builder, field);
                builder.function(Function::functionTable.names.fnOr, 2);
                sizeOfStuff++;
            }
            else if (prev->interval.rightMargin == iter->interval.leftMargin &&
                     iter->interval.leftClosure == Interval::OPEN &&
                     prev->interval.rightClosure == Interval::OPEN)
            {
                // A gap of a single value.
                builder.pushNode(field);
                builder.constant(prev->interval.rightMargin);
                builder.function(Function::functionTable.names.notEqual, 2);
                sizeOfStuff++;
            }
        }
        
        if (sizeOfStuff > 1)
        {
            builder.function(Function::functionTable.names.fnAnd, sizeOfStuff);
        }
        else if (sizeOfStuff == 0)
        {
            builder.constant("true", PMMLDocument::TYPE_BOOL);
        }
    }
    
    // Recursively build a binary search tree out of ternary instructions, to find the correct range in log(n) checks.
    void buildDiscretizeTable(AstBuilder & builder, const std::vector<DiscretizeBin> & bins, const AstNode & field, PMMLDocument::FieldType fieldType,
                        size_t bottom, size_t top)
    {
        size_t range = top - bottom;
        if (range > 1)
        {
            size_t cutoff = bottom + range / 2;
            bins[cutoff - 1].interval.addRightCondition(builder, field);
            buildDiscretizeTable(builder, bins, field, fieldType, bottom, cutoff);
            buildDiscretizeTable(builder, bins, field, fieldType, cutoff, top);
            builder.function(Function::functionTable.names.ternary,  3);
        }
        else if (range == 1)
        {
            builder.constant(bins[bottom].binValue, fieldType);
        }
        else
        {
            assert(false);
        }
    }

    // This parses a Discretize transformation and pushes it to the top of builder
    bool parseDiscretize(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        std::vector<DiscretizeBin> bins;
        if (!parseDiscretizeBins(builder, bins, node))
        {
            return false;
        }
        
        if (!getField(builder, node))
        {
            return false;
        }
        
        const char * defaultValue = node->Attribute("defaultValue");
        std::string missingReplacementFromField;
        bool fieldMissingValueReplacementOutside = false;
        bool fieldHasValidMissingReplacement = false;
        
        // If there is a default specified, we can pull it out here and move it outside the rest of the statement.
        // This isn't actually needed, but leads to more efficient, more readable code.
        if (builder.topNode().function().functionType == Function::DEFAULT_MACRO &&
            builder.topNode().type == PMMLDocument::TYPE_NUMBER)
        {
            double asValue = strtod(builder.topNode().content.c_str(), nullptr);
            
            // Remove the default from the field (the field itself is its child)
            builder.pushNode(builder.popNode().children[0]);
            
            // Find where this value fits in the bins
            for (const auto & bin : bins)
            {
                if (bin.interval.isIn(asValue))
                {
                    fieldHasValidMissingReplacement = true;
                    missingReplacementFromField = bin.binValue;
                    break;
                }
            }
            // If nothing is found, then this is not pointing to a mapped region. Use the same logic for out-of-bounds
            fieldMissingValueReplacementOutside = !fieldHasValidMissingReplacement;
        }
        // The field is referenced multiple times, save it first.
        AstNode field = builder.topNode();
        
        PMMLDocument::FieldType fieldType = PMMLDocument::TYPE_STRING;
        if (const char * dataType = node->Attribute("dataType"))
        {
            // Type is given explicitly
            fieldType = PMMLDocument::dataTypeFromString(dataType);
            if (fieldType == PMMLDocument::TYPE_INVALID)
            {
                builder.parsingError("Invalid type name", dataType, node->GetLineNum());
                return false;
            }
        }
        
        // Check that the field is not null. Otherwise, the default value will catch this scenario (which it shouldn't according to the spec).
        // It will be closed as the first argument of a ternary or a bound
        builder.function(Function::functionTable.names.isNotMissing, 1);
        
        // Work out the bits where we return unknown
        findHolesInDiscretizeBins(builder, bins, field);
        
        if (fieldMissingValueReplacementOutside)
        {
            // A missing value in this case will evaluate to default. Use the same condition for both
            builder.function(Function::functionTable.names.fnAnd, 2);
        }
        
        // Work out which bins the value is in.
        buildDiscretizeTable(builder, bins, field, fieldType, 0, bins.size());
        if (defaultValue)
        {
            builder.constant(defaultValue, fieldType);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        else
        {
            // Otherwise, it will return a null
            builder.function(Function::boundFunction, 2);
        }
        
        if (fieldMissingValueReplacementOutside)
        {
            // This is already handled by the default clause
        }
        else if (fieldHasValidMissingReplacement)
        {
            // If the field has its own inbuilt mapping, that takes precedence.
            builder.constant(missingReplacementFromField, fieldType);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        else if (const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder)))
        {
            // If a mapMissingTo attribute is provided, use this if the field is null
            builder.constant(mapMissingTo, fieldType);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        else
        {
            // Otherwise, it will return a null
            builder.function(Function::boundFunction, 2);
        }
        return true;
    }
    
    struct MapRow
    {
        std::vector<std::string> inColumns;
        std::string outColumn;
    };
    
    void buildMapValueTable(AstBuilder & builder, const std::vector<MapRow> & bins, const std::vector<PMMLDocument::ConstFieldDescriptionPtr> & fields,
                            PMMLDocument::FieldType outputFieldType, size_t bottom, size_t top, size_t checked, PMMLDocument::ConstFieldDescriptionPtr variable)
    {
        assert((top - bottom) > 0);
        
        // Every in column has been checked, output the answer
        if (checked == fields.size())
        {
            builder.constant(bins[bottom].outColumn, outputFieldType);
            builder.assign(variable);
            return;
        }
        
        // Generate a vector of unique keys for the column being checked
        std::vector<std::pair<std::string, size_t>> uniqueKeys;
        for (size_t i = bottom; i < top; ++i)
        {
            const std::string & value = bins[i].inColumns[checked];
            if (uniqueKeys.empty() || value != uniqueKeys.back().first)
            {
                uniqueKeys.emplace_back(value, i);
            }
        }
        
        const PMMLDocument::FieldType cmpType = fields[checked]->field.dataType;
        if (uniqueKeys.size() >= 4)
        {
            // Split recursively by the centre value
            const std::pair<std::string, size_t> & cutoff = uniqueKeys[(uniqueKeys.size() + 1) / 2];
            buildMapValueTable(builder, bins, fields, outputFieldType, bottom, cutoff.second, checked, variable);
            
            builder.field(fields[checked]);
            builder.constant(cutoff.first, cmpType);
            builder.function(Function::functionTable.names.lessThan, 2);
            
            buildMapValueTable(builder, bins, fields, outputFieldType, cutoff.second, top, checked, variable);
            builder.ifChain(3);
        }
        else
        {
            // Compare against possible values for this column
            size_t rangeBottom = bottom;
            for (size_t i = 0; i < uniqueKeys.size(); ++i)
            {
                const size_t rangeTop = (i + 1) < uniqueKeys.size() ? uniqueKeys[i + 1].second : top;
                
                buildMapValueTable(builder, bins, fields, outputFieldType, rangeBottom, rangeTop, checked + 1, variable);
                
                builder.field(fields[checked]);
                builder.constant(uniqueKeys[i].first, cmpType);
                builder.function(Function::functionTable.names.equal, 2);
                rangeBottom = rangeTop;
            }
            
            builder.ifChain(uniqueKeys.size() * 2);
        }
    }
    
    
    void buildMapValueTable(AstBuilder & builder, std::vector<MapRow> & bins, const std::vector<PMMLDocument::ConstFieldDescriptionPtr> & fields,
                            PMMLDocument::FieldType outputFieldType, PMMLDocument::ConstFieldDescriptionPtr variable)
    {
        
        std::sort(bins.begin(), bins.end(), [&fields](const MapRow & a, const MapRow & b)
                  {
                      assert(a.inColumns.size() == fields.size());
                      assert(b.inColumns.size() == fields.size());
                      // Sort according to each column, with decending importance
                      for (size_t i = 0; i < fields.size(); ++i)
                      {
                          const std::string & aVal = a.inColumns[i];
                          const std::string & bVal = b.inColumns[i];
                          // Order columns lexically or numerically depending on the type
                          if (fields[i]->field.dataType == PMMLDocument::TYPE_NUMBER)
                          {
                              double diff = atof(aVal.c_str()) - atof(bVal.c_str());
                              if (diff < 0)
                                  return true;
                              if (diff > 0)
                                  return false;
                          }
                          else
                          {
                              int diff = aVal.compare(bVal);
                              if (diff < 0)
                                  return true;
                              if (diff > 0)
                                  return false;
                          }
                    }
                      return false;
                  });
        // Create a nested series of ifs
        buildMapValueTable(builder, bins, fields, outputFieldType, 0, bins.size(), 0, variable);
    }
        
    // This parses a MapValues transformation and pushes it to the top of builder
    bool parseMapValues(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * outputColumn = node->Attribute("outputColumn");
        if (outputColumn == nullptr)
        {
            builder.parsingError("Missing outputColumn", node->GetLineNum());
            return false;
        }
        
        PMMLDocument::FieldType fieldType = PMMLDocument::TYPE_INVALID;
        if (const char * dataType = node->Attribute("dataType"))
        {
            // Type is given explicitly
            fieldType = PMMLDocument::dataTypeFromString(dataType);
            if (fieldType == PMMLDocument::TYPE_INVALID)
            {
                builder.parsingError("Invalid type name", dataType, node->GetLineNum());
                return false;
            }
        }
        
        // Create parameters for passing in the inputs
        std::vector<PMMLDocument::ConstFieldDescriptionPtr> parameters;
        std::vector<std::string> columns;
        for (const tinyxml2::XMLElement * child = node->FirstChildElement("FieldColumnPair"); child; child = child->NextSiblingElement("FieldColumnPair"))
        {
            const char * column = child->Attribute("column");
            if (column == nullptr)
            {
                builder.parsingError("FieldColumnPair requires a column", child->GetLineNum());
                return false;
            }
            // First load the columns onto the AST builder for now... this will verify that they are valid etc.
            if (!getField(builder, child))
            {
                return false;
            }
            columns.push_back(column);
            parameters.push_back(builder.context().createVariable(builder.topNode().type, column, PMMLDocument::ORIGIN_PARAMETER));
        }
        
        const tinyxml2::XMLElement * inlineTable = node->FirstChildElement("InlineTable");
        if (inlineTable == nullptr)
        {
            builder.parsingError("MapValues requires an InlineTable", node->GetLineNum());
            return false;
        }
        
        std::vector<MapRow> rows;
        for (const tinyxml2::XMLElement * row = inlineTable->FirstChildElement("row"); row; row = row->NextSiblingElement("row"))
        {
            rows.emplace_back();
            MapRow & rowOut = rows.back();
            
            for (const std::string & column : columns)
            {
                if (const tinyxml2::XMLElement * columnNode = row->FirstChildElement(column.c_str()))
                {
                    rowOut.inColumns.emplace_back(columnNode->GetText());
                }
                else
                {
                    builder.parsingError("Missing column", column.c_str(), row->GetLineNum());
                    return false;
                }
            }
            
            const tinyxml2::XMLElement * outputColumnNode = row->FirstChildElement(outputColumn);
            if (outputColumn == nullptr)
            {
                builder.parsingError("Missing column", outputColumn, row->GetLineNum());
            }
            
            if (fieldType == PMMLDocument::TYPE_INVALID)
            {
                // If type not specified, work it out automatically (they will be homoginised later)
                fieldType = typeFromText(outputColumnNode->GetText());
            }
 
            rowOut.outColumn.assign(outputColumnNode->GetText());
        }
        
        
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            builder.field(parameters[i]);
        }
   
        // Create a variable for the output
        PMMLDocument::ConstFieldDescriptionPtr variable = builder.context().createVariable(fieldType, "mappedValue");
        builder.declare(variable, AstBuilder::NO_INITIAL_VALUE);
        
        // Create a nested series of ifs
        buildMapValueTable(builder, rows, parameters, fieldType, variable);
        
        if (const char * defaultValue = node->Attribute("defaultValue"))
        {
            // Put the default assignment before the if block
            PMMLDocument::FieldType type = fieldType;
            if (type == PMMLDocument::TYPE_INVALID)
            {
                type = typeFromText(defaultValue);
            }
            
            builder.constant(defaultValue, type);
            builder.assign(variable);
            
            builder.swapNodes(-1, -2);
            builder.block(2);
        }
        
        if (!parameters.empty())
        {
            // Check if any of the inputs are missing
            for (const auto & field : parameters)
            {
                builder.field(field);
                builder.function(Function::functionTable.names.isNotMissing, 1);
            }
            // If there is more than one input, add an and  (in either case, the predicate should now be one node).
            if (parameters.size() > 1)
            {
                builder.function(Function::functionTable.names.fnAnd, parameters.size());
            }
            
            if (const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder)))
            {
                builder.constant(mapMissingTo, builder.topNode().type);
                builder.assign(variable);
                builder.ifChain(3);
            }
            else
            {
                builder.ifChain(2);
            }
        }
        
        // Return the final variable
        builder.field(variable);
        builder.block(3);
 
        // Put it into a lambda (takes in all inputs)
        builder.lambda(parameters.size());
        
        builder.function(Function::runLambda, parameters.size() + 1);
        return true;
    }
    
    // This converts a derived field, which is the basic unit in TransformationDictionary and LocalTransformation
    bool convertDerivedField(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ScopedVariableDefinitionStackGuard &)
    {
        const char * dataType = node->Attribute("dataType");
        if (dataType == nullptr)
        {
            builder.parsingError("Derived field requires dataType", node->GetLineNum());
            return false;
        }

        PMMLDocument::FieldType type = PMMLDocument::dataTypeFromString(dataType);
        if (type == PMMLDocument::TYPE_INVALID)
        {
            builder.parsingError("Unknown type in derived field", dataType, node->GetLineNum());
            return false;
        }

        const tinyxml2::XMLElement * expression = PMMLDocument::skipExtensions(node->FirstChildElement());
        if (expression == nullptr)
        {
            builder.parsingError("No expression in derived field", node->GetLineNum());
            return false;
        }
        
        if (!Transformation::parse(builder, expression))
        {
            return false;
        }
        
        // We ignore the return value here because the specifier has asked for this type and they probably know what they are doing.
        builder.coerceToSpecificTypes(1, &type);
        
        return true;
    }

    // This allows a function to be defined within the PMML document and called through "apply".
    bool convertDefinedFunction(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * name = node->Attribute("name");
        if (name == nullptr)
        {
            builder.parsingError("DefineFunction requires name", node->GetLineNum());
            return false;
        }

        std::vector<PMMLDocument::FieldType> parameterList;
        std::vector<PMMLDocument::ConstFieldDescriptionPtr> parameters;

        PMMLDocument::ScopedVariableDefinitionStackGuard parameterScope(builder.context());
        const tinyxml2::XMLElement * expression = nullptr;
        for (const tinyxml2::XMLElement * child = PMMLDocument::skipExtensions(node->FirstChildElement()); child;
             child = PMMLDocument::skipExtensions(child->NextSiblingElement()))
        {
            // Read function parameter
            if (strcmp(child->Name(), "ParameterField") == 0)
            {
                const char * paramName = child->Attribute("name");
                if (paramName == nullptr)
                {
                    builder.parsingError("ParameterField requires name", child->GetLineNum());
                    return false;
                }
                PMMLDocument::FieldType paramType = PMMLDocument::TYPE_INVALID;
                if (const char * dataType = child->Attribute("dataType"))
                {
                    // Data type is optional. But when it is specified, it must be valid.
                    paramType = PMMLDocument::dataTypeFromString(dataType);
                    if (paramType == PMMLDocument::TYPE_INVALID)
                    {
                        builder.parsingError("Unknown type in ParameterField", dataType, child->GetLineNum());
                        return false;
                    }
                }
                PMMLDocument::OpType opType = PMMLDocument::OPTYPE_INVALID;
                if (const char * type = child->Attribute("optype"))
                {
                    opType = PMMLDocument::optypeFromString(type);
                }
                // Add the data type to the declaration
                parameterList.push_back(paramType);
                // Add the parameter to the scope
                auto desc = parameterScope.addDataField(paramName, paramType, PMMLDocument::ORIGIN_PARAMETER, opType);
                parameters.emplace_back(desc);
                builder.field(desc);
            }
            else
            {
                expression = child;
                break;
            }
        }
        
        if (expression == nullptr)
        {
            builder.parsingError("No content for DefineFunction", node->GetLineNum());
            return false;
        }
        
        
        if (!Transformation::parse(builder, expression))
        {
            return false;
        }
        
        PMMLDocument::FieldType type;
        if (const char * dataType = node->Attribute("dataType"))
        {
            // Data type is optional. But when it is specified, it must be valid.
            type = PMMLDocument::dataTypeFromString(dataType);
            if (type == PMMLDocument::TYPE_INVALID)
            {
                builder.parsingError("Unknown type in DefineFunction", dataType, node->GetLineNum());
                return false;
            }
            // We ignore the return value here because the specifier has asked for this type and they probably know what they are doing.
            builder.coerceToSpecificTypes(1, &type);
        }
        else
        {
            type = builder.topNode().type;
        }
        
        // Find out the nullity properties of this function.
        // We can do this by fiddling with assertions
        const Function::Definition * myRunLambda = &Function::runLambda;
        Analyser::AnalyserContext context;
        if (!context.mightBeMissing(builder.topNode()))
        {
            myRunLambda = &Function::runLambdaNeverMissing;
        }
        else
        {
            // Add an assertion to each parameter to see if the expression may still be unknown
            Analyser::NonNoneAssertionStackGuard testGuard(context);
            for (auto iter = parameters.begin(); iter != parameters.end(); ++iter)
            {
                testGuard.addVariableAssertion(**iter);
            }
            if (!context.mightBeMissing(builder.topNode()))
            {
                myRunLambda = &Function::runLambdaArgsMissing;
            }
        }
        
        builder.lambda(parameterList.size());
        auto var = builder.context().createVariable(PMMLDocument::TYPE_LAMBDA, name);
        builder.declare(var, AstBuilder::HAS_INITIAL_VALUE);
        builder.context().declareCustomFunction(name, var, type, myRunLambda, std::move(parameterList));
        
        return true;
    }


    // Pass a call to a custom function into an AST and push it to the top of builder
    bool parseFunctionExpression(AstBuilder & builder, const char * name, const Function::Definition * definition, const tinyxml2::XMLElement * node,
                                 const PMMLDocument::FieldType * types)
    {
        int i = 0;
        for (const tinyxml2::XMLElement * iterator = node->FirstChildElement(); iterator != nullptr; iterator = iterator->NextSiblingElement(), i++)
        {
            if (!Transformation::parse(builder, iterator))
            {
                return false;
            }
        }
        bool coersionOK = types ? builder.coerceToSpecificTypes(i, types) : builder.coerceToSameType(i);
        if (!coersionOK)
        {
            builder.parsingError("Mismatched argument types", name, node->GetLineNum());
            return false;
        }

        builder.function(*definition, i);

        return true;
    }

    // Parse an if statement into a ternary or bounds and push it to the top of builder
    bool parseIfStatement(AstBuilder & builder, const Function::Definition * definition, const tinyxml2::XMLElement * node)
    {
        const tinyxml2::XMLElement * predicate = node->FirstChildElement();
        if (!Transformation::parse(builder, predicate))
        {
            return false;
        }

        // Argument must be a bool
        const PMMLDocument::FieldType type = PMMLDocument::TYPE_BOOL;
        builder.coerceToSpecificTypes(1, &type);

        const tinyxml2::XMLElement * ifTrue = predicate->NextSiblingElement();
        if (!Transformation::parse(builder, ifTrue))
        {
            return false;
        }

        if (const tinyxml2::XMLElement * ifFalse = ifTrue->NextSiblingElement())
        {
            if (!Transformation::parse(builder, ifFalse))
            {
                return false;
            }
            // The two sides of the ternary must have the same type, or things get weird.
            builder.coerceToSameType(2);
            builder.function(*definition, 3);
        }
        else
        {
            builder.function(Function::boundFunction, 2);
        }
        return true;
    }

    // This parses an "apply" transformation (i.e. a function call) and pushes it to the top of the stack.
    bool parseApply(AstBuilder & builder, const tinyxml2::XMLElement * node)
    {
        const char * functionName = node->Attribute("function");
        if (functionName == nullptr)
        {
            builder.parsingError("Apply needs a function", node->GetLineNum());
            return false;
        }

        size_t nParameters = 0;
        for (const tinyxml2::XMLElement * iterator = node->FirstChildElement(); iterator != nullptr; iterator = iterator->NextSiblingElement())
        {
            nParameters++;
        }

        if (const Function::BuiltInDefinition * found = Function::findBuiltInFunctionDefinition(functionName))
        {
            if (nParameters < found->minArgs || nParameters > found->maxArgs)
            {
                char buffer[4096];
                if (found->maxArgs == (std::numeric_limits<size_t>::max)())
                {
                    snprintf(buffer, sizeof(buffer), "%s expects >= %zu arguments, got %zu", functionName, found->minArgs, nParameters);
                }
                else if (found->minArgs == found->maxArgs)
                {
                    snprintf(buffer, sizeof(buffer), "%s expects %zu arguments, got %zu", functionName, found->minArgs, nParameters);
                }
                else
                {
                    snprintf(buffer, sizeof(buffer),"%s expects %zu-%zu arguments, got %zu", functionName, found->minArgs, found->maxArgs, nParameters);
                }

                builder.parsingError("Wrong number of arguments for built in function", buffer, node->GetLineNum());

                return false;
            }

            if (found->functionType == Function::UNSUPPORTED)
            {
                builder.parsingError("Function has not been implemented\n", functionName, node->GetLineNum());
                return false;
            }

            // These built in functions all have special case processing.
            if (found == &Function::functionTable.names.ternary)
            {
                if (!parseIfStatement(builder, found, node))
                {
                    return false;
                }
            }
            else if (found == &Function::functionTable.names.substring)
            {
                // Substring takes a string and two numbers, so it needs its own special case
                const PMMLDocument::FieldType types[3] = {PMMLDocument::TYPE_STRING, PMMLDocument::TYPE_NUMBER, PMMLDocument::TYPE_NUMBER};
                if (!parseFunctionExpression(builder, functionName, found, node, types))
                {
                    return false;
                }
            }
            else if (found == &Function::functionTable.names.formatNumber)
            {
                const tinyxml2::XMLElement * number = node->FirstChildElement();
                const tinyxml2::XMLElement * format = number->NextSiblingElement();
                if (!Transformation::parse(builder, format))
                {
                    return false;
                }
                if (!Transformation::parse(builder, number))
                {
                    return false;
                }
                const PMMLDocument::FieldType types[2] = {PMMLDocument::TYPE_STRING, PMMLDocument::TYPE_NUMBER};
                if (!builder.coerceToSpecificTypes(2, types))
                {
                    builder.parsingError("Mismatched argument types", functionName, node->GetLineNum());
                    return false;
                }

                builder.function(*found, 2);
                return true;
            }
            else
            {
                if (!parseFunctionExpression(builder, functionName, found, node, nullptr))
                {
                    return false;
                }
            }
        }
        else if (const Function::CustomDefinition * customDefinition = builder.context().findCustomFunction(functionName))
        {
            if (nParameters != customDefinition->parameters.size())
            {
                char buffer[50];
                snprintf(buffer, sizeof(buffer), "%s expects >= %zu arguments, got %zu", functionName, customDefinition->parameters.size(), nParameters);

                builder.parsingError("Wrong number of arguments for custom function", functionName, node->GetLineNum());
            }

            int i = 0;
            for (const tinyxml2::XMLElement * iterator = node->FirstChildElement(); iterator != nullptr; iterator = iterator->NextSiblingElement(), i++)
            {
                if (!Transformation::parse(builder, iterator))
                {
                    return false;
                }
            }
            bool coersionOK = builder.coerceToSpecificTypes(i, &customDefinition->parameters[0]);
            if (!coersionOK)
            {
                builder.parsingError("Mismatched argument types", functionName, node->GetLineNum());
                return false;
            }

            builder.field(customDefinition->functionVariable);
            builder.function(*customDefinition->lambdaDefinition, nParameters + 1);
            builder.topNode().type = builder.topNode().coercedType = customDefinition->outputType;
        }
        else
        {
            builder.parsingError("Function not found", functionName, node->GetLineNum());
            return false;
        }


        // Missing value must be applied before default value because it works on the arguments of its direct child node.
        // As far as I can tell from the spec, it also is logically applied first.
        if (const char * mapMissingTo = node->Attribute(mapMissingToAttr(builder)))
        {
            // Take the newly added function from the stack
            AstNode myFunction = builder.popNode();
            int nodes = 0;
            // Check if any of the inputs are missing
            for (AstNode::Children::const_iterator iter = myFunction.children.begin(); iter != myFunction.children.end(); ++iter)
            {
                builder.pushNode(*iter);
                builder.function(Function::functionTable.names.isNotMissing, 1);
                nodes++;
            }
            // If there is more than one input, add an and function (in either case, the predicate should now be one or zero nodes.
            if (nodes > 1)
            {
                builder.function(Function::functionTable.names.fnAnd, nodes);
            }
            // Put the function back in place (after the predicate, if there is one)
            builder.pushNode(myFunction);
            if (nodes > 0)
            {
                // If there is a predicate, build a ternary to switch between this and the replacement
                builder.constant(mapMissingTo, myFunction.type);
                builder.function(Function::functionTable.names.ternary, 3);
            }
        }

        if (const char * defaultValue = node->Attribute("defaultValue"))
        {
            builder.defaultValue(defaultValue);
        }
        return true;
    }
}

// This parses an individual transformation element and pushes it to the top of builder.
// It is called recursively as transformations may be the argument of other transformations.
bool Transformation::parse(AstBuilder & builder, const tinyxml2::XMLElement * node)
{
    switch(getExpressionTypeFromString(node->Name()))
    {
        case EXPRESSION_APPLY:
            return parseApply(builder, node);

        case EXPRESSION_CONSTANT:
            return parseConstant(builder, node);

        case EXPRESSION_FIELD_REF:
            return parseFieldRef(builder, node);

        case EXPRESSION_NORM_CONTINUOUS:
            return parseNormContinuous(builder, node);

        case EXPRESSION_NORM_DISCRETE:
            return parseNormDiscrete(builder, node);

        case EXPRESSION_DISCRETIZE:
            return parseDiscretize(builder, node);
            
        case EXPRESSION_MAP_VALUES:
            return parseMapValues(builder, node);
            
        case EXPRESSION_AGGREGATE:
        case EXPRESSION_LAG:
        case EXPRESSION_TEXT_INDEX:
            builder.parsingError("Unimplemented expression type", node->Name(), node->GetLineNum());
            return false;

        case EXPRESSION_INVALID:
            builder.parsingError("Invalid expression type", node->Name(), node->GetLineNum());
            return false;
    }
    return true;
}

// This loads the transformation dictionary and makes it available in the context.
bool Transformation::parseTransformationDictionary(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ScopedVariableDefinitionStackGuard & scope, size_t & blockSize)
{
    // With this set, fields will not be checked to make sure they are available in the mining schema.
    builder.context().setLoadingTransformationDictionary(true);
    
    std::shared_ptr<PMMLDocument::TransformationDictionary> dict = std::make_shared<PMMLDocument::TransformationDictionary>();
    for (const tinyxml2::XMLElement * iterator = PMMLDocument::skipExtensions(node->FirstChildElement()); iterator;
         iterator = PMMLDocument::skipExtensions(iterator->NextSiblingElement()))
    {
        if (std::strcmp(iterator->Name(), "DerivedField") == 0)
        {
            const char * name = iterator->Attribute("name");

            if (name == nullptr)
            {
                builder.parsingError("Derived field requires name and optype", node->GetLineNum());
                return false;
            }

            if (!convertDerivedField(builder, iterator, scope))
            {
                return false;
            }

            dict->emplace(name, builder.topNode());
            builder.popNode();
        }
        else if (std::strcmp(iterator->Name(), "DefineFunction") == 0)
        {
            if (!convertDefinedFunction(builder, iterator))
            {
                return false;
            }
            blockSize++;
        }
    }
    builder.context().setLoadingTransformationDictionary(false);
    builder.context().setTransformationDictionary(dict);
    
    return true;
}

// Transformations in the TransformationDictionary are just definitions. They need to be included into each model according to that model's mining schema.
// This is particularly important if a mining schema (for instance) sets a default value or bounds.
bool importElement(AstBuilder & builder, const AstNode & source)
{
    if (source.function().functionType == Function::FIELD_REF)
    {
        // References to fields need to be resolved to follow the mining schema.
        if (const PMMLDocument::MiningField * fieldDefinition = builder.context().getMiningField(source.content))
        {
            builder.field(fieldDefinition);
        }
        else
        {
            // If this cannot be expressed in terms of the mining schema, it isn't an error. The derived field simply will not be available for this model.
            // This is quite expected as there is no way of saying which derived fields you actually need in this model.
            // If someone tries to use that derived field inside this model, it will cause an error.
            return false;
        }
    }
    else
    {
        // Otherwise, it's something algebraic, so this will recurse to deep-copy in this operation (with field references fixed).
        int traversed = 0;
        for (auto & child : source.children)
        {
            if (importElement(builder, child))
            {
                traversed++;
            }
            else
            {
                // One misstep and it needs to roll itself back. This does not always indicate an error, just that the transformation is unavailable in this model.
                while(traversed-- > 0)
                {
                    builder.popNode();
                }
                return false;
            }
        }
        // This copies the node in.
        builder.customNode(source.function(), source.type, source.content, traversed);
    }
    return true;
}

// The transformation dictionary is a global dictionary of transformations that are available to every model within the file.
// This file is called for every one of those models to import every single derived field seperately for each model (WRT that model's mining schema)
void Transformation::importTransformationDictionary(AstBuilder & builder, PMMLDocument::ScopedVariableDefinitionStackGuard & scope, size_t & blockSize)
{
    const PMMLDocument::TransformationDictionary * dict = builder.context().transformationDictionary();
    if (dict == nullptr)
    {
        return;
    }
    for (const auto &pair : *dict)
    {
        if (importElement(builder, pair.second))
        {
            // Element imported successfully and field will be available (otherwise, it is not actually an error, but not available either)
            
            // Fixme, optype is not read correctly.
            auto field = scope.addDataField(pair.first, builder.topNode().type, PMMLDocument::ORIGIN_TRANSFORMED_VALUE, PMMLDocument::OPTYPE_CONTINUOUS);

            builder.declare(field, AstBuilder::HAS_INITIAL_VALUE);

            builder.context().addDefaultMiningField(pair.first, field);
            blockSize++;
        }
    }
}

// Unlike transformation dictionary, local transformations are added to the code and made available right away.
bool Transformation::parseLocalTransformations(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ScopedVariableDefinitionStackGuard & scope,
                                               size_t & blockSize)
{
    for (const tinyxml2::XMLElement * iterator = node->FirstChildElement("DerivedField"); iterator != nullptr; iterator = iterator->NextSiblingElement("DerivedField"))
    {
        const char * name = iterator->Attribute("name");
        const char * optype = iterator->Attribute("optype");

        if (name == nullptr || optype == nullptr)
        {
            builder.parsingError("Derived field requires name and optype", iterator->GetLineNum());
            return false;
        }

        if (!convertDerivedField(builder, iterator, scope))
        {
            return false;
        }

        auto field = scope.addDataField(name, builder.topNode().type, PMMLDocument::ORIGIN_TRANSFORMED_VALUE, PMMLDocument::optypeFromString(optype));

        builder.declare(field, AstBuilder::HAS_INITIAL_VALUE);

        // It is directly accessable as part of the model
        builder.context().addDefaultMiningField(name, field);

        blockSize++;
    }
    return true;
}
