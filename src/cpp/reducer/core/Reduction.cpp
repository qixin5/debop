#include "Reduction.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>

#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Program.h"

#include "OptionManager.h"
#include "Profiler.h"

void Reduction::Initialize(clang::ASTContext &C) {
  Transformation::Initialize(C);
}

bool Reduction::callOracle() {
  Profiler::GetInstance()->beginOracle();
  int Status = llvm::sys::ExecuteAndWait(OptionManager::OracleFile,
                                         {OptionManager::OracleFile});
  Profiler::GetInstance()->endOracle();
  return (Status == 0);
}

float* Reduction::getEvalResult() {
  Profiler::GetInstance()->beginOracle();
  int Status = llvm::sys::ExecuteAndWait(OptionManager::OracleFile,
                                         {OptionManager::OracleFile});
  Profiler::GetInstance()->endOracle();
  if (Status != 0) { return NULL; }

  //Read the first six lines of file
  float* rslt = new float[6];
  rslt[0] = -1; rslt[1] = -1;
  rslt[2] = -1; rslt[3] = -1;
  rslt[4] = -1; rslt[5] = -1;
  std::ifstream infile(OptionManager::EvalResultFile);
  std::string Line0 = "-1", Line1 = "-1", Line2 = "-1", Line3 = "-1", Line4 = "-1", Line5 = "-1";
  if (infile.good()) {
    for (int i=0; i<6; i++) {
      std::string line = "";
      if (std::getline(infile, line)) {	
	if (i == 0) { Line0 = line; }
	else if (i == 1) { Line1 = line; }
	else if (i == 2) { Line2 = line; }
	else if (i == 3) { Line3 = line; }
	else if (i == 4) { Line4 = line; }
	else if (i == 5) { Line5 = line; }
      }
      else {
	break;
      }
    }
  }
  infile.close();
  rslt[0] = std::stof(Line0);
  rslt[1] = std::stof(Line1);
  rslt[2] = std::stof(Line2);
  rslt[3] = std::stof(Line3);
  rslt[4] = std::stof(Line4);
  rslt[5] = std::stof(Line5);
  return rslt;
}

float Reduction::getSizeRScore(float original_size, float reduced_size) {
  float srscore = (original_size - reduced_size) / original_size;
  if (srscore < 0) { srscore = 0; }
  return srscore;
}

float Reduction::getAttackSurfaceRScore(float original_gadget, float reduced_gadget) {
  float arscore = (original_gadget - reduced_gadget) / original_gadget;
  if (arscore < 0) { arscore = 0; }
  return arscore;
}

float Reduction::getRScore(float srscore, float arscore) {
  float alpha = OptionManager::Alpha;
  return (1-alpha) * srscore + alpha * arscore;
}

float Reduction::getGScore(float total_tests, float passed_tests) {
  return passed_tests / total_tests;
}

float Reduction::getOScore(float rscore, float gscore) {
  float beta = OptionManager::Beta;
  return (1-beta) * rscore + beta * gscore;
}

float Reduction::getDScore(float oscore) {
  float kvalue = OptionManager::K;
  return (float) exp(kvalue * oscore);
}

DDElementSet Reduction::toSet(DDElementVector &Vec) {
  DDElementSet S(Vec.begin(), Vec.end());
  return S;
}

DDElementSet Reduction::setDifference(DDElementSet &A, DDElementSet &B) {
  DDElementSet Result;
  std::set_difference(A.begin(), A.end(), B.begin(), B.end(),
                      std::inserter(Result, Result.begin()));
  return Result;
}

DDElementVector Reduction::toVector(DDElementSet &Set) {
  DDElementVector Vec(Set.begin(), Set.end());
  return Vec;
}

std::vector<DDElementVector> Reduction::getCandidates(DDElementVector &Decls,
                                                      int ChunkSize) {
  if (Decls.size() == 1)
    return {Decls};
  std::vector<DDElementVector> Result;
  int Partitions = Decls.size() / ChunkSize;
  for (int Idx = 0; Idx < Partitions; Idx++) {
    DDElementVector Target;
    Target.insert(Target.end(), Decls.begin() + Idx * ChunkSize,
                  Decls.begin() + (Idx + 1) * ChunkSize);
    if (Target.size() > 0)
      Result.emplace_back(Target);
  }
  for (int Idx = 0; Idx < Partitions; Idx++) {
    DDElementVector Complement;
    Complement.insert(Complement.end(), Decls.begin(),
                      Decls.begin() + Idx * ChunkSize);
    Complement.insert(Complement.end(), Decls.begin() + (Idx + 1) * ChunkSize,
                      Decls.end());
    if (Complement.size() > 0)
      Result.emplace_back(Complement);
  }

  if (OptionManager::SkipLearning)
    return Result;
  else {
    arma::uvec ChunkOrder = TheModel.sortCandidates(Decls, Result);
    std::vector<DDElementVector> SortedResult;
    for (int I = 0; I < Result.size(); I++)
      if (ChunkOrder[I] != -1)
        SortedResult.emplace_back(Result[ChunkOrder[I]]);
    return SortedResult;
  }
}

DDElementSet Reduction::doDeltaDebugging(DDElementVector &Decls) {
  Cache.clear();
  DDElementSet Removed;
  DDElementVector DeclsCopy = Decls;

  TheModel.initialize(Decls);

  int ChunkSize = (DeclsCopy.size() + 1) / 2;
  int Iteration = 0;
  spdlog::get("Logger")->info("Running delta debugging - Size: {}",
                              DeclsCopy.size());

  while (DeclsCopy.size() > 0) {
    bool Success = false;
    TheModel.train(Iteration);
    auto Targets = getCandidates(DeclsCopy, ChunkSize);
    for (auto Target : Targets) {
      Iteration++;
      if (std::find(Cache.begin(), Cache.end(), Target) != Cache.end() ||
          isInvalidChunk(Target))
        continue;

      if (!OptionManager::NoCache)
        Cache.insert(Target);

      bool Status = test(Target);
      TheModel.addForTraining(Decls, Target, Status);
      if (Status) {
        auto TargetSet = toSet(Target);
        Removed.insert(TargetSet.begin(), TargetSet.end());
        for (auto T : Target)
          DeclsCopy.erase(std::remove(DeclsCopy.begin(), DeclsCopy.end(), T),
                          DeclsCopy.end());
        Success = true;
        break;
      }
    }
    if (Success) {
      spdlog::get("Logger")->info("Reduced - Size: {}", DeclsCopy.size());
      ChunkSize = (DeclsCopy.size() + 1) / 2;
    } else {
      if (ChunkSize == 1)
        break;
      ChunkSize = (ChunkSize + 1) / 2;
    }
  }
  return Removed;
}
