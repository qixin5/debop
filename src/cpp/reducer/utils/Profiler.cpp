#include "Profiler.h"

#include "llvm/Support/Timer.h"

Profiler *Profiler::Instance;

void Profiler::Initialize() {
  Instance = new Profiler();

  Instance->ChiselTimer.init("ChiselTimer", "");
  Instance->OracleTimer.init("OracleTimer", "");
  Instance->LearningTimer.init("LearningTimer", "");
}

Profiler *Profiler::GetInstance() {
  if (!Instance)
    Initialize();
  return Instance;
}

void Profiler::Finalize() { delete Instance; }

void Profiler::incrementGlobalReductionCounter() { GlobalReductionCounter++; }

void Profiler::incrementSuccessfulGlobalReductionCounter() {
  SuccessfulGlobalReductionCounter++;
}

void Profiler::incrementLocalReductionCounter() { LocalReductionCounter++; }

void Profiler::incrementSuccessfulLocalReductionCounter() {
  SuccessfulLocalReductionCounter++;
}

void Profiler::setBestSampleId(int Id) {
  BestSampleId = Id;
}

int Profiler::getBestSampleId() {
  return BestSampleId;
}

void Profiler::setBestSizeRScore(float SRScore) {
  BestSRScore = SRScore;
}

float Profiler::getBestSizeRScore() {
  return BestSRScore;
}

void Profiler::setBestAttackSurfaceRScore(float ARScore) {
  BestARScore = ARScore;
}

float Profiler::getBestAttackSurfaceRScore() {
  return BestARScore;
}

void Profiler::setBestRScore(float RScore) {
  BestRScore = RScore;
}

float Profiler::getBestRScore() {
  return BestRScore;
}

void Profiler::setBestGScore(float GScore) {
  BestGScore = GScore;
}

float Profiler::getBestGScore() {
  return BestGScore;
}

void Profiler::setBestOScore(float OScore) {
  BestOScore = OScore;
}

float Profiler::getBestOScore() {
  return BestOScore;
}

void Profiler::beginChisel() {
  assert(ChiselTimer.isInitialized());
  ChiselTimer.startTimer();
}

void Profiler::endChisel() {
  assert(ChiselTimer.isRunning());
  ChiselTimer.stopTimer();
  ChiselTimeRecord += ChiselTimer.getTotalTime();
  ChiselTimer.clear();
}

void Profiler::beginOracle() {
  assert(OracleTimer.isInitialized());
  OracleTimer.startTimer();
}

void Profiler::endOracle() {
  assert(OracleTimer.isRunning());
  OracleTimer.stopTimer();
  OracleTimeRecord += OracleTimer.getTotalTime();
  OracleTimer.clear();
}

void Profiler::beginLearning() {
  assert(LearningTimer.isInitialized());
  LearningTimer.startTimer();
}

void Profiler::endLearning() {
  assert(LearningTimer.isRunning());
  LearningTimer.stopTimer();
  LearningTimeRecord += LearningTimer.getTotalTime();
  LearningTimer.clear();
}
