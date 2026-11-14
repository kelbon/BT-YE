cmake_minimum_required(VERSION 3.21)
project(btye)

option(btye_ENABLE_TESTING "enables testing" OFF)
set(btye_SANITIZE "" CACHE STRING "value for -fsanitize (for CI), e.g. address,undefined or thread")

set(btye_TRACE_TASKS "DEBUG" CACHE STRING "trace btye::task: ON, OFF or DEBUG (only in Debug configuration)")
set_property(CACHE btye_TRACE_TASKS PROPERTY STRINGS ON OFF DEBUG)

# до зависимостей: санитайзер нужен и в их коде
if(btye_SANITIZE)
  add_compile_options(-fsanitize=${btye_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${btye_SANITIZE})
endif()

include(cmake/deps.cmake)

add_library(btyelib STATIC
    src/btye/app.cpp
    src/btye/io.cpp
    src/btye/interrupt.cpp
    src/btye/crash.cpp
    src/btye/sleep.cpp
    src/btye/client_pool.cpp
    src/btye/actor.cpp
    src/btye/offload_pool.cpp
    src/btye/utils/thread_tune.cpp
    src/btye/utils/log_exception.cpp
    src/btye/trace.cpp
    src/btye/task.cpp
)

target_include_directories(btyelib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(btye_TRACE_TASKS STREQUAL "DEBUG")
  target_compile_definitions(btyelib PUBLIC "BTYE_TRACE_TASKS=$<IF:$<CONFIG:Debug>,1,0>")
elseif(btye_TRACE_TASKS)
  target_compile_definitions(btyelib PUBLIC BTYE_TRACE_TASKS=1)
else()
  target_compile_definitions(btyelib PUBLIC BTYE_TRACE_TASKS=0)
endif()

target_link_libraries(btyelib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    hidilib
    svarg_cxx_supportlib
    Boost::stacktrace
    zallib
    metellib
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
