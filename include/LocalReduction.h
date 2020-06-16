#ifndef LOCAL_REDUCTION_H
#define LOCAL_REDUCTION_H

#include <queue>
#include <vector>

#include "clang/AST/RecursiveASTVisitor.h"

#include "Reduction.h"

class LocalElementCollectionVisitor;

/// \brief Represents a local reduction phase
///
/// In local reduction phase, local statements are reduced
/// hierarchically with respect to AST.
class LocalReduction : public Reduction {
  friend class LocalElementCollectionVisitor;

public:
  LocalReduction() : CollectionVisitor(NULL) {}
  ~LocalReduction() { delete CollectionVisitor; }

private:
  void Initialize(clang::ASTContext &Ctx);
  bool HandleTopLevelDecl(clang::DeclGroupRef D);
  void HandleTranslationUnit(clang::ASTContext &Ctx);

  std::set<clang::Stmt *> toSet(std::vector<clang::Stmt *> &Vec);
  std::set<clang::Stmt *> setDifference(std::set<clang::Stmt *> &A,
                                        std::set<clang::Stmt *> &B);
  std::set<clang::Decl *> setDifference(std::set<clang::Decl *> &A,
                                        std::set<clang::Decl *> &B);
  static DDElement CastElement(clang::Stmt *S);

  bool callOracle();
  bool test(DDElementVector &ToBeRemoved);
  //const clang::FunctionDecl* getFunctionDeclFromStmt(clang::Stmt* S);
  bool isInvalidChunkByReduction(std::vector<clang::Stmt*>& ToBeRemovedStmts,
				 std::set<clang::Stmt*>& AlreadyRemovedStmts,
				 clang::FunctionDecl* FD);
  bool isInvalidChunkByReverting(std::vector<clang::Stmt*>& ToBeRevertedStmts,
				 std::set<clang::Stmt*>& AlreadyRemovedStmts,
				 clang::FunctionDecl* FD);
  bool isInvalidChunk(DDElementVector &Chunk);
  void filterElements(std::vector<clang::Stmt *> &Vec);

  void doHierarchicalDeltaDebugging(clang::Stmt *S);
  void reduceSwitch(clang::SwitchStmt *SS);
  void reduceIf(clang::IfStmt *IS);
  void reduceWhile(clang::WhileStmt *WS);
  void reduceDoWhile(clang::DoStmt *DS);
  void reduceFor(clang::ForStmt *FS);
  void reduceCompound(clang::CompoundStmt *CS);
  void reduceLabel(clang::LabelStmt *LS);
  void reduceStmt(clang::Stmt *S);
  int countReturnStmts(std::set<clang::Stmt *> &Elements);
  bool noReturn(std::set<clang::Stmt *> &FunctionStmts,
                std::set<clang::Stmt *> &AllRemovedStmts,
		clang::FunctionDecl* FD);
  bool danglingLabel(std::set<clang::Stmt *> &Remaining);
  bool brokenDependency(std::set<clang::Stmt *> &Remaining,
			clang::FunctionDecl* FD);

  std::vector<clang::DeclRefExpr *> getDeclRefExprs(clang::Expr *E);
  std::vector<clang::Stmt *> getBodyStatements(clang::CompoundStmt *CS);

  void addDefUse(clang::DeclRefExpr *DRE, std::set<clang::Decl *> &DU);

  std::set<clang::Stmt *> RemovedElements;
  std::vector<clang::FunctionDecl *> Functions;
  std::queue<clang::Stmt *> Queue;

  LocalElementCollectionVisitor *CollectionVisitor;
  clang::FunctionDecl *CurrentFunction;
};

class LocalElementCollectionVisitor
    : public clang::RecursiveASTVisitor<LocalElementCollectionVisitor> {
public:
  LocalElementCollectionVisitor(LocalReduction *R) : Consumer(R) {}

  bool VisitFunctionDecl(clang::FunctionDecl *FD);

private:
  LocalReduction *Consumer;
};

#endif // LOCAL_REDUCTION_H
