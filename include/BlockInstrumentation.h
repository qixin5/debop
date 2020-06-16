#ifndef BLOCK_INSTRUMENTATION_H
#define BLOCK_INSTRUMENTATION_H

#include <queue>
#include <vector>

#include "clang/AST/RecursiveASTVisitor.h"

#include "InstruTransformation.h"
#include "RewriterTool.h"

class BlockCollectionVisitor;

/// \brief For statement instrumentation
///
class BlockInstrumentation : public InstruTransformation {
  friend class BlockCollectionVisitor;

public:
   BlockInstrumentation() : CollectionVisitor(NULL) {}
   ~BlockInstrumentation() { delete CollectionVisitor; }

  void Finalize();
   
private:
  void Initialize(clang::ASTContext &Ctx);
  bool HandleTopLevelDecl(clang::DeclGroupRef D);
  void HandleTranslationUnit(clang::ASTContext &Ctx);

  std::set<clang::Stmt *> RemovedElements;
  std::vector<clang::FunctionDecl *> Functions;
  std::queue<clang::Stmt *> Queue;
  
  BlockCollectionVisitor *CollectionVisitor;
  clang::FunctionDecl *CurrentFunction;
};

class BlockCollectionVisitor : public clang::RecursiveASTVisitor<BlockCollectionVisitor> {
public:
  BlockCollectionVisitor(BlockInstrumentation *R, clang::Rewriter& TheRewriter) :
  Consumer(R), RwTool(TheRewriter) {}
  
  //bool shouldTraversePostOrder() const { return true; } //Visit in post order
  bool VisitFunctionDecl(clang::FunctionDecl *FD);
  std::string GetFuncSignature(clang::FunctionDecl* fd);
  bool IsBranchLeaf(const clang::Stmt* stmt);
  std::string GetStmtTypeCountString(const clang::Stmt* stmt);
  int* GetStmtTypeCountForBranch(const clang::CompoundStmt* cs);
  std::string GetExprTypeCountString(const clang::Stmt* stmt);
  int* GetExprTypeCountForBranch(const clang::CompoundStmt* cs);
  void addCount(int* c0, int* c1, int size);
  int* GetExprTypeCount(const clang::Stmt* stmt);
  std::string GetNamesString(const clang::Stmt* stmt);
  void addVectorElems(std::vector<std::string> &vec0, std::vector<std::string> &vec1);
  void GetNamesForBranch(const clang::CompoundStmt* cs, std::vector<std::string> &names);
  void GetNames(const clang::Stmt* stmt, std::vector<std::string> &names);
  std::string GetGotoLabels(const clang::Stmt* stmt);
  void GetGotoLabelsForBranch(const clang::CompoundStmt* cs, std::vector<std::string> &labels);
  std::string GetMetricsString(const clang::Stmt* stmt, int depth, std::string branch_type, std::string label_branch_label);
  void InstruStmt(const clang::Stmt* stmt, std::string src_fname,
                  int depth, std::string branch_type, int par_branch_id,
                  bool add_instru_str_if_compound,
		  std::string par_branch_type,
                  std::string label_branch_label);
  void InstruBranch(const clang::CompoundStmt* cs,
		    std::string src_fname,
                    int depth, std::string branch_type,
		    int par_id, std::string label_branch_label);
  void InsertBefore(const clang::Stmt* stmt, std::string instru_str);
  void InsertAfter(const clang::Stmt* stmt, std::string instru_str);
  bool WriteChangesToFiles();

private:
  BlockInstrumentation *Consumer;
  RewriterTool RwTool;
  int block_count = 0;
};

#endif // BLOCK_INSTRUMENTATION_H
