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
//  Created by Caleb Moore on 2/9/20.
//

#ifndef basicexport_hpp
#define basicexport_hpp

#include "pmmldocumentdefs.hpp"


namespace PMMLExporter
{
    enum class Format
    {
        AS_MULTI_ARG,
        AS_TABLE
    };
    struct ModelOutput;
    // Generate a Lua script from sourceFile into the already-configured luaOutputter
    // inputs and outputs are both io parameters. If they are non-empty, they will be used. If they are empty, we will populate them from the model.
    bool createScript(const char * sourceFile, LuaOutputter & luaOutputter,
                      std::vector<PMMLExporter::ModelOutput> & inputs, std::vector<PMMLExporter::ModelOutput> & outputs,
                      Format inputFormat = Format::AS_MULTI_ARG, Format outputFormat = Format::AS_MULTI_ARG);
}

#endif /* basicexport_hpp */
