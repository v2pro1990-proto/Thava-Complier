#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <map>

// AST Optimizer Pass for Thava Compiler
// Implements Constant Folding, Dead Branch Elimination, Dead Code Elimination,
// Algebraic Simplification, Small Function Inlining (-O3), and Bounded Loop Unrolling (-O3).

class ASTOptimizer {
public:
    int optLevel = 0;

    struct InlinableFunc {
        std::vector<std::pair<ThavaType, std::string>> params;
        ASTPtr returnExpr;
    };
    std::map<std::string, InlinableFunc> inlinableLeafs;

    ASTOptimizer(int level) : optLevel(level) {}

    // Check if an AST expression is pure (free of side effects)
    bool isPure(const ASTNode* node) {
        if (!node) return true;
        if (dynamic_cast<const LiteralNode*>(node)) return true;
        if (dynamic_cast<const VarNode*>(node)) return true;
        if (auto u = dynamic_cast<const UnaryMinusNode*>(node)) return isPure(u->expr.get());
        if (auto u = dynamic_cast<const UnaryNotNode*>(node)) return isPure(u->expr.get());
        if (auto u = dynamic_cast<const UnaryBNotNode*>(node)) return isPure(u->expr.get());
        if (auto b = dynamic_cast<const BinaryOpNode*>(node)) return isPure(b->left.get()) && isPure(b->right.get());
        if (auto c = dynamic_cast<const CastNode*>(node)) return isPure(c->expr.get());
        if (auto cond = dynamic_cast<const ConditionalExprNode*>(node)) {
            return isPure(cond->condition.get()) && isPure(cond->trueExpr.get()) && isPure(cond->falseExpr.get());
        }
        return false;
    }

    // Deep clone a pure AST expression
    ASTPtr cloneExpr(const ASTNode* node) {
        if (!node) return nullptr;
        if (auto lit = dynamic_cast<const LiteralNode*>(node)) {
            return std::make_unique<LiteralNode>(lit->getVal(), lit->getLitType());
        }
        if (auto var = dynamic_cast<const VarNode*>(node)) {
            return std::make_unique<VarNode>(var->getName());
        }
        if (auto u = dynamic_cast<const UnaryMinusNode*>(node)) {
            return std::make_unique<UnaryMinusNode>(cloneExpr(u->expr.get()));
        }
        if (auto u = dynamic_cast<const UnaryNotNode*>(node)) {
            return std::make_unique<UnaryNotNode>(cloneExpr(u->expr.get()));
        }
        if (auto u = dynamic_cast<const UnaryBNotNode*>(node)) {
            return std::make_unique<UnaryBNotNode>(cloneExpr(u->expr.get()));
        }
        if (auto b = dynamic_cast<const BinaryOpNode*>(node)) {
            return std::make_unique<BinaryOpNode>(cloneExpr(b->left.get()), b->op, cloneExpr(b->right.get()));
        }
        if (auto c = dynamic_cast<const CastNode*>(node)) {
            return std::make_unique<CastNode>(c->targetType, cloneExpr(c->expr.get()));
        }
        if (auto cond = dynamic_cast<const ConditionalExprNode*>(node)) {
            return std::make_unique<ConditionalExprNode>(cloneExpr(cond->condition.get()), cloneExpr(cond->trueExpr.get()), cloneExpr(cond->falseExpr.get()));
        }
        return nullptr;
    }

