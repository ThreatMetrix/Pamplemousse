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
//  Created by Caleb Moore on 21/11/18.
//

#include "supportvectormachine.hpp"
#include "ast.hpp"
#include "predicate.hpp"
#include "transformation.hpp"
#include "regressionmodel.hpp"
#include <vector>

// Note: the value is a string to prevent converting to and from double and losing precision
typedef std::unordered_map<size_t, std::string> SupportVector;

class SVMKernel
{
protected:
    ~SVMKernel() = default;
public:
    virtual void apply(AstBuilder & builder, const std::vector<AstNode> & fields, const SupportVector & vector) const = 0;
};

class LinearKernel : public SVMKernel
{
public:
    void apply(AstBuilder & builder, const std::vector<AstNode> & fields, const SupportVector & vector) const final
    {
        size_t toSum = 0;
        for (SupportVector::const_iterator iter = vector.begin(); iter != vector.end(); ++iter)
        {
            builder.pushNode(fields[iter->first]);
            builder.constant(iter->second, PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.times, 2);
            toSum++;
        }
        builder.function(Function::functionTable.names.sum, toSum);
    }
};

class PolynomialKernel : public SVMKernel
{
    double gamma;
    double coef0;
    double degree;
public:
    bool read(const tinyxml2::XMLElement * kernel)
    {
        gamma = 1;
        if (kernel->QueryDoubleAttribute("gamma", &gamma) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "invalid gamma value at %i\n", kernel->GetLineNum());
            return false;
        }
        coef0 = 1;
        if (kernel->QueryDoubleAttribute("coef0", &coef0) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "invalid coef0 value at %i\n", kernel->GetLineNum());
            return false;
        }
        degree = 1;
        if (kernel->QueryDoubleAttribute("degree", &degree) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "invalid degree value at %i\n", kernel->GetLineNum());
            return false;
        }
        return true;
    }
    
    void apply(AstBuilder & builder, const std::vector<AstNode> & fields, const SupportVector & vector) const final
    {
        size_t toSum = 0;
        if (coef0 != 0)
        {
            builder.constant(coef0);
            toSum++;
        }
        
        for (SupportVector::const_iterator iter = vector.begin(); iter != vector.end(); ++iter)
        {
            builder.pushNode(fields[iter->first]);
            builder.constant(iter->second, PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.times, 2);
            toSum++;
        }
        builder.function(Function::functionTable.names.sum, toSum);
        
        if (gamma != 1)
        {
            builder.constant(gamma);
            builder.function(Function::functionTable.names.times, 2);
        }
        if (degree != 1)
        {
            builder.constant(degree);
            builder.function(Function::functionTable.names.pow, 2);
        }
    }
};

class RadialBasisKernel : public SVMKernel
{
    std::string gamma;
public:
    bool read(const tinyxml2::XMLElement * kernel)
    {
        gamma = 1;
        if (const char * gammaVal = kernel->Attribute("gamma"))
        {
            gamma = gammaVal;
        }
        return true;
    }
    
    void apply(AstBuilder & builder, const std::vector<AstNode> & fields, const SupportVector & vector) const final
    {
        size_t toSum = 0;
        size_t vectorIndex = 0;
        // This is a little bit different from other basis functions in that elements of 0 cannot be ignored
        for (std::vector<AstNode>::const_iterator iter = fields.begin(); iter != fields.end(); ++iter, vectorIndex++)
        {
            builder.pushNode(*iter);
            auto foundElement = vector.find(vectorIndex);
            if (foundElement != vector.end())
            {
                builder.constant(foundElement->second, PMMLDocument::TYPE_NUMBER);
            }
            else
            {
                builder.constant(0);
            }
            builder.function(Function::functionTable.names.minus, 2);
            builder.constant(2);
            builder.function(Function::functionTable.names.pow, 2);
            toSum++;
        }
        if (toSum > 1)
        {
            builder.function(Function::functionTable.names.sum, toSum);
        }
        builder.function(Function::unaryMinus, 1);
        if (gamma != "1")
        {
            builder.constant(gamma, PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.times, 2);
        }
        builder.function(Function::functionTable.names.exp, 1);
    }
};


