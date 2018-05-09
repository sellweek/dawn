//
// Created by selvek on 8.5.2018.
//

#include "Codegen.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "../ast/expr/NumExpr.h"
#include "../ast/expr/VarExpr.h"
#include "../ast/expr/UnaryOpExpr.h"
#include "../ast/expr/BinaryOpExpr.h"
#include "../ast/expr/CallExpr.h"
#include "../ast/stmt/CallStmt.h"
#include "../ast/stmt/AssignmentStmt.h"
#include "../ast/stmt/CompoundStmt.h"
#include "../ast/stmt/IfStmt.h"

codegen::Codegen::Codegen(llvm::SourceMgr &Sources, llvm::Module &Module) :
        Module(Module), LastValue(nullptr), Builder(Module.getContext()),
        Sources(Sources) {
    FPM = std::make_unique<llvm::legacy::FunctionPassManager>(&Module);
    FPM->add(llvm::createPromoteMemoryToRegisterPass());
    FPM->add(llvm::createInstructionCombiningPass());
    FPM->add(llvm::createReassociatePass());
    FPM->add(llvm::createGVNPass());
    FPM->add(llvm::createCFGSimplificationPass());
    FPM->doInitialization();
}

void codegen::Codegen::visit(ast::BinaryOpExpr &E) {
    E.L->accept(*this); auto LHS = LastValue;
    E.R->accept(*this); auto RHS = LastValue;
    if (LHS == nullptr || RHS == nullptr) {
        LastValue = nullptr;
        return;
    }
    switch (E.Op) {
        case Lexeme::Kind::LT:
            generateICmp(llvm::CmpInst::Predicate::ICMP_SLT, LHS, RHS);
            break;
        case Lexeme::Kind::LTE:
            generateICmp(llvm::CmpInst::Predicate::ICMP_SLE, LHS, RHS);
            break;
        case Lexeme::Kind::GT:
            generateICmp(llvm::CmpInst::Predicate::ICMP_SGT, LHS, RHS);
            break;
        case Lexeme::Kind::GTE:
            generateICmp(llvm::CmpInst::Predicate::ICMP_SGE, LHS, RHS);
            break;
        case Lexeme::Kind::EQ:
            generateICmp(llvm::CmpInst::Predicate::ICMP_EQ, LHS, RHS);
            break;
        case Lexeme::Kind::NEQ:
            generateICmp(llvm::CmpInst::Predicate::ICMP_NE, LHS, RHS);
            break;
        case Lexeme::Kind::PLUS:
            LastValue = Builder.CreateAdd(LHS, RHS);
            break;
        case Lexeme::Kind::MINUS:
            LastValue = Builder.CreateSub(LHS, RHS);
            break;
        case Lexeme::Kind::MOD:
            LastValue = Builder.CreateSRem(LHS, RHS);
            break;
        case Lexeme::Kind::MUL:
            LastValue = Builder.CreateMul(LHS, RHS);
            break;
        case Lexeme::Kind::DIV:
            LastValue = Builder.CreateSDiv(LHS, RHS);
            break;
        case Lexeme::Kind::AND:
        case Lexeme::Kind::OR:
            generateLogical(E.Op, LHS, RHS);
            break;
        default:
            llvm_unreachable("Invalid binary operation in codegen");
    }
}

void codegen::Codegen::visit(ast::CallExpr &E) {
    //TODO: Implement function variable definitions and function returns
    lookupFunction(E.FunctionName);
    if (LastValue == nullptr)
        return LogError(E.Loc, "Undefined function called");
    auto F = llvm::dyn_cast<llvm::Function>(LastValue);
    if (F->arg_size() != E.Args.size())
        return LogError(E.Loc, "Invalid number of function arguments");
    std::vector<llvm::Value*> FunctionArgs;
    for (auto &Arg : E.Args) {
        Arg->accept(*this);
        if (LastValue == nullptr)
            return;
        FunctionArgs.push_back(LastValue);
    }
    LastValue = Builder.CreateCall(F, FunctionArgs, "calltmp");
}

void codegen::Codegen::visit(ast::NumExpr &E) {
    LastValue = llvm::ConstantInt::get(Module.getContext(), llvm::APInt(64, E.Value, true));
}

