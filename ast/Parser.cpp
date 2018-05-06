//
// Created by selvek on 5.5.2018.
//

#include "Parser.h"
#include "NumExpr.h"
#include "VarExpr.h"
#include "CallExpr.h"
#include "UnaryOpExpr.h"
#include "BinaryOpExpr.h"

ast::Parser::Parser(Lexer Lexer): L(std::move(Lexer)), NextLexeme(L.getLexeme()), Precedences(11) {
    Precedences[static_cast<int>(Lexeme::Kind::EQ)] = 5;
    Precedences[static_cast<int>(Lexeme::Kind::NEQ)] = 5;
    Precedences[static_cast<int>(Lexeme::Kind::LT)] = 10;
    Precedences[static_cast<int>(Lexeme::Kind::LTE)] = 10;
    Precedences[static_cast<int>(Lexeme::Kind::GT)] = 10;
    Precedences[static_cast<int>(Lexeme::Kind::GTE)] = 10;
    Precedences[static_cast<int>(Lexeme::Kind::PLUS)] = 20;
    Precedences[static_cast<int>(Lexeme::Kind::MINUS)] = 20;
    Precedences[static_cast<int>(Lexeme::Kind::MUL)] = 30;
    Precedences[static_cast<int>(Lexeme::Kind::DIV)] = 30;
    Precedences[static_cast<int>(Lexeme::Kind::MOD)] = 40;
}

Lexeme ast::Parser::getLexeme() {
    Lexeme OldLexeme = NextLexeme;
    NextLexeme = L.getLexeme();
    return OldLexeme;
}

std::unique_ptr<ast::Expr> ast::Parser::parseNumberExpr() {
    auto N = getLexeme();
    assert(N.K == Lexeme::Kind::NUMBER);
    return std::make_unique<ast::NumExpr>(N.NumericValue);
}

std::unique_ptr<ast::Expr> ast::Parser::parseIdentifierExpr() {
    auto Ident = getLexeme();
    assert(Ident.K == Lexeme::Kind::IDENT);
    if (NextLexeme.K != Lexeme::Kind::LPAR) {
        return std::make_unique<ast::VarExpr>(Ident.IdentifierStr);
    }
    auto Call = std::make_unique<ast::CallExpr>(Ident.IdentifierStr);
    while (NextLexeme.K != Lexeme::Kind::RPAR) {
        Call->addArgument(parseExpr());
        if (!(NextLexeme.K == Lexeme::Kind::UNKNOWN && NextLexeme.Char == ',')) {
            return LogError("Expected function argument separator (,)");
        }
        getLexeme();
    }
    getLexeme();
}

std::unique_ptr<ast::Expr> ast::Parser::parseUnaryOpExpr() {
    auto Op = getLexeme();
    assert(Op.K == Lexeme::Kind::MINUS || Op.K == Lexeme::Kind::NOT);
    auto E = parseExpr();
    return std::make_unique<ast::UnaryOpExpr>(Op.K, std::move(E));
}

std::unique_ptr<ast::Expr> ast::Parser::parseParenExpr() {
    auto L = getLexeme();
    assert(L.K == Lexeme::Kind::LPAR);
    auto E = parseExpr();
    if (NextLexeme.K != Lexeme::Kind::RPAR) {
        return LogError("Unclosed parenthesised expression");
    }
    getLexeme();
    return E;
}

std::unique_ptr<ast::Expr> ast::Parser::parsePrimaryExpr() {
    switch (NextLexeme.K) {
        case Lexeme::Kind::IDENT:
            return parseIdentifierExpr();
        case Lexeme::Kind::NUMBER:
            return parseNumberExpr();
        case Lexeme::Kind::MINUS:
        case Lexeme::Kind::NOT:
            return parseUnaryOpExpr();
        case Lexeme::Kind::LPAR:
            return parseParenExpr();
        default:
            return LogError("Primary expression beginning with unexpected token");
    }
}

std::unique_ptr<ast::Expr> ast::Parser::parseExpr() {
    auto LHS = parsePrimaryExpr();
    if (!LHS)
        return nullptr;

    return parseBinExprRHS(1, std::move(LHS));
}

std::unique_ptr<ast::Expr> ast::Parser::parseBinExprRHS(unsigned Precedence, std::unique_ptr<ast::Expr> LHS) {
    while (true) {
        unsigned CurrentPrecedence = getPrecedence(NextLexeme.K);
        if (CurrentPrecedence < Precedence) {
            return LHS;
        }

        auto Op = getLexeme().K;
        auto RHS = parsePrimaryExpr();
        if (!RHS)
            return nullptr;

        unsigned NextPrecedence = getPrecedence(NextLexeme.K);
        if (CurrentPrecedence < NextPrecedence) {
            RHS = parseBinExprRHS(CurrentPrecedence + 1, std::move(RHS));
            if (!RHS)
                return nullptr;
        }

        LHS = std::make_unique<ast::BinaryOpExpr>(Op, std::move(LHS), std::move(RHS));
    }
}

unsigned ast::Parser::getPrecedence(Lexeme::Kind K) {
    int Key = static_cast<int>(K);
    if (Precedences.count(Key) == 0)
        return 0;
    return Precedences[Key];
}