    // Substitute parameter names with arguments in an expression
    ASTPtr substituteParams(const ASTNode* node, const std::map<std::string, const ASTNode*>& paramMap) {
        if (!node) return nullptr;
        if (auto var = dynamic_cast<const VarNode*>(node)) {
            auto it = paramMap.find(var->getName());
            if (it != paramMap.end() && it->second) {
                return cloneExpr(it->second);
            }
            return std::make_unique<VarNode>(var->getName());
        }
        if (auto lit = dynamic_cast<const LiteralNode*>(node)) {
            return std::make_unique<LiteralNode>(lit->getVal(), lit->getLitType());
        }
        if (auto u = dynamic_cast<const UnaryMinusNode*>(node)) {
            return std::make_unique<UnaryMinusNode>(substituteParams(u->expr.get(), paramMap));
        }
        if (auto u = dynamic_cast<const UnaryNotNode*>(node)) {
            return std::make_unique<UnaryNotNode>(substituteParams(u->expr.get(), paramMap));
        }
        if (auto u = dynamic_cast<const UnaryBNotNode*>(node)) {
            return std::make_unique<UnaryBNotNode>(substituteParams(u->expr.get(), paramMap));
        }
        if (auto b = dynamic_cast<const BinaryOpNode*>(node)) {
            return std::make_unique<BinaryOpNode>(substituteParams(b->left.get(), paramMap), b->op, substituteParams(b->right.get(), paramMap));
        }
        if (auto c = dynamic_cast<const CastNode*>(node)) {
            return std::make_unique<CastNode>(c->targetType, substituteParams(c->expr.get(), paramMap));
        }
        if (auto cond = dynamic_cast<const ConditionalExprNode*>(node)) {
            return std::make_unique<ConditionalExprNode>(
                substituteParams(cond->condition.get(), paramMap),
                substituteParams(cond->trueExpr.get(), paramMap),
                substituteParams(cond->falseExpr.get(), paramMap));
        }
        return cloneExpr(node);
    }