void codegen::Codegen::visit(ast::UnaryOpExpr &E) {
    E.Expression->accept(*this);
    auto Zero = llvm::ConstantInt::get(Module.getContext(), llvm::APInt(64, 0, true));
    switch (E.Op) {
        case Lexeme::Kind::MINUS:
            LastValue = Builder.CreateSub(Zero, LastValue, "unarymintmp");
            break;
        case Lexeme::Kind::NOT:
            LastValue = Builder.CreateIntCast(Builder.CreateICmpEQ(Zero, LastValue, "nottmp"),
                    llvm::Type::getInt64Ty(Module.getContext()), true);
            break;
        default:
            LogError(E.Loc, "Invalid binary operation");
    }
}

void codegen::Codegen::visit(ast::VarExpr &E) {
    llvm::Value *V = NamedValues[E.VarName];
    if (!V)
        return LogError(E.Loc, "Undeclared variable used");

    LastValue = Builder.CreateLoad(V, E.VarName.c_str());
}

void codegen::Codegen::visit(ast::AssignmentStmt &E) {
    auto Alloca = NamedValues[E.Var];
    if (!Alloca)
        return LogError(E.Loc, "Undeclared variable assigned to");
    E.Value->accept(*this);
    if (!LastValue)
        return;
    Builder.CreateStore(LastValue, Alloca);
}

void codegen::Codegen::visit(ast::CallStmt &E) {
    E.Callee->accept(*this);
}

void codegen::Codegen::visit(ast::CompoundStmt &E) {
    for (auto &Stmt : E.Body) {
        Stmt->accept(*this);
        if (LastValue == nullptr)
            return;
    }
}

void codegen::Codegen::visit(ast::ExitStmt &E) {
    auto Parent = Builder.GetInsertBlock()->getParent();
    llvm::BasicBlock *RetBlock = nullptr;
    for (auto &Block : Parent->getBasicBlockList()) {
        if (Block.getName() == "return") {
            RetBlock = &Block;
            break;
        }
    }
    assert(RetBlock != nullptr);
    Builder.CreateBr(RetBlock);

    auto NewBB = llvm::BasicBlock::Create(Module.getContext(), "afterexit", Parent);
    Builder.SetInsertPoint(NewBB);
}

void codegen::Codegen::visit(ast::ForStmt &E) {

}

void codegen::Codegen::visit(ast::IfStmt &E) {
    E.Condition->accept(*this);
    if (LastValue == nullptr)
        return;
    auto Condition = Builder.CreateICmpNE(LastValue,
                                        llvm::ConstantInt::get(Module.getContext(), llvm::APInt(64, 0, true)), "cond");
    llvm::Function *Fn = Builder.GetInsertBlock()->getParent();
    auto ThenBB = llvm::BasicBlock::Create(Module.getContext(), "then", Fn);
    auto ElseBB = llvm::BasicBlock::Create(Module.getContext(), "else", Fn);
    auto AfterBB = llvm::BasicBlock::Create(Module.getContext(), "after", Fn);

    Builder.CreateCondBr(Condition, ThenBB, ElseBB);

    Builder.SetInsertPoint(ThenBB);
    E.IfBody->accept(*this);
    if (LastValue == nullptr)
        return;
    Builder.CreateBr(AfterBB);

    Builder.SetInsertPoint(ElseBB);
    if (E.ElseBody) {
        E.ElseBody->accept(*this);
        if (LastValue == nullptr)
            return;
    }
    Builder.CreateBr(AfterBB);

    Builder.SetInsertPoint(AfterBB);
}

void codegen::Codegen::visit(ast::WhileStmt &E) {

}

void codegen::Codegen::visit(ast::Consts &E) {

}


void codegen::Codegen::visit(ast::Prototype &E) {
    //TODO: Handle redefinition errors
    Prototypes[E.Name] = &E;
}

