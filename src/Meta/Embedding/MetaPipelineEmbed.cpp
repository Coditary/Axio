#include "axc/Meta/MetaPipeline.h"

#include <functional>

#include "axc/Support/Diagnostic.h"

namespace axc {

namespace {

template <typename Fn>
void visitExpr(const Expr* expr, Fn&& fn) {
    if (expr == nullptr) {
        return;
    }

    fn(expr);

    switch (expr->kind) {
        case ExprKind::Unary: {
            const auto* unary = static_cast<const UnaryExpr*>(expr);
            visitExpr(unary->operand.get(), fn);
            break;
        }
        case ExprKind::Binary: {
            const auto* binary = static_cast<const BinaryExpr*>(expr);
            visitExpr(binary->lhs.get(), fn);
            visitExpr(binary->rhs.get(), fn);
            break;
        }
        case ExprKind::Cast: {
            const auto* cast = static_cast<const CastExpr*>(expr);
            visitExpr(cast->value.get(), fn);
            break;
        }
        case ExprKind::Range: {
            const auto* range = static_cast<const RangeExpr*>(expr);
            visitExpr(range->start.get(), fn);
            visitExpr(range->end.get(), fn);
            break;
        }
        case ExprKind::Call: {
            const auto* call = static_cast<const CallExpr*>(expr);
            visitExpr(call->callee.get(), fn);
            for (const auto& arg : call->compileArguments) {
                visitExpr(arg.get(), fn);
            }
            for (const auto& arg : call->runtimeArguments) {
                visitExpr(arg.get(), fn);
            }
            break;
        }
        case ExprKind::Member: {
            const auto* member = static_cast<const MemberExpr*>(expr);
            visitExpr(member->base.get(), fn);
            break;
        }
        case ExprKind::Initializer: {
            const auto* init = static_cast<const InitializerExpr*>(expr);
            for (const auto& value : init->values) {
                visitExpr(value.get(), fn);
            }
            break;
        }
        case ExprKind::CompileCall: {
            const auto* call = static_cast<const CompileCallExpr*>(expr);
            for (const auto& arg : call->arguments) {
                visitExpr(arg.get(), fn);
            }
            break;
        }
        case ExprKind::IntegerLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::CharLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::NullLiteral:
        case ExprKind::DeclRef:
        case ExprKind::Dialect:
            break;
    }
}

void visitStmtExprs(const Stmt* stmt, const std::function<void(const Expr*)>& fn) {
    if (stmt == nullptr) {
        return;
    }

    switch (stmt->kind) {
        case StmtKind::Compound: {
            const auto* block = static_cast<const CompoundStmt*>(stmt);
            for (const auto& child : block->statements) {
                visitStmtExprs(child.get(), fn);
            }
            break;
        }
        case StmtKind::Return: {
            const auto* ret = static_cast<const ReturnStmt*>(stmt);
            for (const auto& value : ret->values) {
                visitExpr(value.get(), fn);
            }
            break;
        }
        case StmtKind::Defer: {
            const auto* deferStmt = static_cast<const DeferStmt*>(stmt);
            visitExpr(deferStmt->call.get(), fn);
            break;
        }
        case StmtKind::Expr:
            visitExpr(static_cast<const ExprStmt*>(stmt)->expression.get(), fn);
            break;
        case StmtKind::Let:
            visitExpr(static_cast<const LetStmt*>(stmt)->initializer.get(), fn);
            break;
        case StmtKind::If: {
            const auto* ifStmt = static_cast<const IfStmt*>(stmt);
            visitExpr(ifStmt->condition.get(), fn);
            visitStmtExprs(ifStmt->thenBlock.get(), fn);
            visitStmtExprs(ifStmt->elseBranch.get(), fn);
            break;
        }
        case StmtKind::While: {
            const auto* whileStmt = static_cast<const WhileStmt*>(stmt);
            visitExpr(whileStmt->condition.get(), fn);
            visitStmtExprs(whileStmt->body.get(), fn);
            break;
        }
        case StmtKind::For: {
            const auto* forStmt = static_cast<const ForStmt*>(stmt);
            visitStmtExprs(forStmt->initializer.get(), fn);
            visitExpr(forStmt->condition.get(), fn);
            visitExpr(forStmt->step.get(), fn);
            visitStmtExprs(forStmt->body.get(), fn);
            break;
        }
        case StmtKind::Foreach: {
            const auto* foreachStmt = static_cast<const ForeachStmt*>(stmt);
            visitExpr(foreachStmt->iterable.get(), fn);
            visitStmtExprs(foreachStmt->body.get(), fn);
            break;
        }
        case StmtKind::DoWhile: {
            const auto* doWhileStmt = static_cast<const DoWhileStmt*>(stmt);
            visitStmtExprs(doWhileStmt->body.get(), fn);
            visitExpr(doWhileStmt->condition.get(), fn);
            break;
        }
        case StmtKind::Switch: {
            const auto* switchStmt = static_cast<const SwitchStmt*>(stmt);
            visitExpr(switchStmt->condition.get(), fn);
            for (const auto& switchCase : switchStmt->cases) {
                for (const auto& pattern : switchCase.patterns) {
                    visitExpr(pattern.value.get(), fn);
                }
                visitStmtExprs(switchCase.body.get(), fn);
            }
            break;
        }
        case StmtKind::Break:
        case StmtKind::Continue:
            break;
    }
}

}  // namespace

void MetaPipeline::validateEmbedCalls(TranslationUnit& translationUnit) const {
    for (const auto& declaration : translationUnit.declarations) {
        if (declaration->kind != DeclKind::Function) {
            continue;
        }

        const auto* function = static_cast<const FunctionDecl*>(declaration.get());
        visitStmtExprs(function->body.get(), [this](const Expr* expr) {
            if (expr == nullptr) {
                return;
            }

            if (expr->kind == ExprKind::CompileCall) {
                const auto* call = static_cast<const CompileCallExpr*>(expr);
                if (call->callee != "readfile" && call->callee != "generate_open_api") {
                    diagnostics_.warning(call->range, "unknown compile function '$" + call->callee + "' ignored");
                    return;
                }
                if (call->arguments.empty() || call->arguments.front()->kind != ExprKind::StringLiteral) {
                    diagnostics_.error(call->range, "$" + call->callee + " expects a leading string literal argument");
                }
            }

            if (expr->kind == ExprKind::Dialect) {
                diagnostics_.warning(expr->range, "dialect blocks are parsed but not yet lowered");
            }
        });
    }
}

}  // namespace axc
