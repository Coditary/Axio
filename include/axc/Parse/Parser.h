#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "axc/AST/AST.h"
#include "axc/Lex/Token.h"

namespace axc {

class DiagnosticEngine;

namespace detail {
class TopLevelParser;
class DeclarationParser;
class StatementParser;
class ExpressionParser;
}  // namespace detail

/// @brief Recursive-descent parser building an AST from lexer tokens.
class Parser {
  public:
    /// @brief Create a parser for a token stream and diagnostic sink.
    Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics);

    /// @brief Parse a full translation unit, including package and declarations.
    TranslationUnit parseTranslationUnit();

  private:
    friend class detail::TopLevelParser;
    friend class detail::DeclarationParser;
    friend class detail::StatementParser;
    friend class detail::ExpressionParser;

    std::unique_ptr<Decl> parseTopLevelDecl();
    std::vector<std::unique_ptr<ImportDecl>> parseImportDecls();
    std::unique_ptr<GlobalVarDecl> parseGlobalDecl(bool mutableStorage);
    std::unique_ptr<FunctionDecl> parseFunctionDecl(bool isExtern, bool isMethod = false, std::string receiverType = {});
    std::unique_ptr<StructDecl> parseStructDecl();
    std::unique_ptr<EnumDecl> parseEnumDecl();
    std::unique_ptr<ClassDecl> parseClassDecl();

    std::unique_ptr<CompoundStmt> parseCompoundStmt();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseReturnStmt();
    std::unique_ptr<Stmt> parseDeferStmt();
    std::unique_ptr<Stmt> parseLetStmt();
    std::unique_ptr<Stmt> parseIfStmt();
    std::unique_ptr<Stmt> parseWhileStmt();
    std::unique_ptr<Stmt> parseForStmt();
    std::unique_ptr<Stmt> parseDoWhileStmt();
    std::unique_ptr<Stmt> parseSwitchStmt();
    std::unique_ptr<Stmt> parseBreakStmt();
    std::unique_ptr<Stmt> parseContinueStmt();
    std::unique_ptr<Stmt> parseExprStmt();
    LetBinding parseLetBinding();

    Type parseType();
    Parameter parseParameter();
    std::optional<Type> parseOptionalReturnType();

    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseComparisonChain();
    std::unique_ptr<Expr> parseBitwiseOr();
    std::unique_ptr<Expr> parseBitwiseXor();
    std::unique_ptr<Expr> parseBitwiseAnd();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> parseArrayLiteral();
    std::unique_ptr<Expr> parseBraceArrayLiteral();
    std::vector<std::unique_ptr<Expr>> parseArgumentList(TokenKind closing);

    bool isTypeStart(const Token& token) const;
    bool isSeparator(const Token& token) const;
    void skipSeparators();
    void consumeOptionalStatementTerminator();
    bool match(TokenKind kind);
    bool matchIdentifier(std::string_view lexeme);
    bool check(TokenKind kind) const;
    bool checkNext(TokenKind kind, std::size_t lookahead = 1) const;
    bool checkIdentifier(std::string_view lexeme) const;
    const Token& advance();
    const Token& current() const;
    const Token& previous() const;
    bool expect(TokenKind kind, const char* message);
    void synchronizeTopLevel();
    void synchronizeStatement();

    SourceRange combine(SourceRange lhs, SourceRange rhs) const;

    std::vector<Token> tokens_ {};
    DiagnosticEngine& diagnostics_;
    std::size_t index_ = 0;
    std::vector<std::unique_ptr<Decl>> pendingTopLevelDecls_ {};
};

}  // namespace axc
