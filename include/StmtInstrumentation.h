#ifndef STMT_INSTRUMENTATION_H
#define STMT_INSTRUMENTATION_H

#include <queue>
#include <vector>

#include "clang/AST/RecursiveASTVisitor.h"

#include "InstruTransformation.h"
#include "RewriterTool.h"

class StmtCollectionVisitor;

/// \brief For statement instrumentation
///
class StmtInstrumentation : public InstruTransformation {
  friend class StmtCollectionVisitor;

public:
   StmtInstrumentation() : CollectionVisitor(NULL) {}
   ~StmtInstrumentation() { delete CollectionVisitor; }

  void Finalize();
   
private:
  void Initialize(clang::ASTContext &Ctx);
  bool HandleTopLevelDecl(clang::DeclGroupRef D);
  void HandleTranslationUnit(clang::ASTContext &Ctx);

  std::set<clang::Stmt *> RemovedElements;
  std::vector<clang::FunctionDecl *> Functions;
  std::queue<clang::Stmt *> Queue;
  
  StmtCollectionVisitor *CollectionVisitor;
  clang::FunctionDecl *CurrentFunction;
};

class StmtCollectionVisitor : public clang::RecursiveASTVisitor<StmtCollectionVisitor> {
public:
  StmtCollectionVisitor(StmtInstrumentation *R, clang::Rewriter& TheRewriter) :
  Consumer(R), RwTool(TheRewriter) {}
  
  bool shouldTraversePostOrder() const { return true; } //Visit in post order
  bool VisitFunctionDecl(clang::FunctionDecl *FD);
  bool VisitStmt(clang::Stmt* stmt);
  std::string GetFuncSignature(clang::FunctionDecl* fd);
  void InstruNonCompoundStmtAsStmtBody(const clang::Stmt* stmt, std::string src_fname);
  void InstruCompoundStmtAsStmtBody(const clang::CompoundStmt* cs, std::string src_fname);
  void InstruCompoundStmtAsStmtBody(const clang::CompoundStmt* cs, std::string src_fname,
				    bool instru_break);
  void InstruStmt(const clang::Stmt* stmt, std::string src_fname, int is_return_stmt);
  void InstruStmtWithBraces(const clang::Stmt* stmt, std::string src_fname, int is_return_stmt);
  void InstruStmt(const clang::Stmt* stmt, std::string instru_str0, std::string instru_str1);
  bool IsParentStmt(const clang::Stmt* stmt);
  bool WriteChangesToFiles();

private:
  StmtInstrumentation *Consumer;
  RewriterTool RwTool;
  int stmt_count = 0;
};

#endif // STMT_INSTRUMENTATION_H