class SigmoidKernel : public SVMKernel
{
    double gamma;
    double coef0;
public:
    bool read(const tinyxml2::XMLElement * kernel)
    {
        gamma = 1;
        if (kernel->QueryDoubleAttribute("gamma", &gamma) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "invalid gamma value at %i\n", kernel->GetLineNum());
            return false;
        }
        coef0 = 1;
        if (kernel->QueryDoubleAttribute("coef0", &coef0) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "invalid coef0 value at %i\n", kernel->GetLineNum());
            return false;
        }
        return true;
    }
    
    void apply(AstBuilder & builder, const std::vector<AstNode> & fields, const SupportVector & vector) const final
    {
        size_t toSum = 0;
        if (coef0 != 0)
        {
            builder.constant(coef0);
            toSum++;
        }
        
        for (SupportVector::const_iterator iter = vector.begin(); iter != vector.end(); ++iter)
        {
            builder.pushNode(fields[iter->first]);
            builder.constant(iter->second, PMMLDocument::TYPE_NUMBER);
            builder.function(Function::functionTable.names.times, 2);
            toSum++;
        }
        if (toSum > 1)
        {
            builder.function(Function::functionTable.names.sum, toSum);
        }
        
        if (gamma != 1)
        {
            builder.constant(gamma);
            builder.function(Function::functionTable.names.times, 2);
        }
        builder.function(Function::functionTable.names.tanh, 1);
    }
};

bool readVectorInstances(std::unordered_map<std::string, SupportVector> & vectors, const tinyxml2::XMLElement * vectorDictionary)
{
    for (const tinyxml2::XMLElement * vectorInstance = vectorDictionary->FirstChildElement("VectorInstance");
         vectorInstance; vectorInstance = vectorInstance->NextSiblingElement("VectorInstance"))
    {
        const char * id = vectorInstance->Attribute("id");
        if (id == nullptr)
        {
            fprintf(stderr, "No id for VectorInstance at %i\n", vectorInstance->GetLineNum());
            return false;
        }
        std::pair<std::unordered_map<std::string, SupportVector>::iterator, bool> inserted = vectors.emplace(id, SupportVector());
        if (inserted.second == false)
        {
            fprintf(stderr, "Duplicate id %s for VectorInstance at %i\n", id, vectorInstance->GetLineNum());
            return false;
        }
        SupportVector & newVector = inserted.first->second;
        if (const tinyxml2::XMLElement * array = vectorInstance->FirstChildElement("Array"))
        {
            size_t index = 0;
            for (PMMLArrayIterator iterator(array->GetText()); iterator.isValid(); ++iterator, ++index)
            {
                char * end;
                std::string thisString(iterator.stringStart(), iterator.stringEnd() - iterator.stringStart());
                double value = strtod(thisString.c_str(), &end);
                if (*end != '\0')
                {
                    fprintf(stderr, "Error parsing number: %s at %i\n", thisString.c_str(), vectorInstance->GetLineNum());
                    return false;
                }
                if (value != 0)
                {
                    newVector[index] = thisString;
                }
            }
        }
        else if (const tinyxml2::XMLElement * sparseArray = vectorInstance->FirstChildElement("REAL-SparseArray"))
        {
            const tinyxml2::XMLElement * incides = sparseArray->FirstChildElement("Indices");
            const tinyxml2::XMLElement * entries = sparseArray->FirstChildElement("REAL-Entries");
            if (incides == nullptr && entries == nullptr)
            {
                // empty sparse array
                continue;
            }
            
            if (incides == nullptr)
            {
                fprintf(stderr, "SparseArray without Indices at %i\n", sparseArray->GetLineNum());
                return false;
            }
            std::vector<size_t> indexVector;
            for (PMMLArrayIterator iterator(incides->GetText()); iterator.isValid(); ++iterator)
            {
                char * end;
                long value = strtol(iterator.stringStart(), &end, 10);
                if (end != iterator.stringEnd())
                {
                    std::string thisString(iterator.stringStart(), iterator.stringEnd() - iterator.stringStart());
                    fprintf(stderr, "Error parsing number: %s at %i\n", thisString.c_str(), vectorInstance->GetLineNum());
                    return false;
                }
                if (value < 1)
                {
                    fprintf(stderr, "Index must be greater than 1 (value %li) at %i\n", value, vectorInstance->GetLineNum());
                    return false;
                }
                // we use indexes starting at zero internally.
                indexVector.push_back(value - 1);
            }
            
            if (entries == nullptr)
            {
                fprintf(stderr, "SparseArray without REAL-Entries at %i\n", sparseArray->GetLineNum());
                return false;
            }
            std::vector<size_t>::const_iterator indexIterator = indexVector.begin();
            PMMLArrayIterator iterator(entries->GetText());
            for (; iterator.isValid() && indexIterator != indexVector.end(); ++iterator, ++indexIterator)
            {
                char * end;
                std::string thisString(iterator.stringStart(), iterator.stringEnd() - iterator.stringStart());
                double value = strtod(thisString.c_str(), &end);
                if (*end != '\0')
                {
                    fprintf(stderr, "Error parsing number: %s at %i\n", thisString.c_str(), vectorInstance->GetLineNum());
                    return false;
                }
                if (value != 0)
                {
                    newVector[*indexIterator] = thisString;
                }
            }
            
            if (indexIterator != indexVector.end())
            {
                fprintf(stderr, "Not enough values for space array at %i\n", sparseArray->GetLineNum());
                return false;
            }
            
            if (iterator.isValid())
            {
                fprintf(stderr, "Not enough values for space array at %i\n", sparseArray->GetLineNum());
                return false;
            }
        }
        else
        {
            fprintf(stderr, "No array found for VectorInstance at %i\n", vectorInstance->GetLineNum());
            return false;
        }
    }
    return true;
}

