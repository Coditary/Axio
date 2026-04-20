#include "axc/AST/ASTPrinter.h"

namespace axc {

namespace {

const char* unaryOpName(UnaryOp op) {
    switch (op) {
        case UnaryOp::Negate:
            return "Negate";
        case UnaryOp::AddressOf:
            return "AddressOf";
        case UnaryOp::Dereference:
            return "Dereference";
        case UnaryOp::LogicalNot:
            return "LogicalNot";
        case UnaryOp::BitwiseNot:
            return "BitwiseNot";
        case UnaryOp::PreIncrement:
            return "PreIncrement";
        case UnaryOp::PreDecrement:
            return "PreDecrement";
        case UnaryOp::PostIncrement:
            return "PostIncrement";
        case UnaryOp::PostDecrement:
            return "PostDecrement";
    }

    return "Unknown";
}

const char* binaryOpName(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:
            return "Add";
        case BinaryOp::Sub:
            return "Sub";
        case BinaryOp::Mul:
            return "Mul";
        case BinaryOp::Div:
            return "Div";
        case BinaryOp::Mod:
            return "Mod";
        case BinaryOp::BitAnd:
            return "BitAnd";
        case BinaryOp::BitOr:
            return "BitOr";
        case BinaryOp::BitXor:
            return "BitXor";
        case BinaryOp::ShiftLeft:
            return "ShiftLeft";
        case BinaryOp::ShiftRight:
            return "ShiftRight";
        case BinaryOp::Equal:
            return "Equal";
        case BinaryOp::NotEqual:
            return "NotEqual";
        case BinaryOp::Less:
            return "Less";
        case BinaryOp::LessEqual:
            return "LessEqual";
        case BinaryOp::Greater:
            return "Greater";
        case BinaryOp::GreaterEqual:
            return "GreaterEqual";
        case BinaryOp::LogicalAnd:
            return "LogicalAnd";
        case BinaryOp::LogicalOr:
            return "LogicalOr";
        case BinaryOp::Assign:
            return "Assign";
    }

    return "Unknown";
}

}  // namespace

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
            if (ret.value) {
                printExpr(*ret.value, level + 1);
            }
            break;
        }
        case StmtKind::Defer: {
            out_ << "Defer\n";
            const auto& deferStmt = static_cast<const DeferStmt&>(stmt);
            if (deferStmt.call) {
                printExpr(*deferStmt.call, level + 1);
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
        case StmtKind::While: {
            out_ << "While\n";
            const auto& whileStmt = static_cast<const WhileStmt&>(stmt);
            printExpr(*whileStmt.condition, level + 1);
            printStmt(*whileStmt.body, level + 1);
            break;
        }
        case StmtKind::For: {
            out_ << "For\n";
            const auto& forStmt = static_cast<const ForStmt&>(stmt);
            if (forStmt.initializer) {
                printStmt(*forStmt.initializer, level + 1);
            }
            if (forStmt.condition) {
                printExpr(*forStmt.condition, level + 1);
            }
            if (forStmt.step) {
                printExpr(*forStmt.step, level + 1);
            }
            printStmt(*forStmt.body, level + 1);
            break;
        }
        case StmtKind::DoWhile: {
            out_ << "DoWhile\n";
            const auto& doWhileStmt = static_cast<const DoWhileStmt&>(stmt);
            printStmt(*doWhileStmt.body, level + 1);
            printExpr(*doWhileStmt.condition, level + 1);
            break;
        }
        case StmtKind::Switch: {
            out_ << "Switch\n";
            const auto& switchStmt = static_cast<const SwitchStmt&>(stmt);
            printExpr(*switchStmt.condition, level + 1);
            for (const auto& switchCase : switchStmt.cases) {
                indent(level + 1);
                out_ << (switchCase.isDefault ? "Default\n" : "Case\n");
                for (const auto& pattern : switchCase.patterns) {
                    printExpr(*pattern.value, level + 2);
                }
                if (switchCase.body) {
                    printStmt(*switchCase.body, level + 2);
                }
            }
            break;
        }
        case StmtKind::Break:
            out_ << "Break\n";
            break;
        case StmtKind::Continue:
            out_ << "Continue\n";
            break;
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
        case ExprKind::DeclRef:
            out_ << "Ref " << static_cast<const DeclRefExpr&>(expr).name << '\n';
            break;
        case ExprKind::Unary:
            out_ << "Unary " << unaryOpName(static_cast<const UnaryExpr&>(expr).op) << '\n';
            printExpr(*static_cast<const UnaryExpr&>(expr).operand, level + 1);
            break;
        case ExprKind::Binary:
            out_ << "Binary " << binaryOpName(static_cast<const BinaryExpr&>(expr).op) << '\n';
            printExpr(*static_cast<const BinaryExpr&>(expr).lhs, level + 1);
            printExpr(*static_cast<const BinaryExpr&>(expr).rhs, level + 1);
            break;
        case ExprKind::Call:
            out_ << "Call\n";
            printExpr(*static_cast<const CallExpr&>(expr).callee, level + 1);
            for (const auto& argument : static_cast<const CallExpr&>(expr).arguments) {
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
                case InitKind::ArrayLiteral:
                    out_ << " array";
                    break;
            }
            out_ << '\n';
            for (const auto& value : static_cast<const InitializerExpr&>(expr).values) {
                printExpr(*value, level + 1);
            }
            break;
    }
}

}  // namespace axc
