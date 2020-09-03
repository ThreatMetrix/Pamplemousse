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
//  Created by Caleb Moore on 7/10/18.
//

#ifndef testrun_hpp
#define testrun_hpp

#include <ostream>
#include <vector>

namespace PMMLExporter
{
    struct ModelOutput;
    bool doTestRun(const char * sourceFile, const std::vector<ModelOutput> & outputToAttribute, const char * inputCSV, const char * verificationCSV, double verificationEpsilon, bool lowercase, std::ostream & output);
}

#endif /* testrun_hpp */