bool convertSVM(AstBuilder & builder, const SVMKernel & kernel, const std::vector<AstNode> & fields, const std::unordered_map<std::string, SupportVector> & vectors, const tinyxml2::XMLElement * supportVectorMachine)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    
    const tinyxml2::XMLElement * supportVectors = supportVectorMachine->FirstChildElement("SupportVectors");
    if (supportVectors == nullptr)
    {
        fprintf(stderr, "No SupportVectors at %i\n", supportVectorMachine->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * coefficients = supportVectorMachine->FirstChildElement("Coefficients");
    if (coefficients == nullptr)
    {
        fprintf(stderr, "No Coefficients at %i\n", supportVectorMachine->GetLineNum());
        return false;
    }
    
    size_t valuesToSum = 0;
    if (const char * absoluteValue = coefficients->Attribute("absoluteValue"))
    {
        char * endPtr;
        double value = strtod(absoluteValue, &endPtr);
        if (*endPtr != '\0')
        {
            fprintf(stderr, "invalid absoluteValue at %i\n", coefficients->GetLineNum());
            return false;
        }
        
        if (value != 0.0)
        {
            // Use the original string to avoid conversion mistakes
            builder.constant(absoluteValue, PMMLDocument::TYPE_NUMBER);
            valuesToSum++;
        }
    }
    
    const tinyxml2::XMLElement * nextSupportVector = supportVectors->FirstChildElement("SupportVector");
    const tinyxml2::XMLElement * nextCoefficient = coefficients->FirstChildElement("Coefficient");
    while (nextSupportVector != nullptr && nextCoefficient != nullptr)
    {
        const char * vectorId = nextSupportVector->Attribute("vectorId");
        if (vectorId == nullptr)
        {
            fprintf(stderr, "Absent vectorId at %i\n", nextSupportVector->GetLineNum());
            return false;
        }
        
        auto found = vectors.find(vectorId);
        if (found == vectors.end())
        {
            fprintf(stderr, "Unknown vectorId \"%s\" at %i\n", vectorId, nextSupportVector->GetLineNum());
            return false;
        }
        
        const char * coefficient = nextCoefficient->Attribute("value");
        if (coefficient == nullptr)
        {
            fprintf(stderr, "Absent value for coefficient at %i\n", nextCoefficient->GetLineNum());
            return false;
        }
        
        char * endp;
        double coefficientAsDouble = strtod(coefficient, &endp);
        if (*endp != '\0')
        {
            fprintf(stderr, "Invalid value %s for coefficient at %i\n", coefficient, nextCoefficient->GetLineNum());
            return false;
        }
        
        if (coefficientAsDouble != 0)
        {
            kernel.apply(builder, fields, found->second);
            
            if (coefficientAsDouble == -1)
            {
                builder.function(Function::unaryMinus, 1);
            }
            else if (coefficientAsDouble != 1)
            {
                // Do not convert coefficient back to string, always use original value.
                builder.constant(coefficient, PMMLDocument::TYPE_NUMBER);
                builder.function(Function::functionTable.names.times, 2);
            }
            valuesToSum++;
        }
        nextSupportVector = nextSupportVector->NextSiblingElement("SupportVector");
        nextCoefficient = nextCoefficient->NextSiblingElement("Coefficient");
    }
    
    if (nextCoefficient != nullptr)
    {
        fprintf(stderr, "Too many coefficients (or not enough support vectors) at %i\n", supportVectorMachine->GetLineNum());
        return false;
    }
    
    if (nextSupportVector != nullptr)
    {
        fprintf(stderr, "Too many support vectors (or not enough coefficients) at %i\n", supportVectorMachine->GetLineNum());
        return false;
    }
    
    builder.function(Function::functionTable.names.sum, valuesToSum);
    return true;
}

bool convertThresholdSVM(AstBuilder & builder, const SVMKernel & kernel, const std::vector<AstNode> & fields, const std::unordered_map<std::string, SupportVector> & vectors,
                         bool maxWins, double defaultThreshold, const tinyxml2::XMLElement * supportVectorMachine)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    
    double threshold = defaultThreshold;
    if (supportVectorMachine->QueryDoubleAttribute("threshold", &threshold) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
    {
        fprintf(stderr, "invalid threshold value at %i\n", supportVectorMachine->GetLineNum());
        return false;
    }
    
    if (!convertSVM(builder, kernel, fields, vectors, supportVectorMachine))
    {
        return false;
    }
    
    builder.constant(threshold);
    
    if (maxWins)
    {
        builder.function(Function::functionTable.names.greaterThan, 2);
    }
    else
    {
        builder.function(Function::functionTable.names.lessThan, 2);
    }
    
    return true;
}

bool convertOneAgainstOne(AstBuilder & builder, const SVMKernel & kernel, const std::vector<AstNode> & fields, const std::unordered_map<std::string, SupportVector> & vectors,
                          const tinyxml2::XMLElement * firstSupportVectorMachine, bool maxWins, double defaultThreshold, PMMLDocument::ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    size_t blockSize = 0;
    // Mapping target category, to an array of predicate variables that add to it.
    // the boolean value represents to invert the predicate if it is true.
    typedef std::vector<std::pair<PMMLDocument::ConstFieldDescriptionPtr, bool>> CategoryPredicates;
    std::unordered_map<std::string, CategoryPredicates> categorySet;
    for (const tinyxml2::XMLElement * supportVectorMachine = firstSupportVectorMachine; supportVectorMachine;
         supportVectorMachine = supportVectorMachine->NextSiblingElement("SupportVectorMachine"))
    {
        const char * targetCategory = supportVectorMachine->Attribute("targetCategory");
        const char * alternateTargetCategory = supportVectorMachine->Attribute("alternateTargetCategory");
        if (targetCategory == nullptr || alternateTargetCategory == nullptr)
        {
            fprintf(stderr, "SupportVectorMachine requires targetCategory and alternateTargetCategory at %i\n", supportVectorMachine->GetLineNum());
            return false;
        }
        
        // Predicate
        if (!convertThresholdSVM(builder, kernel, fields, vectors, maxWins, defaultThreshold, supportVectorMachine))
        {
            return false;
        }
        
        PMMLDocument::ConstFieldDescriptionPtr predicateField = builder.context().createVariable(PMMLDocument::TYPE_BOOL, std::string(targetCategory) + "_or_" + std::string(alternateTargetCategory));
        
        builder.declare(predicateField, AstBuilder::HAS_INITIAL_VALUE);
        blockSize++;
        
        auto targetEmplaced = categorySet.emplace(targetCategory, CategoryPredicates());
        targetEmplaced.first->second.emplace_back(predicateField, false);
        
        auto alternateTargetEmplaced = categorySet.emplace(alternateTargetCategory, CategoryPredicates());
        alternateTargetEmplaced.first->second.emplace_back(predicateField, true);
    }
    
    // Work out the sum of how many times a category won.
    for (const auto & item : categorySet)
    {
        for (const auto & predicate : item.second)
        {
            builder.field(predicate.first);
            // Select between 1 and 0 if not inverted (was target)
            // Select between 0 and 1 if inverted (was alternate target)
            builder.constant(!predicate.second ? 1 : 0);
            builder.constant(!predicate.second ? 0 : 1);
            builder.function(Function::functionTable.names.ternary, 3);
        }
        builder.function(Function::functionTable.names.sum, item.second.size());
        
        builder.declare(PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities_output", PMMLDocument::TYPE_NUMBER, item.first), AstBuilder::HAS_INITIAL_VALUE);
        blockSize++;
    }
    
    blockSize += PMMLDocument::normaliseProbabilitiesAndPickWinner(builder, config);
    builder.block(blockSize);
    
    return true;
}

bool convertOneAgainstAll(AstBuilder & builder, const SVMKernel & kernel, const std::vector<AstNode> & fields, const std::unordered_map<std::string, SupportVector> & vectors,
                          const tinyxml2::XMLElement * firstSupportVectorMachine, bool maxWins, PMMLDocument::ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    PMMLDocument::ScopedVariableDefinitionStackGuard defs(builder.context());
    size_t blockSize = 0;
    
    for (const tinyxml2::XMLElement * supportVectorMachine = firstSupportVectorMachine; supportVectorMachine;
         supportVectorMachine = supportVectorMachine->NextSiblingElement("SupportVectorMachine"))
    {
        const char * targetCategory = supportVectorMachine->Attribute("targetCategory");
        if (targetCategory == nullptr)
        {
            fprintf(stderr, "SupportVectorMachine requires targetCategory at %i\n", supportVectorMachine->GetLineNum());
            return false;
        }
        
        if (!convertSVM(builder, kernel, fields, vectors, supportVectorMachine))
        {
            return false;
        }
        
        if (!maxWins)
        {
            // If less is better, flip it.
            builder.function(Function::unaryMinus, 1);
        }
        
        builder.declare(PMMLDocument::getOrAddCategoryInOutputMap(builder.context(), config.probabilityValueName, "probabilities_output", PMMLDocument::TYPE_NUMBER, targetCategory), AstBuilder::HAS_INITIAL_VALUE);

        blockSize++;
    }
    
    blockSize += PMMLDocument::normaliseProbabilitiesAndPickWinner(builder, config);
    builder.block(blockSize);
    
    return true;
}

bool parseWithKernel(AstBuilder & builder, const tinyxml2::XMLElement * node, const SVMKernel & kernel, PMMLDocument::ModelConfig & config)
{
    double defaultThreshold = 0;
    if (node->QueryDoubleAttribute("threshold", &defaultThreshold) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
    {
        fprintf(stderr, "invalid threshold value at %i\n", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * vectorDictionary = node->FirstChildElement("VectorDictionary");
    if (vectorDictionary == nullptr)
    {
        fprintf(stderr, "No VectorDictionary at %i\n", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * vectorFields = vectorDictionary->FirstChildElement("VectorFields");
    if (vectorFields == nullptr)
    {
        fprintf(stderr, "No VectorDictionary at %i\n", vectorDictionary->GetLineNum());
        return false;
    }

    size_t nFields = 0;
    for (const tinyxml2::XMLElement * childNode = PMMLDocument::skipExtensions(vectorFields->FirstChildElement());
         childNode; childNode = PMMLDocument::skipExtensions(childNode->NextSiblingElement()))
    {
        if (std::strcmp(childNode->Name(), "FieldRef") == 0)
        {
            if (!Transformation::parse(builder, childNode))
            {
                return false;
            }
        }
        else if (std::strcmp(childNode->Name(), "CategoricalPredictor") == 0)
        {
            double coefficient;
            if (childNode->QueryDoubleAttribute("coefficient", &coefficient))
            {
                fprintf(stderr, "coefficient required at %i\n", childNode->GetLineNum());
                return false;
            }
            
            if (!RegressionModel::buildCatagoricalPredictor(builder, childNode, coefficient))
            {
                return false;
            }
        }
        
        
        nFields++;
    }
    std::vector<AstNode> fields;
    builder.popNodesIntoVector(fields, nFields);
    
    std::unordered_map<std::string, SupportVector> vectors;
    if (!readVectorInstances(vectors, vectorDictionary))
    {
        return false;
    }
    
    const tinyxml2::XMLElement * firstSupportVectorMachine = node->FirstChildElement("SupportVectorMachine");
    if (firstSupportVectorMachine == nullptr)
    {
        fprintf(stderr, "No SupportVectorMachine at %i\n", node->GetLineNum());
        return false;
    }
    
    if (config.function == PMMLDocument::FUNCTION_REGRESSION)
    {
        if (!convertSVM(builder, kernel, fields, vectors, firstSupportVectorMachine))
        {
            return false;
        }
        builder.declare(config.outputValueName, AstBuilder::HAS_INITIAL_VALUE);
        return true;
    }
    else
    {
        bool maxWins = false;
        if (node->QueryBoolAttribute("maxWins", &maxWins) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
        {
            fprintf(stderr, "Invalid value for maxWins at %i\n", node->GetLineNum());
            return false;
        }
        
        // Special case, "binary" classification.
        if (firstSupportVectorMachine->NextSiblingElement("SupportVectorMachine") == nullptr)
        {
             // OneAgainstOne is semantically the same as binary classification, but it will give a probability of 1 to one category and 0 to the other.
            return convertOneAgainstOne(builder, kernel, fields, vectors, firstSupportVectorMachine, maxWins, defaultThreshold, config);
        }
        else
        {
            if (const char * classificationMethod = node->Attribute("classificationMethod"))
            {
                if (strcmp(classificationMethod, "OneAgainstOne") == 0)
                {
                    return convertOneAgainstOne(builder, kernel, fields, vectors, firstSupportVectorMachine, maxWins, defaultThreshold, config);
                }
                else if (strcmp(classificationMethod, "OneAgainstAll") != 0)
                {
                    fprintf(stderr, "Invalid value %s for classificationMethod at %i\n", classificationMethod, node->GetLineNum());
                    return false;
                }
            }
            
            // OneAgainstAll is the default
            return convertOneAgainstAll(builder, kernel, fields, vectors, firstSupportVectorMachine, maxWins, config);
        }
        return true;
    }
}

bool SupportVectorMachine::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    ASSERT_AST_BUILDER_ONE_NEW_NODE(builder);
    if (const tinyxml2::XMLElement * linearKernel = node->FirstChildElement("LinearKernelType"))
    {
        LinearKernel kernel;
        return parseWithKernel(builder, node, kernel, config);
    }
    else if (const tinyxml2::XMLElement * polynomialKernel = node->FirstChildElement("PolynomialKernelType"))
    {
        PolynomialKernel kernel;
        if (!kernel.read(polynomialKernel))
        {
            return false;
        }
        return parseWithKernel(builder, node, kernel, config);
    }
    else if (const tinyxml2::XMLElement * radialBasisKernel = node->FirstChildElement("RadialBasisKernelType"))
    {
        RadialBasisKernel kernel;
        if (!kernel.read(radialBasisKernel))
        {
            return false;
        }
        return parseWithKernel(builder, node, kernel, config);
    }
    else if (const tinyxml2::XMLElement * sigmoidKernel = node->FirstChildElement("SigmoidKernelType"))
    {
        SigmoidKernel kernel;
        if (!kernel.read(sigmoidKernel))
        {
            return false;
        }
        return parseWithKernel(builder, node, kernel, config);
    }
    else
    {
        fprintf(stderr, "No recognised kernel specified at %i\n", node->GetLineNum());
        return false;
    }
}

