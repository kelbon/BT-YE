cmake_minimum_required(VERSION 3.21)
project(btye)

option(btye_ENABLE_TESTING "enables testing" OFF)
option(btye_ENABLE_ASAN "build ASAN (for CI)" OFF)
option(btye_ENABLE_UBSAN "build UBSAN (for CI)" OFF)

include(cmake/deps.cmake)

add_library(btyelib STATIC
    src/app.cpp
    src/io.cpp
)

target_include_directories(btyelib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(btyelib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    hidilib
    zallib
)

set_target_properties(btyelib PROPERTIES
	LINKER_LANGUAGE CXX
	CXX_STANDARD 23
	CMAKE_CXX_STANDARD_REQUIRED ON
)

if(btye_ENABLE_TESTING)
	include(CTest)
	add_subdirectory(test)
endif()

# generates launch.json, places compile_commands.json correctly for clangd
ccozy_on_configuration_end_vscode()
