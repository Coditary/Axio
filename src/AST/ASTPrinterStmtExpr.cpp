#include "axc/AST/ASTPrinter.h"

namespace axc {

void ASTPrinter::printStmt(const Stmt& stmt, int level) const {
    indent(level);
    switch (stmt.kind) {
        case StmtKind::Compound: {
            out_ << "Block\n";
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            for (const auto& child : block.statements) {
                printStmt(*child, level + 1);
            }
            break;
        }
        case StmtKind::Return: {
            out_ << "Return\n";
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            for (const auto& value : ret.values) {
                printExpr(*value, level + 1);
            }
            break;
        }
        case StmtKind::Expr: {
            out_ << "ExprStmt\n";
            printExpr(*static_cast<const ExprStmt&>(stmt).expression, level + 1);
            break;
        }
        case StmtKind::Let: {
            const auto& letStmt = static_cast<const LetStmt&>(stmt);
            out_ << "Let";
            for (std::size_t i = 0; i < letStmt.bindings.size(); ++i) {
                const auto& binding = letStmt.bindings[i];
                out_ << (i == 0 ? " " : ", ") << binding.name;
                if (!binding.explicitType.name.empty()) {
                    out_ << ':';
                    printType(binding.explicitType);
                }
            }
            out_ << '\n';
            if (letStmt.initializer) {
                printExpr(*letStmt.initializer, level + 1);
            }
            break;
        }
        case StmtKind::If: {
            out_ << "If\n";
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            printExpr(*ifStmt.condition, level + 1);
            printStmt(*ifStmt.thenBlock, level + 1);
            if (ifStmt.elseBranch) {
                printStmt(*ifStmt.elseBranch, level + 1);
            }
            break;
        }
    }
}

void ASTPrinter::printExpr(const Expr& expr, int level) const {
    indent(level);
    switch (expr.kind) {
        case ExprKind::IntegerLiteral:
            out_ << "Int " << static_cast<const IntegerLiteralExpr&>(expr).value << '\n';
            break;
        case ExprKind::FloatLiteral:
            out_ << "Float " << static_cast<const FloatLiteralExpr&>(expr).value << '\n';
            break;
        case ExprKind::BoolLiteral:
            out_ << "Bool " << (static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false") << '\n';
            break;
        case ExprKind::CharLiteral:
            out_ << "Char\n";
            break;
        case ExprKind::StringLiteral:
            out_ << "String\n";
            break;
        case ExprKind::NullLiteral:
            out_ << "Null\n";
            break;
        case ExprKind::DeclRef:
            out_ << "Ref " << static_cast<const DeclRefExpr&>(expr).name << '\n';
            break;
        case ExprKind::Unary:
            out_ << "Unary\n";
            printExpr(*static_cast<const UnaryExpr&>(expr).operand, level + 1);
            break;
        case ExprKind::Binary:
            out_ << "Binary\n";
            printExpr(*static_cast<const BinaryExpr&>(expr).lhs, level + 1);
            printExpr(*static_cast<const BinaryExpr&>(expr).rhs, level + 1);
            break;
        case ExprKind::Range:
            out_ << "Range\n";
            printExpr(*static_cast<const RangeExpr&>(expr).start, level + 1);
            printExpr(*static_cast<const RangeExpr&>(expr).end, level + 1);
            break;
        case ExprKind::Call:
            out_ << "Call\n";
            printExpr(*static_cast<const CallExpr&>(expr).callee, level + 1);
            for (const auto& argument : static_cast<const CallExpr&>(expr).compileArguments) {
                printExpr(*argument, level + 1);
            }
            for (const auto& argument : static_cast<const CallExpr&>(expr).runtimeArguments) {
                printExpr(*argument, level + 1);
            }
            break;
        case ExprKind::Member:
            out_ << "Member " << static_cast<const MemberExpr&>(expr).member << '\n';
            printExpr(*static_cast<const MemberExpr&>(expr).base, level + 1);
            break;
        case ExprKind::Initializer:
            out_ << "Init " << static_cast<const InitializerExpr&>(expr).typeName;
            switch (static_cast<const InitializerExpr&>(expr).initKind) {
                case InitKind::Value:
                    break;
                case InitKind::Arc:
                    out_ << " arc";
                    break;
                case InitKind::Weak:
                    out_ << " weak";
                    break;
                case InitKind::Unique:
                    out_ << " unique";
                    break;
                case InitKind::ArrayLiteral:
                    out_ << " array";
                    break;
            }
            out_ << '\n';
            for (const auto& value : static_cast<const InitializerExpr&>(expr).values) {
                printExpr(*value, level + 1);
            }
            break;
        case ExprKind::CompileCall:
            out_ << "CompileCall " << static_cast<const CompileCallExpr&>(expr).callee << '\n';
            break;
        case ExprKind::Dialect:
            out_ << "Dialect " << static_cast<const DialectExpr&>(expr).dialectName << '\n';
            break;
    }
}

}  // namespace axc
