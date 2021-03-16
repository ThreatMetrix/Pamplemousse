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
//  Created by Caleb Moore on 29/11/18.
//

#include "rulesetmodel.hpp"
#include "ast.hpp"
#include "predicate.hpp"
#include "treemodel.hpp"

#include <algorithm>
#include <vector>

struct Rule
{
    AstNode value;
    AstNode predicate;
    double weight;
    Rule(AstBuilder & builder, double w) :
        value(builder.popNode()),
        predicate(builder.popNode()),
        weight(w)
    {}
};

bool parseScope(std::vector<Rule> & rules, AstBuilder & builder, const tinyxml2::XMLElement * firstRule, PMMLDocument::ModelConfig & config, const AstNode * parentPredicate)
{
    for (const tinyxml2::XMLElement *  rule = firstRule; rule; rule = PMMLDocument::skipExtensions(rule->NextSiblingElement()))
    {
        const tinyxml2::XMLElement * predicate = PMMLDocument::skipExtensions(rule->FirstChildElement());
        
        if (parentPredicate)
        {
            builder.pushNode(*parentPredicate);
        }
        if (!Predicate::parse(builder, predicate))
        {
            return false;
        }
        if (parentPredicate)
        {
            builder.function(Function::functionTable.names.fnAnd, 2);
        }
        
        if (strcmp(rule->Name(), "SimpleRule") == 0)
        {
            if (!TreeModel::writeScore(builder, rule, config, nullptr))
            {
                return false;
            }
            double weight = 1;
            if (rule->QueryDoubleAttribute("weight", &weight) == tinyxml2::XML_WRONG_ATTRIBUTE_TYPE)
            {
                builder.parsingError("Invalid weight", rule->GetLineNum());
            }
            // This will pop two nodes from the builder inside the Rule constructor.
            rules.emplace_back(builder, weight);
        }
        else if (strcmp(rule->Name(), "CompoundRule") == 0)
        {
            AstNode myPredicate = builder.popNode();
            if (!parseScope(rules, builder, PMMLDocument::skipExtensions(predicate->NextSiblingElement()), config, &myPredicate))
            {
                return false;
            }
        }
        else
        {
            builder.parsingError("Unknown rule", rule->Name(), rule->GetLineNum());
            return false;
        }
    }
    return true;
}

bool RulesetModel::parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config)
{
    const tinyxml2::XMLElement * ruleSet = node->FirstChildElement("RuleSet");
    if (ruleSet == nullptr)
    {
        builder.parsingError("No RuleSet", node->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * ruleSelectionMethod = ruleSet->FirstChildElement("RuleSelectionMethod");
    if (ruleSelectionMethod == nullptr)
    {
        builder.parsingError("No RuleSelectionMethod", ruleSet->GetLineNum());
        return false;
    }
    const char * criterion = ruleSelectionMethod->Attribute("criterion");
    if (criterion == nullptr)
    {
        builder.parsingError("No criterion", ruleSet->GetLineNum());
        return false;
    }
    
    const tinyxml2::XMLElement * firstRule = PMMLDocument::skipExtensions(ruleSelectionMethod->NextSiblingElement());
    while (firstRule != nullptr && strcmp(firstRule->Name(), "RuleSelectionMethod") == 0)
    {
        firstRule = PMMLDocument::skipExtensions(firstRule->NextSiblingElement());
    }
    
    std::vector<Rule> rules;
    if (!parseScope(rules, builder, firstRule, config, nullptr))
    {
        return false;
    }
    
    if (strcmp(criterion, "firstHit") == 0)
    {
        // Already in correct order
    }
    else if (strcmp(criterion, "weightedMax") == 0)
    {
        // Put highest weight rules first.
        std::stable_sort(rules.begin(), rules.end(), [](const Rule & a, const Rule & b){ return a.weight > b.weight; });
    }
    else if (strcmp(criterion, "weightedSum") == 0)
    {
        builder.parsingError("Sorry, weightedSum rule selection criterion is not supported", ruleSelectionMethod->GetLineNum());
        return false;
    }
    else
    {
        builder.parsingError("Unknown rule selection criterion: %s not supported", criterion, ruleSelectionMethod->GetLineNum());
        return false;
    }
    
    size_t ifChainSize = 0;
    for (const Rule & rule : rules)
    {
        builder.pushNode(rule.value);
        builder.pushNode(rule.predicate);
        ifChainSize += 2;
    }
    
    const char * defaultScore = ruleSet->Attribute("defaultScore");
    if (defaultScore != nullptr || ruleSet->FirstChildElement("ScoreDistribution"))
    {
        if (!TreeModel::writeScore(builder, ruleSet, config, nullptr, defaultScore))
        {
            return false;
        }
        ifChainSize++;
    }
    
    builder.ifChain(ifChainSize);
    
    // Declare the probability outputs before the model
    AstNode ifChain = builder.popNode();
    size_t numDeclarations = 0;
    for (const auto & probOutput : config.probabilityValueName)
    {
        builder.declare(probOutput.second, AstBuilder::NO_INITIAL_VALUE);
        numDeclarations++;
    }
    
    builder.pushNode(std::move(ifChain));
    builder.block(numDeclarations + 1);
    
    return true;
}
