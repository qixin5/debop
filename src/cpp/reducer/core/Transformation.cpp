#include "Transformation.h"

#include "SourceManager.h"

#include <queue>

#include "clang/Lex/Lexer.h"

void Transformation::Initialize(clang::ASTContext &C) {
  Context = &C;
  TheRewriter.setSourceMgr(Context->getSourceManager(), Context->getLangOpts());
}

std::vector<clang::Stmt *> Transformation::getAllChildren(clang::Stmt *S) {
  std::queue<clang::Stmt *> ToVisit;
  std::vector<clang::Stmt *> AllChildren;
  ToVisit.push(S);
  while (!ToVisit.empty()) {
    auto C = ToVisit.front();
    ToVisit.pop();
    AllChildren.emplace_back(C);
    for (auto const &Child : C->children()) {
      if (Child != NULL)
        ToVisit.push(Child);
    }
  }
  return AllChildren;
}

std::vector<clang::Stmt *> Transformation::getAllPrimitiveChildrenStmts(clang::CompoundStmt *S) {
  std::queue<clang::Stmt *> ToVisit;
  std::vector<clang::Stmt *> AllPrimitiveChildrenStmts;
  ToVisit.push(S);
  while (!ToVisit.empty()) {
    auto C = ToVisit.front();
    ToVisit.pop();

    if (clang::CompoundStmt *CS = llvm::dyn_cast<clang::CompoundStmt>(C)) {
      for (auto ChildS : CS->body())
	if (ChildS != NULL)
	  ToVisit.push(ChildS);
    }
    else if (clang::DoStmt *DS = llvm::dyn_cast<clang::DoStmt>(C)) {
      ToVisit.push(DS->getBody());
    }
    else if (clang::ForStmt *FS = llvm::dyn_cast<clang::ForStmt>(C)) {
      ToVisit.push(FS->getBody());
    }
    else if (clang::IfStmt *IS = llvm::dyn_cast<clang::IfStmt>(C)) {
      ToVisit.push(IS->getThen());
      if (IS->hasElseStorage()) {
	ToVisit.push(IS->getElse());
      }
    }
    else if (clang::LabelStmt *LS = llvm::dyn_cast<clang::LabelStmt>(C)) {
      ToVisit.push(LS->getSubStmt());
    }
    else if (clang::SwitchStmt *SS = llvm::dyn_cast<clang::SwitchStmt>(C)) {
      ToVisit.push(SS->getBody());
    }
    else if (clang::SwitchCase *SC = llvm::dyn_cast<clang::SwitchCase>(C)) {
      ToVisit.push(SC->getSubStmt());
    }
    else if (clang::WhileStmt *WS = llvm::dyn_cast<clang::WhileStmt>(C)) {
      ToVisit.push(WS->getBody());
    }
    else {
      AllPrimitiveChildrenStmts.emplace_back(C);
    }
  }
  return AllPrimitiveChildrenStmts;
}

void Transformation::removeSourceText(const clang::SourceLocation &B,
                                      const clang::SourceLocation &E) {
  llvm::StringRef Text =
      SourceManager::GetSourceText(Context->getSourceManager(), B, E);
  std::string Replacement = "";
  for (auto const &chr : Text) {
    if (chr == '\n')
      Replacement += '\n';
    else if (isprint(chr))
      Replacement += " ";
    else
      Replacement += chr;
  }
  TheRewriter.ReplaceText(clang::SourceRange(B, E), Replacement);
}
