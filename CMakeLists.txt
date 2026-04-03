cmake_minimum_required(VERSION 3.28)

project(AxioCompiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AXC_ENABLE_COVERAGE "Enable coverage flags" OFF)
option(AXC_USE_RE2C "Use re2c-generated lexer" OFF)

include(CTest)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_program(RE2C_EXECUTABLE re2c)

set(AXC_CORE_SOURCES
    src/main.cpp
    src/Driver/Compiler.cpp
    src/Driver/ModuleLoader.cpp
    src/Support/SourceManager.cpp
    src/Support/Diagnostic.cpp
    src/AST/ASTPrinter.cpp
    src/Parse/Parser.cpp
    src/Parse/ParserSupport.cpp
    src/Parse/ParserDecl.cpp
    src/Parse/ParserStmt.cpp
    src/Parse/ParserExpr.cpp
    src/Sema/Sema.cpp
    src/Sema/SemaAnalysis.cpp
    src/Sema/SemaDecl.cpp
    src/Sema/SemaExpr.cpp
    src/Meta/MetaPipeline.cpp
    src/Codegen/LLVMEmitter.cpp
    src/Codegen/LLVMEmitterModule.cpp
    src/Codegen/LLVMEmitterDecl.cpp
    src/Codegen/LLVMEmitterExpr.cpp
    src/Codegen/LLVMEmitterStmt.cpp
    src/Lex/LexerNames.cpp
    src/Lex/LexerSupport.cpp
    src/Lex/LexerCore.cpp
    src/AST/ASTPrinterDecl.cpp
    src/AST/ASTPrinterStmtExpr.cpp
    src/Meta/MetaPipelineAnnotations.cpp
    src/Meta/MetaPipelineEmbed.cpp
)

list(REMOVE_ITEM AXC_CORE_SOURCES src/main.cpp)

add_library(axc_core ${AXC_CORE_SOURCES})

add_executable(axc src/main.cpp)

if(AXC_USE_RE2C AND RE2C_EXECUTABLE)
    set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(GENERATED_LEXER ${GENERATED_DIR}/Lexer.cpp)

    add_custom_command(
        OUTPUT ${GENERATED_LEXER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND ${RE2C_EXECUTABLE} -W --no-generation-date -o ${GENERATED_LEXER} ${CMAKE_CURRENT_SOURCE_DIR}/src/Lex/Lexer.re
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Lex/Lexer.re
        COMMENT "Generating lexer with re2c"
        VERBATIM
    )

    target_sources(axc_core PRIVATE ${GENERATED_LEXER})
    target_compile_definitions(axc_core PRIVATE AXC_USING_RE2C=1)
else()
    if(AXC_USE_RE2C AND NOT RE2C_EXECUTABLE)
        message(WARNING "re2c was not found. Falling back to the checked-in lexer implementation.")
    endif()
    target_sources(axc_core PRIVATE src/Lex/Lexer.cpp)
endif()

target_include_directories(axc_core PUBLIC include ${LLVM_INCLUDE_DIRS})
target_compile_definitions(axc_core PRIVATE ${LLVM_DEFINITIONS})

if(AXC_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(axc_core PRIVATE --coverage -O0 -g)
    target_link_options(axc_core PRIVATE --coverage)
endif()

llvm_map_components_to_libnames(AXC_LLVM_LIBS
    Core
    Support
    native
    nativecodegen
    target
    mc
    passes
)

target_link_libraries(axc_core PUBLIC ${AXC_LLVM_LIBS})
target_link_libraries(axc PRIVATE axc_core)

if(BUILD_TESTING)
    add_executable(axc_unit_tests
        tests/unit/CompilerUnitTests.cpp
    )
    target_link_libraries(axc_unit_tests PRIVATE axc_core)
    target_include_directories(axc_unit_tests PRIVATE include ${LLVM_INCLUDE_DIRS})
    add_test(NAME axc_unit_tests COMMAND axc_unit_tests)
endif()

install(TARGETS axc RUNTIME DESTINATION bin)
