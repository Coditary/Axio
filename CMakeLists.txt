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
    src/App/Main.cpp
    src/Driver/Pipeline/Compiler.cpp
    src/Driver/Pipeline/CompilerWorkflow.cpp
    src/Driver/Module/ModuleLoader.cpp
    src/Driver/Module/ModuleFileParser.cpp
    src/Driver/Module/ModuleImportResolver.cpp
    src/Driver/Module/ModuleInterface.cpp
    src/Driver/Module/ModuleQualifier.cpp
    src/Support/InlineLlvm.cpp
    src/Support/Source/SourceManager.cpp
    src/Support/Diagnostics/DiagnosticEngine.cpp
    src/AST/Print/ASTPrinter.cpp
    src/AST/Print/ASTPrinterDecl.cpp
    src/AST/Print/ASTPrinterStmtExpr.cpp
    src/Parse/Core/Parser.cpp
    src/Parse/Core/ParserSupport.cpp
    src/Parse/Decl/ParserDecl.cpp
    src/Parse/Stmt/ParserStmt.cpp
    src/Parse/Expr/Core/ParserEntry.cpp
    src/Parse/Expr/Core/ParserAssignment.cpp
    src/Parse/Expr/Core/ParserPipe.cpp
    src/Parse/Expr/Logical/ParserLogical.cpp
    src/Parse/Expr/Logical/ParserComparison.cpp
    src/Parse/Expr/Arithmetic/ParserBitwise.cpp
    src/Parse/Expr/Arithmetic/ParserShift.cpp
    src/Parse/Expr/Arithmetic/ParserAdditive.cpp
    src/Parse/Expr/Arithmetic/ParserMultiplicative.cpp
    src/Parse/Expr/Primary/ParserUnary.cpp
    src/Parse/Expr/Primary/ParserPostfix.cpp
    src/Parse/Expr/Primary/ParserPrimary.cpp
    src/Sema/Core/Sema.cpp
    src/Sema/Core/SemaAnalysis.cpp
    src/Sema/Decl/SemaDecl.cpp
    src/Sema/Expr/SemaExpr.cpp
    src/Meta/Core/MetaPipeline.cpp
    src/Meta/Core/MetaWorkflow.cpp
    src/Meta/Annotations/MetaPipelineAnnotations.cpp
    src/Meta/Embedding/MetaPipelineEmbed.cpp
    src/Codegen/Core/LLVMEmitter.cpp
    src/Codegen/Core/LLVMEmitterModule.cpp
    src/Codegen/Core/ModuleEmissionWorkflow.cpp
    src/Codegen/Decl/LLVMEmitterDecl.cpp
    src/Codegen/Expr/LLVMEmitterExpr.cpp
    src/Codegen/Stmt/LLVMEmitterStmt.cpp
    src/Lex/Support/TokenNames.cpp
    src/Lex/Support/LexerText.cpp
    src/Lex/Core/LexerCore.cpp
    src/Runtime/Core/AxioRuntime.cpp
)

list(REMOVE_ITEM AXC_CORE_SOURCES src/App/Main.cpp)

add_library(axc_core ${AXC_CORE_SOURCES})

add_executable(axc src/App/Main.cpp)

if(AXC_USE_RE2C AND RE2C_EXECUTABLE)
    set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(GENERATED_LEXER ${GENERATED_DIR}/Lexer.cpp)

    add_custom_command(
        OUTPUT ${GENERATED_LEXER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
        COMMAND ${RE2C_EXECUTABLE} -W --no-generation-date -o ${GENERATED_LEXER} ${CMAKE_CURRENT_SOURCE_DIR}/src/Lex/Grammar/Lexer.re
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Lex/Grammar/Lexer.re
        COMMENT "Generating lexer with re2c"
        VERBATIM
    )

    target_sources(axc_core PRIVATE ${GENERATED_LEXER})
    target_compile_definitions(axc_core PRIVATE AXC_USING_RE2C=1)
else()
    if(AXC_USE_RE2C AND NOT RE2C_EXECUTABLE)
        message(WARNING "re2c was not found. Falling back to the checked-in lexer implementation.")
    endif()
    target_sources(axc_core PRIVATE src/Lex/Core/Lexer.cpp)
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
    Linker
    native
    nativecodegen
    target
    mc
    passes
)

target_link_libraries(axc_core PUBLIC ${AXC_LLVM_LIBS})
target_link_libraries(axc PRIVATE axc_core)

if(BUILD_TESTING)
    file(GLOB_RECURSE AXC_UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
    add_executable(axc_unit_tests
        ${AXC_UNIT_TEST_SOURCES}
    )
    target_link_libraries(axc_unit_tests PRIVATE axc_core)
    target_include_directories(axc_unit_tests PRIVATE include tests/unit ${LLVM_INCLUDE_DIRS})
    add_test(NAME axc_unit_tests COMMAND axc_unit_tests)
endif()

install(TARGETS axc RUNTIME DESTINATION bin)