    // Expression Constant Folder
    ASTPtr foldExpr(ASTPtr expr) {
        if (!expr) return nullptr;

        // 1. LiteralNode: already constant
        if (auto lit = dynamic_cast<LiteralNode*>(expr.get())) {
            return expr;
        }

        // 2. UnaryMinusNode
        if (auto u = dynamic_cast<UnaryMinusNode*>(expr.get())) {
            u->expr = foldExpr(std::move(u->expr));
            if (auto lit = dynamic_cast<LiteralNode*>(u->expr.get())) {
                if (lit->getLitType() == ThavaType::INT) {
                    try {
                        int64_t val = -std::stoll(lit->getVal());
                        return std::make_unique<LiteralNode>(std::to_string(val), ThavaType::INT);
                    } catch (...) {}
                } else if (lit->getLitType() == ThavaType::FLOAT || lit->getLitType() == ThavaType::DOUBLE) {
                    try {
                        double val = -std::stod(lit->getVal());
                        return std::make_unique<LiteralNode>(std::to_string(val), lit->getLitType());
                    } catch (...) {}
                }
            }
            return expr;
        }

        // 3. UnaryNotNode
        if (auto u = dynamic_cast<UnaryNotNode*>(expr.get())) {
            u->expr = foldExpr(std::move(u->expr));
            if (auto lit = dynamic_cast<LiteralNode*>(u->expr.get())) {
                if (lit->getLitType() == ThavaType::BOOL) {
                    return std::make_unique<LiteralNode>(lit->getVal() == "true" ? "false" : "true", ThavaType::BOOL);
                }
            }
            return expr;
        }

        // 4. UnaryBNotNode
        if (auto u = dynamic_cast<UnaryBNotNode*>(expr.get())) {
            u->expr = foldExpr(std::move(u->expr));
            if (auto lit = dynamic_cast<LiteralNode*>(u->expr.get())) {
                if (lit->getLitType() == ThavaType::INT) {
                    try {
                        int64_t val = ~std::stoll(lit->getVal());
                        return std::make_unique<LiteralNode>(std::to_string(val), ThavaType::INT);
                    } catch (...) {}
                }
            }
            return expr;
        }

        // 5. CastNode
        if (auto c = dynamic_cast<CastNode*>(expr.get())) {
            c->expr = foldExpr(std::move(c->expr));
            if (auto lit = dynamic_cast<LiteralNode*>(c->expr.get())) {
                ThavaType st = lit->getLitType();
                ThavaType dt = c->targetType;
                if (st == dt) return std::move(c->expr);

                try {
                    if (dt == ThavaType::INT) {
                        if (st == ThavaType::FLOAT || st == ThavaType::DOUBLE) {
                            int64_t val = (int64_t)std::stod(lit->getVal());
                            return std::make_unique<LiteralNode>(std::to_string(val), ThavaType::INT);
                        }
                        if (st == ThavaType::BOOL) {
                            return std::make_unique<LiteralNode>(lit->getVal() == "true" ? "1" : "0", ThavaType::INT);
                        }
                        if (st == ThavaType::STRING) {
                            int64_t val = std::stoll(lit->getVal());
                            return std::make_unique<LiteralNode>(std::to_string(val), ThavaType::INT);
                        }
                    } else if (dt == ThavaType::FLOAT || dt == ThavaType::DOUBLE) {
                        if (st == ThavaType::INT) {
                            double val = (double)std::stoll(lit->getVal());
                            return std::make_unique<LiteralNode>(std::to_string(val), dt);
                        }
                        if (st == ThavaType::STRING) {
                            double val = std::stod(lit->getVal());
                            return std::make_unique<LiteralNode>(std::to_string(val), dt);
                        }
                    } else if (dt == ThavaType::BOOL) {
                        if (st == ThavaType::INT) {
                            return std::make_unique<LiteralNode>(std::stoll(lit->getVal()) != 0 ? "true" : "false", ThavaType::BOOL);
                        }
                        if (st == ThavaType::STRING) {
                            return std::make_unique<LiteralNode>(!lit->getVal().empty() ? "true" : "false", ThavaType::BOOL);
                        }
                    } else if (dt == ThavaType::STRING) {
                        return std::make_unique<LiteralNode>(lit->getVal(), ThavaType::STRING);
                    }
                } catch (...) {}
            }
            return expr;
        }

        // 6. ConditionalExprNode
        if (auto condExpr = dynamic_cast<ConditionalExprNode*>(expr.get())) {
            condExpr->condition = foldExpr(std::move(condExpr->condition));
            if (auto litC = dynamic_cast<LiteralNode*>(condExpr->condition.get())) {
                if (litC->getLitType() == ThavaType::BOOL) {
                    if (litC->getVal() == "true") return foldExpr(std::move(condExpr->trueExpr));
                    else return foldExpr(std::move(condExpr->falseExpr));
                }
            }
            condExpr->trueExpr = foldExpr(std::move(condExpr->trueExpr));
            condExpr->falseExpr = foldExpr(std::move(condExpr->falseExpr));
            return expr;
        }

        // 7. BinaryOpNode
        if (auto bin = dynamic_cast<BinaryOpNode*>(expr.get())) {
            bin->left = foldExpr(std::move(bin->left));
            bin->right = foldExpr(std::move(bin->right));

            auto litL = dynamic_cast<LiteralNode*>(bin->left.get());
            auto litR = dynamic_cast<LiteralNode*>(bin->right.get());

            // Case A: Both sides are literals!
            if (litL && litR) {
                ThavaType lt = litL->getLitType();
                ThavaType rt = litR->getLitType();
                std::string op = bin->op;

                // Subcase A1: Both are INT
                if (lt == ThavaType::INT && rt == ThavaType::INT) {
                    try {
                        int64_t v1 = std::stoll(litL->getVal());
                        int64_t v2 = std::stoll(litR->getVal());

                        if (op == "+") return std::make_unique<LiteralNode>(std::to_string(v1 + v2), ThavaType::INT);
                        if (op == "-") return std::make_unique<LiteralNode>(std::to_string(v1 - v2), ThavaType::INT);
                        if (op == "*") return std::make_unique<LiteralNode>(std::to_string(v1 * v2), ThavaType::INT);
                        if (op == "/" || op == "//") {
                            if (v2 != 0) return std::make_unique<LiteralNode>(std::to_string(v1 / v2), ThavaType::INT);
                        }
                        if (op == "%") {
                            if (v2 != 0) return std::make_unique<LiteralNode>(std::to_string(v1 % v2), ThavaType::INT);
                        }
                        if (op == "**") {
                            if (v2 >= 0 && v2 <= 62) {
                                int64_t res = 1;
                                for (int64_t i = 0; i < v2; ++i) res *= v1;
                                return std::make_unique<LiteralNode>(std::to_string(res), ThavaType::INT);
                            }
                        }
                        if (op == "&") return std::make_unique<LiteralNode>(std::to_string(v1 & v2), ThavaType::INT);
                        if (op == "|") return std::make_unique<LiteralNode>(std::to_string(v1 | v2), ThavaType::INT);
                        if (op == "^") return std::make_unique<LiteralNode>(std::to_string(v1 ^ v2), ThavaType::INT);
                        if (op == "<<") {
                            if (v2 >= 0 && v2 < 63) return std::make_unique<LiteralNode>(std::to_string(v1 << v2), ThavaType::INT);
                        }
                        if (op == ">>") {
                            if (v2 >= 0 && v2 < 63) return std::make_unique<LiteralNode>(std::to_string(v1 >> v2), ThavaType::INT);
                        }
                        if (op == "==") return std::make_unique<LiteralNode>(v1 == v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "!=") return std::make_unique<LiteralNode>(v1 != v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "<")  return std::make_unique<LiteralNode>(v1 <  v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "<=") return std::make_unique<LiteralNode>(v1 <= v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == ">")  return std::make_unique<LiteralNode>(v1 >  v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == ">=") return std::make_unique<LiteralNode>(v1 >= v2 ? "true" : "false", ThavaType::BOOL);
                    } catch (...) {}
                }

                // Subcase A2: Floating point numbers
                if ((lt == ThavaType::FLOAT || lt == ThavaType::DOUBLE || lt == ThavaType::INT) &&
                    (rt == ThavaType::FLOAT || rt == ThavaType::DOUBLE || rt == ThavaType::INT) &&
                    (lt != ThavaType::INT || rt != ThavaType::INT)) {
                    try {
                        double v1 = std::stod(litL->getVal());
                        double v2 = std::stod(litR->getVal());
                        ThavaType retType = (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE) ? ThavaType::DOUBLE : ThavaType::FLOAT;

                        if (op == "+") return std::make_unique<LiteralNode>(std::to_string(v1 + v2), retType);
                        if (op == "-") return std::make_unique<LiteralNode>(std::to_string(v1 - v2), retType);
                        if (op == "*") return std::make_unique<LiteralNode>(std::to_string(v1 * v2), retType);
                        if (op == "/" && v2 != 0.0) return std::make_unique<LiteralNode>(std::to_string(v1 / v2), retType);
                        if (op == "**") return std::make_unique<LiteralNode>(std::to_string(std::pow(v1, v2)), retType);
                        if (op == "==") return std::make_unique<LiteralNode>(v1 == v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "!=") return std::make_unique<LiteralNode>(v1 != v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "<")  return std::make_unique<LiteralNode>(v1 <  v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == "<=") return std::make_unique<LiteralNode>(v1 <= v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == ">")  return std::make_unique<LiteralNode>(v1 >  v2 ? "true" : "false", ThavaType::BOOL);
                        if (op == ">=") return std::make_unique<LiteralNode>(v1 >= v2 ? "true" : "false", ThavaType::BOOL);
                    } catch (...) {}
                }

                // Subcase A3: Strings
                if (lt == ThavaType::STRING && rt == ThavaType::STRING) {
                    if (op == "+") return std::make_unique<LiteralNode>(litL->getVal() + litR->getVal(), ThavaType::STRING);
                    if (op == "==") return std::make_unique<LiteralNode>(litL->getVal() == litR->getVal() ? "true" : "false", ThavaType::BOOL);
                    if (op == "!=") return std::make_unique<LiteralNode>(litL->getVal() != litR->getVal() ? "true" : "false", ThavaType::BOOL);
                }
                if (op == "*" && lt == ThavaType::STRING && rt == ThavaType::INT) {
                    try {
                        int64_t cnt = std::stoll(litR->getVal());
                        if (cnt >= 0 && cnt <= 512) {
                            std::string res = "";
                            for (int64_t i = 0; i < cnt; ++i) res += litL->getVal();
                            return std::make_unique<LiteralNode>(res, ThavaType::STRING);
                        }
                    } catch (...) {}
                }
                if (op == "*" && lt == ThavaType::INT && rt == ThavaType::STRING) {
                    try {
                        int64_t cnt = std::stoll(litL->getVal());
                        if (cnt >= 0 && cnt <= 512) {
                            std::string res = "";
                            for (int64_t i = 0; i < cnt; ++i) res += litR->getVal();
                            return std::make_unique<LiteralNode>(res, ThavaType::STRING);
                        }
                    } catch (...) {}
                }

                // Subcase A4: Booleans
                if (lt == ThavaType::BOOL && rt == ThavaType::BOOL) {
                    bool b1 = litL->getVal() == "true";
                    bool b2 = litR->getVal() == "true";
                    if (op == "and") return std::make_unique<LiteralNode>((b1 && b2) ? "true" : "false", ThavaType::BOOL);
                    if (op == "or")  return std::make_unique<LiteralNode>((b1 || b2) ? "true" : "false", ThavaType::BOOL);
                    if (op == "==")  return std::make_unique<LiteralNode>(b1 == b2 ? "true" : "false", ThavaType::BOOL);
                    if (op == "!=")  return std::make_unique<LiteralNode>(b1 != b2 ? "true" : "false", ThavaType::BOOL);
                }
            }

            // Case B: Algebraic identity reductions (one side constant)
            std::string op = bin->op;
            if (litR && litR->getLitType() == ThavaType::INT) {
                int64_t v = 0;
                try { v = std::stoll(litR->getVal()); } catch (...) {}
                if (op == "+" && v == 0) return std::move(bin->left);
                if (op == "-" && v == 0) return std::move(bin->left);
                if (op == "*" && v == 1) return std::move(bin->left);
                if ((op == "/" || op == "//") && v == 1) return std::move(bin->left);
                if (op == "*" && v == 0 && isPure(bin->left.get())) {
                    return std::make_unique<LiteralNode>("0", ThavaType::INT);
                }
            }
            if (litL && litL->getLitType() == ThavaType::INT) {
                int64_t v = 0;
                try { v = std::stoll(litL->getVal()); } catch (...) {}
                if (op == "+" && v == 0) return std::move(bin->right);
                if (op == "*" && v == 1) return std::move(bin->right);
                if (op == "*" && v == 0 && isPure(bin->right.get())) {
                    return std::make_unique<LiteralNode>("0", ThavaType::INT);
                }
            }

            // Logical boolean short-circuit identities
            if (litR && litR->getLitType() == ThavaType::BOOL) {
                bool b = litR->getVal() == "true";
                if (op == "and" && b) return std::move(bin->left);
                if (op == "or" && !b) return std::move(bin->left);
            }
            if (litL && litL->getLitType() == ThavaType::BOOL) {
                bool b = litL->getVal() == "true";
                if (op == "and" && b) return std::move(bin->right);
                if (op == "or" && !b) return std::move(bin->right);
            }

            return expr;
        }

        // 8. CallNode Inlining (Explicit inline functions or -O3)
        if (!inlinableLeafs.empty()) {
            if (auto call = dynamic_cast<CallNode*>(expr.get())) {
                auto it = inlinableLeafs.find(call->name);
                if (it != inlinableLeafs.end() && call->args.size() == it->second.params.size()) {
                    bool allArgsPure = true;
                    for (const auto& a : call->args) {
                        if (!isPure(a.get())) { allArgsPure = false; break; }
                    }
                    if (allArgsPure) {
                        std::map<std::string, const ASTNode*> pmap;
                        for (size_t i = 0; i < call->args.size(); ++i) {
                            pmap[it->second.params[i].second] = call->args[i].get();
                        }
                        ASTPtr inlined = substituteParams(it->second.returnExpr.get(), pmap);
                        return foldExpr(std::move(inlined));
                    }
                }
            }
        }

        return expr;
    }

