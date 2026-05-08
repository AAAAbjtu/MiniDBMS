cmake_minimum_required(VERSION 3.15)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows: set UTF-8 as the source/execution charset (equivalent to /utf-8 in MSVC)
if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

# ---- Dependencies (header-only) ----
include(FetchContent)

# cpp-httplib: lightweight HTTP server
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.14.3
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json: JSON for Modern C++
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(json)
# ---- End Dependencies ----

# Console application
add_executable(MiniDB
    main.cpp
    MiniDB.cpp
    Schema.cpp
    Table.cpp
    Constraints.cpp
    FileManager.cpp
    Parser.cpp
    QueryEval.cpp
    Executor.cpp
    ApiServer.cpp
)

# Header-only: INTERFACE include paths for httplib / nlohmann_json
target_link_libraries(MiniDB PRIVATE httplib::httplib nlohmann_json::nlohmann_json)

# Headers for IDE integration
target_sources(MiniDB PRIVATE
    MiniDB.h
    Ast.h
    Schema.h
    Record.h
    Table.h
    FileManager.h
    Parser.h
    Executor.h
    QueryEval.h
)

# MinGW: link runtime libraries statically for a self-contained exe
if(MINGW)
    target_link_options(MiniDB PRIVATE -static)
endif()

target_compile_definitions(MiniDB PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    _CONSOLE
    WEB_ROOT="${CMAKE_SOURCE_DIR}/web"
)

# Use folders in IDE
set_property(TARGET MiniDB PROPERTY FOLDER "MiniDB")
