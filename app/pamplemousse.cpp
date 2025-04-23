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

#include "modeloutput.hpp"
#include "basicexport.hpp"
#include "luaconverter/luaoutputter.hpp"
#include "testrun.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <stdlib.h>

#ifdef _WIN32

struct option
{
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#define no_argument        0
#define required_argument  1

// Copyright 2016 Microsoft
// Licensed under the Apache License, Version 2.0
// https://github.com/iotivity/iotivity/blob/master/resource/c_common/windows/src/getopt.c

static char* optarg = NULL;
static int optind = 1;

static int getopt(int argc, char *const argv[], const char *optstring)
{
    if ((optind >= argc) || (argv[optind][0] != '-') || (argv[optind][0] == 0))
    {
        return -1;
    }

    int opt = argv[optind][1];
    const char *p = strchr(optstring, opt);

    if (p == NULL)
    {
        return '?';
    }
    if (p[1] == ':')
    {
        optind++;
        if (optind >= argc)
        {
            return '?';
        }
        optarg = argv[optind];
        optind++;
    }
    return opt;
}
// End Copyright Microsoft

#else
#include <getopt.h>
#endif

#ifdef INCLUDE_UI
#include "pamplemousse_ui.h"
static int runUI(int argc, char *argv[], bool insensitive, std::vector<PMMLExporter::ModelOutput> && outputs, int inputFormat, int outputFormat);
#endif



static void printUsage(const char * programName, option* longopts)
{
    static constexpr size_t NUMBER_OF_MODES = 3;
    static constexpr const char * DESCRIPTIONS[] = {
        "Check model output given a CSV input",
        "Convert model to LUA",
        "Display this message.",
        "Convert all strings to lower case",
        "Write to a file (defaults to stdout)",
        "Define input",
        "Output to a custom attribute.",
        "CSV input file",
        "CSV input file",
        "Precision to verify output",
        "Use multiple parameters for inputs (default)",
        "Use table for inputs",
        "Use multiple parameters for outputs",
        "Use table for outputs",
        nullptr
    };
    
    printf("OVERVIEW:\tconverts PMML document to Lua\n");
    printf("Usage: %s <mode> <option> <option>... <filename.pmml>\n\nModes:\n", programName);
    
    for (size_t i = 0; longopts[i].name != nullptr; ++i)
    {
        if (i == NUMBER_OF_MODES)
        {
            printf("\nOptions:\n");
        }
        
        const option & opt = longopts[i];
        char singleOption[3] = "  ";
        if (opt.flag == nullptr)
        {
            singleOption[0] = '-';
            singleOption[1] = opt.val;
        }
#ifdef _WIN32
        else
        {
            // Windows currently only supports short options for now
            continue;
        }
#endif
        const char * argstring = opt.has_arg == no_argument ? "     " : "<arg>";
        
        assert(DESCRIPTIONS[i]);
        printf("  %s %s    ", singleOption, argstring);
#ifndef _WIN32
        // Do not show long names for windows, because they are not supported
        const char * name = opt.name;
        constexpr char columnBlank[] = "                   ";
        constexpr size_t columnBlankWidth = sizeof(columnBlank) - 1;
        const size_t namelen = strlen(name);
        assert(namelen <= columnBlankWidth);
        printf("--%s %s%.*s", name, argstring, (int)(columnBlankWidth - namelen), columnBlank);
#endif
        printf("%s\n",  DESCRIPTIONS[i]);
    }
    
#ifdef INCLUDE_UI
    printf("\nStarting without a mode specified will open the interactive GUI\n");
#endif
    
    printf("\nFor any output, you may reference any target/predicted or output value from the model. Furthermore, you may access any neuron's activation value through \"neuron:<id>\"\n");
    printf("You may also put expression using +, -, * and / after an model output, but not before.\n");
    printf("E.g. \"--prediction probability=predicted_value*100+3\" is acceptable, but \"--prediction probability=100*predicted_value+3\" is not\n");
}

int main(int argc, char *argv[])
{
    int isTest = 0;
    int isConvert = 0;
    
    int inputFormat = int(PMMLExporter::Format::AS_MULTI_ARG);
    int outputFormat = int(PMMLExporter::Format::AS_MULTI_ARG);
    
    struct option longopts[] = {
        { "test",      no_argument,      NULL,         'T' },
        { "convert",   no_argument,      NULL,         'C' },
        { "insensitive", no_argument,    NULL,         'i' },
        { "data",      required_argument,NULL,         'd' },
        { "verify",    required_argument,NULL,         'v' },
        { "output",    required_argument,NULL,         'o' },
        { "feature",   required_argument,NULL,         'f' },
        { "prediction",required_argument,NULL,         'p' },
        { "help",      no_argument,      NULL,         'h' },
        { "epsilon",   required_argument,NULL,         'e' },
        { "input_multi",no_argument,     &inputFormat, int(PMMLExporter::Format::AS_MULTI_ARG) },
        { "input_table",no_argument,     &inputFormat, int(PMMLExporter::Format::AS_TABLE) },
        { "output_multi",no_argument,    &outputFormat,int(PMMLExporter::Format::AS_MULTI_ARG) },
        { "output_table",no_argument,    &outputFormat,int(PMMLExporter::Format::AS_TABLE) },
        { NULL,        0,                NULL,          0 }
    };
    
    static constexpr char OPTSTRING[] = "id:v:o:f:p:he:TC";

    const char * dataFile   = nullptr;
    const char * verifyFile = nullptr;
    const char * outputFile = nullptr;
    bool insensitive = false;
    double epsilon = 0.0001;
    std::vector<PMMLExporter::ModelOutput> inputs;
    std::vector<PMMLExporter::ModelOutput> outputs;
    int c;
    
#ifndef _WIN32
    while ((c = getopt_long(argc, argv, OPTSTRING, longopts, NULL)) != -1)
#else
    while ((c = getopt(argc, argv, OPTSTRING)) != -1)
#endif
    {
        if (c == 'T')
        {
            isTest = 1;
        }
        else if (c == 'C')
        {
            isConvert = 1;
        }
        else if (c == 'i')
        {
            insensitive = true;
        }
        else if (c == 'd')
        {
            dataFile = optarg;
        }
        else if (c == 'v')
        {
            verifyFile = optarg;
        }
        else if (c == 'o')
        {
            outputFile = optarg;
        }
        else if (c == 'f')
        {
            inputs.emplace_back(optarg, optarg);
        }
        else if (c == 'p')
        {
            std::string customOutput(optarg);
            size_t separator = customOutput.find('=');
            if (separator != std::string::npos)
            {
                outputs.emplace_back(customOutput.substr(separator + 1), customOutput.substr(0, separator));
            }
            else
            {
                outputs.emplace_back(customOutput, customOutput);
            }
        }
        else if (c == 'h')
        {
            printUsage(argv[0], longopts);
            return 0;
        }
        else if (c == 'e')
        {
            char * endOfString;
            epsilon = strtod(optarg, &endOfString);
            if (*endOfString != '\0' && !isspace(*endOfString))
            {
                fprintf( stderr, "%s: Epsilon should be a number (found '%c')\n", argv[0], *endOfString);
                return -1;
            }
        }
        else if (c != 0)
        {
            fprintf( stderr, "%s: Unrecognised option: %s\n", argv[0], argv[optind]);
            printUsage(argv[0], longopts);
            return -1;
        }
    }

#ifdef INCLUDE_UI
    if (isTest == 0 && isConvert == 0)
    {
        return runUI(argc, argv, insensitive, std::move(outputs), inputFormat, outputFormat);
    }
#endif

    if (isTest + isConvert != 1)
    {
        fprintf( stderr, "%s: Requires exactly one of the following arguments: -T/--test, -C/--convert\n", argv[0]);
        printUsage(argv[0], longopts);
        return -1;
    }

    if (optind >= argc)
    {
        std::cerr << argv[0] << ": No input files specified\n";
        return -1;
    }

    const char * sourceFile = argv[optind];
    std::ofstream outFileStream;
    if (outputFile)
    {
        outFileStream.open(outputFile);
        if (!outFileStream.is_open())
        {
            std::cerr << argv[0] << ": Cannot open " << outputFile << " for writing\n";
            return -1;
        }
    }

    if (isTest)
    {
        if (dataFile == nullptr)
        {
            std::cerr << argv[0] << ": No data file specified (required for test mode)\n";
            return -1;
        }
        
        if (!PMMLExporter::doTestRun(sourceFile, outputs, dataFile, verifyFile, epsilon, insensitive, outputFile ? outFileStream : std::cout))
        {
            return -1;
        }
    }
    else if (isConvert)
    {
        LuaOutputter output(outputFile ? outFileStream : std::cout, insensitive ? LuaOutputter::OPTION_LOWERCASE : 0);

        if (!PMMLExporter::createScript(sourceFile, output, inputs, outputs, PMMLExporter::Format(inputFormat), PMMLExporter::Format(outputFormat)))
        {
            return -1;
        }
    }
    
    if (outputFile)
    {
        outFileStream.close();
    }

    return 0;
}


#ifdef INCLUDE_UI
static int runUI(int argc, char *argv[], bool insensitive, std::vector<PMMLExporter::ModelOutput> && outputs, int inputFormat, int outputFormat)
{
    QApplication app(argc, argv);
    PamplemousseUI mainWindow;

    if (optind < argc)
    {
        const char * sourceFile = argv[optind];
        tinyxml2::XMLDocument doc(sourceFile);
        if (doc.LoadFile(sourceFile) != tinyxml2::XML_SUCCESS)
        {
            fprintf(stderr, "Failed to load file \"%s\": %s\n", sourceFile, doc.ErrorStr());
            return -1;
        }

        AstBuilder builder;
        if (!PMMLDocument::convertPMML( builder, doc.RootElement() ))
        {
            return -1;
        }

        for (auto & output : outputs)
        {
            if (!output.bindToModel(builder.context()))
            {
                return -1;
            }
        }

        mainWindow.importLoadedModel(std::move(builder));
        mainWindow.importOutputs(std::move(outputs));
    }

    if (insensitive)
    {
        mainWindow.setInsensitive();
    }

    if (inputFormat == int(PMMLExporter::Format::AS_TABLE))
    {
        mainWindow.setTableIn();
    }

    if (outputFormat == int(PMMLExporter::Format::AS_TABLE))
    {
        mainWindow.setTableOut();
    }

    mainWindow.show();

    return app.exec();
}
#endif
