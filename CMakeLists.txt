cmake_minimum_required(VERSION 3.10)
set(VCPKG_OVERLAY_TRIPLETS ../custom_triplets)

project(pamplemousse)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package( Lua REQUIRED )

find_package( tinyxml2 REQUIRED )

include_directories(${LUA_INCLUDE_DIR} . common tinyxml2::tinyxml2)

add_library(libpamplemousse STATIC
    common/ast.cpp common/ast.hpp
    common/conversioncontext.cpp common/conversioncontext.hpp
    common/document.cpp common/document.hpp
    common/function.cpp common/function.hpp
    common/pmmldocumentdefs.cpp common/pmmldocumentdefs.hpp
    common/analyser.cpp common/analyser.hpp
    common/functiondispatch.hpp
    model/generalregressionmodel.cpp model/generalregressionmodel.hpp
    model/miningmodel.cpp model/miningmodel.hpp
    model/naivebayesmodel.cpp model/naivebayesmodel.hpp
    model/neuralnetworkmodel.cpp model/neuralnetworkmodel.hpp
    model/output.cpp model/output.hpp
    model/predicate.cpp model/predicate.hpp
    model/regressionmodel.cpp model/regressionmodel.hpp
    model/rulesetmodel.cpp model/rulesetmodel.hpp
    model/scorecardmodel.cpp model/scorecardmodel.hpp
    model/supportvectormachine.cpp model/supportvectormachine.hpp
    model/transformation.cpp model/transformation.hpp
    model/treemodel.cpp model/treemodel.hpp
    luaconverter/luaconverter-internal.hpp
    luaconverter/luaconverter.cpp luaconverter/luaconverter.hpp
    luaconverter/luaconverter-predicate.cpp
    luaconverter/luaconverter-procedural.cpp
    luaconverter/luaconverter-ternary.cpp
    luaconverter/luaoutputter.cpp luaconverter/luaoutputter.hpp
    luaconverter/optimiser.cpp luaconverter/optimiser.hpp)

target_link_libraries(libpamplemousse PUBLIC tinyxml2::tinyxml2)

if (NOT DEFINED skip_tests)
    add_subdirectory(CUTI-master ${CMAKE_BINARY_DIR}/cuti)

    cuti_creates_test_target(libpamplemousse_test libpamplemousse
        unit_tests/testutils.cpp
        unit_tests/testutils.hpp
        unit_tests/test_function.cpp
        unit_tests/test_miningmodel.cpp
        unit_tests/test_naivebayes.cpp
        unit_tests/test_predicate.cpp
        unit_tests/test_ruleset.cpp
        unit_tests/test_scorecard.cpp
        unit_tests/test_supportvectormachine.cpp
        unit_tests/test_transform.cpp
        unit_tests/test_tree.cpp)
    target_link_libraries(libpamplemousse_test PRIVATE ${LUA_LIBRARIES})
endif()

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 COMPONENTS Widgets)
if (Qt5_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    add_executable(pamplemousse
        app/pamplemousse.cpp
        app/pamplemousse_ui.cpp app/pamplemousse_ui.h
        app/outputtable.cpp app/outputtable.h
        app/mainwindow.ui
        app/testrun.cpp app/testrun.hpp
        app/modeloutput.cpp app/modeloutput.hpp
        app/basicexport.cpp app/basicexport.hpp
        app/outputslist.cpp app/outputslist.h
        app/resources.qrc)
    target_link_libraries(pamplemousse Qt5::Widgets)
    target_include_directories(pamplemousse PRIVATE app)
    target_compile_definitions(pamplemousse PRIVATE INCLUDE_UI)
else()
    add_executable(pamplemousse
        app/pamplemousse.cpp
        app/testrun.cpp app/testrun.hpp
        app/modeloutput.cpp app/modeloutput.hpp
        app/basicexport.cpp app/basicexport.hpp)
endif()
        
target_link_libraries(pamplemousse libpamplemousse)
target_link_libraries(pamplemousse ${LUA_LIBRARIES})

if(MSVC)
  target_compile_definitions(libpamplemousse PRIVATE NOMINMAX _USE_MATH_DEFINES)
  target_compile_definitions(pamplemousse PRIVATE NOMINMAX _USE_MATH_DEFINES)
  target_compile_definitions(libpamplemousse_test PRIVATE NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(libpamplemousse PRIVATE -Wshadow -Wall -Wextra -pedantic -Werror)
  target_compile_options(pamplemousse PRIVATE -Wshadow -Wall -Wextra -pedantic -Werror)
endif()

