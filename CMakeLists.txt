cmake_minimum_required(VERSION 3.5)
project(pamplemousse)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(CUTI-master ${CMAKE_BINARY_DIR}/cuti)

find_package( Lua REQUIRED )
if (NOT DEFINED TinyXML2_DIR)
  find_package( TinyXML2 REQUIRED )
endif()

include_directories(${LUA_INCLUDE_DIR} /usr/local/include/ . common)
link_directories(/usr/local/lib/ )

add_library(libpamplemousse STATIC
    common/ast.cpp common/ast.hpp
    common/conversioncontext.cpp common/conversioncontext.hpp
    common/document.cpp common/document.hpp
    common/function.cpp common/function.hpp
    common/pmmldocumentdefs.cpp common/pmmldocumentdefs.hpp
    common/analyser.cpp common/analyser.hpp
    common/functiondispatch.hpp
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

target_link_libraries(libpamplemousse PUBLIC tinyxml2)
target_compile_options(libpamplemousse PRIVATE -Wextra -Werror)

add_executable(pamplemousse
    app/pamplemousse.cpp
    app/testrun.cpp app/testrun.hpp
    app/modeloutput.cpp app/modeloutput.hpp
    app/basicexport.cpp app/basicexport.hpp)
target_link_libraries(pamplemousse libpamplemousse)
target_link_libraries(pamplemousse ${LUA_LIBRARIES})
target_compile_options(pamplemousse PRIVATE -Wextra -Werror)

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


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_executable(pamplemousse_ui
    app/pamplemousse_ui.cpp
    app/outputtable.cpp app/outputtable.h
    app/mainwindow.ui
    app/testrun.cpp app/testrun.hpp
    app/modeloutput.cpp app/modeloutput.hpp
    app/basicexport.cpp app/basicexport.hpp
    app/outputslist.cpp app/outputslist.h
    app/resources.qrc)
target_link_libraries(pamplemousse_ui Qt5::Widgets)
target_link_libraries(pamplemousse_ui libpamplemousse)
target_link_libraries(pamplemousse_ui ${LUA_LIBRARIES})
target_include_directories(pamplemousse_ui PRIVATE app)
target_compile_options(pamplemousse_ui PRIVATE -Wextra -Werror)
