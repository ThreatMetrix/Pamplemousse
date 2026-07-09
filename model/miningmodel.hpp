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
//  Created by Caleb Moore on 31/8/18.
//

#ifndef miningmodel_hpp
#define miningmodel_hpp

#include "document.hpp"
#include "tinyxml2.h"

namespace MiningModel
{
    bool parse(AstBuilder & builder, const tinyxml2::XMLElement * node, PMMLDocument::ModelConfig & config);

    // Apply the same linear transform (x -> x*factor + constant) that
    // <Target rescaleFactor="..." rescaleConstant="..."/> applies to the
    // model's score output, so the Saabas invariant
    //     bias + sum(per-feature contributions) == model_score
    // continues to hold for the contribution bundle.
    //
    // - The bias accumulator (if set) is updated in place.
    // - Each entry of the contributions table (if set) is multiplied by
    //   factor when factor != 1.0; rescaleConstant only affects the bias.
    // No-op (and emits nothing) if neither factor nor constant is set or
    // the model has no biasAccumulator/contributionsTable to update.
    //
    // Returns the number of statements emitted (added to blockSize by caller).
    size_t applyRescaleToContributions(AstBuilder & builder,
                                       const PMMLDocument::ModelConfig & config,
                                       bool hasFactor, double factor,
                                       bool hasConstant, double constant);
}

#endif /* miningmodel_hpp */