void codegen::Codegen::visit(ast::Function &E) {
    //TODO: Handle redefinition errors
    generatePrototype(*E.Proto);
    auto F = llvm::dyn_cast<llvm::Function>(LastValue);

    llvm::BasicBlock *Body = llvm::BasicBlock::Create(Module.getContext(), "entry", F);
    Builder.SetInsertPoint(Body);

    llvm::StringMap<llvm::AllocaInst *> OldNamedValues;
    std::swap(NamedValues, OldNamedValues);

    for (auto &Arg : F->args()) {
        llvm::AllocaInst *Alloca = createAlloca(F, Arg.getName());
        Builder.CreateStore(&Arg, Alloca);
        NamedValues[Arg.getName()] = Alloca;
    }
    auto Zero = llvm::ConstantInt::get(Module.getContext(), llvm::APInt(64, 0, true));
    for (const auto &VarInfo : E.Variables->Variables) {
        //TODO: Handle different type names
        llvm::AllocaInst *Alloca = createAlloca(F, VarInfo.first);
        Builder.CreateStore(Zero, Alloca);
        NamedValues[VarInfo.first] = Alloca;
    }

    if (E.Proto->ReturnType != "void") {
        llvm::AllocaInst *RetVal = createAlloca(F, F->getName());
        Builder.CreateStore(Zero, RetVal);
        NamedValues[F->getName()] = RetVal;
    }

    llvm::BasicBlock *RetBlock = llvm::BasicBlock::Create(Module.getContext(), "return", F);
    Builder.SetInsertPoint(RetBlock);
    if (E.Proto->ReturnType != "void") {
        auto ReturnResult = Builder.CreateLoad(NamedValues[F->getName()]);
        Builder.CreateRet(ReturnResult);
    } else {
        Builder.CreateRetVoid();
    }

    Builder.SetInsertPoint(Body);
    E.Body->accept(*this);
    if (LastValue == nullptr)
        return;
    Builder.CreateBr(RetBlock);

    assert(!llvm::verifyFunction(*F, &llvm::errs()));
    FPM->run(*F);
    std::swap(NamedValues, OldNamedValues);

    LastValue = F;
}

void codegen::Codegen::visit(ast::Program &E) {

}

void codegen::Codegen::visit(ast::Vars &E) {
    llvm_unreachable("Var generation should be handled in their parent");
}

void codegen::Codegen::generateICmp(llvm::CmpInst::Predicate Pred, llvm::Value *LHS, llvm::Value *RHS) {
    LHS = Builder.CreateICmp(Pred, LHS, RHS, "cmptmp");
    LastValue = Builder.CreateIntCast(LHS, llvm::Type::getInt64Ty(Module.getContext()), true);
}

void codegen::Codegen::generateLogical(Lexeme::Kind Op, llvm::Value *LHS, llvm::Value *RHS) {
    auto Zero = llvm::ConstantInt::get(Module.getContext(), llvm::APInt(64, 0, true));
    LHS = Builder.CreateIntCast(Builder.CreateICmpNE(Zero, LHS, "lhscoercion"),
                               llvm::Type::getInt64Ty(Module.getContext()), true);
    RHS = Builder.CreateIntCast(Builder.CreateICmpNE(Zero, RHS, "rhscoercion"),
                                llvm::Type::getInt64Ty(Module.getContext()), true);
    if (Op == Lexeme::Kind::AND)
        LastValue = Builder.CreateAnd(LHS, RHS, "andop");
    else if (Op == Lexeme::Kind::OR)
        LastValue = Builder.CreateOr(LHS, RHS, "orop");
    else
        llvm_unreachable("Invalid logical operation in codegen");
}

void codegen::Codegen::lookupFunction(llvm::StringRef Name) {
    if (auto *F = Module.getFunction(Name))
        LastValue = F;

    if (Prototypes.count(Name))
        return generatePrototype(*Prototypes[Name]);

    LastValue = nullptr;
}

llvm::AllocaInst *codegen::Codegen::createAlloca(llvm::Function *F, llvm::StringRef VarName) {
    llvm::IRBuilder<> B(&F->getEntryBlock(), F->getEntryBlock().begin());
    return B.CreateAlloca(llvm::Type::getInt64Ty(Module.getContext()), nullptr, VarName);
}

void codegen::Codegen::generatePrototype(ast::Prototype &P) {
    std::vector<llvm::Type *> Ints(P.Parameters.size(), llvm::Type::getInt64Ty(Module.getContext()));
    llvm::Type *ReturnType;
    if (P.ReturnType == "integer")
        ReturnType = llvm::Type::getInt64Ty(Module.getContext());
    else
        ReturnType = llvm::Type::getVoidTy(Module.getContext());
    llvm::FunctionType *FT = llvm::FunctionType::get(ReturnType, Ints, false);

    auto *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage, P.Name, &Module);
    size_t i = 0;
    for (auto &Param : F->args())
        Param.setName(P.Parameters[i++].first);
    LastValue = F;
}