cmake_minimum_required(VERSION 3.15)
project(MiniDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows: set UTF-8 as the source/execution charset (equivalent to /utf-8 in MSVC)
if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

# Console application
add_executable(MiniDB
    main.cpp
    MiniDB.cpp
    Schema.cpp
    Table.cpp
    FileManager.cpp
    Parser.cpp
    Executor.cpp
)

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
)

# MinGW: link runtime libraries statically for a self-contained exe
if(MINGW)
    target_link_options(MiniDB PRIVATE -static)
endif()

target_compile_definitions(MiniDB PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    _CONSOLE
)

# Use folders in IDE
set_property(TARGET MiniDB PROPERTY FOLDER "MiniDB")