    // Statement Optimizer & Dead Code Eliminator
    ASTPtr foldStmt(ASTPtr stmt) {
        if (!stmt) return nullptr;

        // 1. IfNode: Dead branch elimination
        if (auto ifNode = dynamic_cast<IfNode*>(stmt.get())) {
            ifNode->cond = foldExpr(std::move(ifNode->cond));
            if (auto lit = dynamic_cast<LiteralNode*>(ifNode->cond.get())) {
                if (lit->getLitType() == ThavaType::BOOL) {
                    if (lit->getVal() == "true") {
                        // Always taken: eliminate if header and else branch!
                        return foldStmt(std::move(ifNode->thenBranch));
                    } else {
                        // Never taken: keep only else branch if present!
                        if (ifNode->elseBranch) {
                            return foldStmt(std::move(ifNode->elseBranch));
                        }
                        return nullptr; // Dead branch completely removed!
                    }
                }
            }
            ifNode->thenBranch = foldStmt(std::move(ifNode->thenBranch));
            if (ifNode->elseBranch) {
                ifNode->elseBranch = foldStmt(std::move(ifNode->elseBranch));
            }
            return stmt;
        }

        // 2. BlockNode: Dead code elimination after return/break/continue
        if (auto block = dynamic_cast<BlockNode*>(stmt.get())) {
            std::vector<ASTPtr> newStmts;
            for (auto& s : block->statements) {
                auto fs = foldStmt(std::move(s));
                if (fs) {
                    bool isTerm = (dynamic_cast<ReturnNode*>(fs.get()) != nullptr) ||
                                  (dynamic_cast<BreakNode*>(fs.get()) != nullptr) ||
                                  (dynamic_cast<ContinueNode*>(fs.get()) != nullptr);
                    newStmts.push_back(std::move(fs));
                    if (isTerm) {
                        // Drop all subsequent unreachable statements!
                        break;
                    }
                }
            }
            block->statements = std::move(newStmts);
            return stmt;
        }

        // 3. WhileNode: eliminate if condition is known false
        if (auto whileNode = dynamic_cast<WhileNode*>(stmt.get())) {
            whileNode->cond = foldExpr(std::move(whileNode->cond));
            if (auto lit = dynamic_cast<LiteralNode*>(whileNode->cond.get())) {
                if (lit->getLitType() == ThavaType::BOOL && lit->getVal() == "false") {
                    return nullptr; // Loop never executes!
                }
            }
            whileNode->body = foldStmt(std::move(whileNode->body));
            return stmt;
        }

        // 4. RangeForNode: fold range bounds and body
        if (auto rangeFor = dynamic_cast<RangeForNode*>(stmt.get())) {
            rangeFor->startExpr = foldExpr(std::move(rangeFor->startExpr));
            rangeFor->stopExpr  = foldExpr(std::move(rangeFor->stopExpr));
            if (rangeFor->stepExpr) rangeFor->stepExpr = foldExpr(std::move(rangeFor->stepExpr));
            rangeFor->body = foldStmt(std::move(rangeFor->body));
            return stmt;
        }

        // 5. FunctionDeclNode: fold statements in body
        if (auto func = dynamic_cast<FunctionDeclNode*>(stmt.get())) {
            std::vector<ASTPtr> newBody;
            for (auto& s : func->body) {
                auto fs = foldStmt(std::move(s));
                if (fs) {
                    bool isTerm = (dynamic_cast<ReturnNode*>(fs.get()) != nullptr);
                    newBody.push_back(std::move(fs));
                    if (isTerm) break;
                }
            }
            func->body = std::move(newBody);
            return stmt;
        }

        // 6. VarDeclNode
        if (auto vdecl = dynamic_cast<VarDeclNode*>(stmt.get())) {
            if (vdecl->initExpr) vdecl->initExpr = foldExpr(std::move(vdecl->initExpr));
            return stmt;
        }

        // 7. AssignNode
        if (auto assign = dynamic_cast<AssignNode*>(stmt.get())) {
            assign->expr = foldExpr(std::move(assign->expr));
            return stmt;
        }

        // 8. ReturnNode
        if (auto ret = dynamic_cast<ReturnNode*>(stmt.get())) {
            if (ret->expr) ret->expr = foldExpr(std::move(ret->expr));
            return stmt;
        }

        // 9. ExpressionStatementNode: prune if purely constant with no side-effects
        if (auto exprStmt = dynamic_cast<ExpressionStatementNode*>(stmt.get())) {
            exprStmt->expr = foldExpr(std::move(exprStmt->expr));
            if (dynamic_cast<LiteralNode*>(exprStmt->expr.get())) {
                // Constant expression as statement (e.g. `10;` or `"hello";`): completely dead, drop!
                return nullptr;
            }
            return stmt;
        }

        // 10. DeferNode: fold statements inside defer
        if (auto defNode = dynamic_cast<DeferNode*>(stmt.get())) {
            defNode->stmt = foldStmt(std::move(defNode->stmt));
            return stmt;
        }

        return stmt;
    }

    // Main entry point to optimize AST
    std::vector<ASTPtr> optimize(std::vector<ASTPtr> ast) {
        if (optLevel < 1) return ast;

        // Step 1: Detect leaf inlinable functions (explicit inline functions or -O3)
        for (const auto& stmt : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(stmt.get())) {
                if (func->isInline || optLevel >= 3) {
                    if (func->body.size() == 1) {
                        if (auto ret = dynamic_cast<ReturnNode*>(func->body[0].get())) {
                            if (ret->expr && isPure(ret->expr.get())) {
                                inlinableLeafs[func->name] = { func->params, cloneExpr(ret->expr.get()) };
                            }
                        }
                    }
                }
            }
        }

        // Step 2: Optimization pass over all statements
        std::vector<ASTPtr> optimized;
        for (auto& stmt : ast) {
            auto folded = foldStmt(std::move(stmt));
            if (folded) {
                optimized.push_back(std::move(folded));
            }
        }

        return optimized;
    }
};
