#include "LocalReduction.h"

#include <spdlog/spdlog.h>
#include <stdlib.h>
#include <unordered_map>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Analysis/CFG.h>
#include <iostream>
#include <string>
#include <stack>

#include "clang/Lex/Lexer.h"

#include "FileManager.h"
#include "OptionManager.h"
#include "Profiler.h"
#include "SourceManager.h"

using BinaryOperator = clang::BinaryOperator;
using BreakStmt = clang::BreakStmt;
using CallExpr = clang::CallExpr;
using ContinueStmt = clang::ContinueStmt;
using CompoundStmt = clang::CompoundStmt;
using DeclGroupRef = clang::DeclGroupRef;
using DeclStmt = clang::DeclStmt;
using FunctionDecl = clang::FunctionDecl;
using GotoStmt = clang::GotoStmt;
using IfStmt = clang::IfStmt;
using LabelStmt = clang::LabelStmt;
using ReturnStmt = clang::ReturnStmt;
using SourceRange = clang::SourceRange;
using SourceLocation = clang::SourceLocation;
using Stmt = clang::Stmt;
using UnaryOperator = clang::UnaryOperator;
using WhileStmt = clang::WhileStmt;
using VarDecl = clang::VarDecl;
using Decl = clang::Decl;
using LabelDecl = clang::LabelDecl;
using Expr = clang::Expr;
using DeclRefExpr = clang::DeclRefExpr;
using ForStmt = clang::ForStmt;
using SwitchStmt = clang::SwitchStmt;
using DoStmt = clang::DoStmt;

void LocalReduction::Initialize(clang::ASTContext& Ctx) {
	Reduction::Initialize(Ctx);
	CollectionVisitor = new LocalElementCollectionVisitor(this);
}

bool LocalReduction::HandleTopLevelDecl(DeclGroupRef D) {
	clang::SourceManager& SM = Context->getSourceManager();
	for (DeclGroupRef::iterator I = D.begin(), E = D.end(); I != E; ++I) {
		if (SourceManager::IsInHeader(SM, *I))
			continue;
		CollectionVisitor->TraverseDecl(*I);
	}
	return true;
}

void LocalReduction::HandleTranslationUnit(clang::ASTContext& Ctx) {
	bool FilterNullElements = true;
	bool FilterDeclElements = true;
	bool UseBasicBlock = OptionManager::BasicBlock;
	int MAX_SAMPLES = OptionManager::MaxSamples;
	int MAX_ITERS = OptionManager::MaxIters;
	float ALPHA = OptionManager::Alpha;
	float BETA = OptionManager::Beta;
	float K = OptionManager::K;
	float GENFACTOR = OptionManager::GenFactor;
	float ELEM_SELECT_PROB = OptionManager::ElemSelectProb;
	Profiler* Prof = Profiler::GetInstance();

	spdlog::get("Logger")->info("Max Samples: {}", MAX_SAMPLES);
	spdlog::get("Logger")->info("Max Iterations: {}", MAX_ITERS);
	spdlog::get("Logger")->info("Alpha Value: {}", std::to_string(ALPHA));
	spdlog::get("Logger")->info("Beta Value: {}", std::to_string(BETA));
	spdlog::get("Logger")->info("K Value: {}", std::to_string(K));
	spdlog::get("Logger")->info("Generality Factor: {}", std::to_string(GENFACTOR));
	spdlog::get("Logger")->info("Element Select Probability: {}", std::to_string(ELEM_SELECT_PROB));

	std::set<Stmt *> GotoElements;//Save the separate gotoStmt
	std::vector<Stmt *> ProgElements;//Save all the progElements
	std::vector<FunctionDecl*> ProgFuncDecls;
	std::unordered_map<int, int> elem_to_blk;//Save the mapping relationship between elements and basic-blocks
	std::unordered_map<int, float> sample_to_oscore;

	const clang::SourceManager& SM = Context->getSourceManager();
	int fd_idx = 0;
	//Get all goto stmts
    for (auto const& FD : Functions) {
		if (clang::CompoundStmt* FDBodyCS = llvm::dyn_cast<clang::CompoundStmt>(FD->getBody())) {
			std::vector<Stmt*> AllPrimitiveChildrenStmts = getAllPrimitiveChildrenStmts(FDBodyCS);
		    if (!AllPrimitiveChildrenStmts.empty()) {
				for (auto const& S : AllPrimitiveChildrenStmts) {
					if(clang::GotoStmt* GS = llvm::dyn_cast<clang::GotoStmt>(S)){
						GotoElements.insert(S);
					}
				}
			}
		}
		ProgFuncDecls.emplace_back(FD);
		fd_idx += 1;
	}

	//Get all basic-blcoks
	int elem_idx = 0, blk_idx = 0;
	for (auto const& FD : Functions) {
		Stmt* funcBody = FD->getBody();
		std::unique_ptr< clang::CFG > sourceCFG = clang::CFG::buildCFG(FD, funcBody, Context, clang::CFG::BuildOptions());
		for (clang::CFGBlock* blk : *sourceCFG){
			//Check whether the block is actually empty
			if(blk->empty())
		        continue;
			Stmt* termCond = blk->getTerminatorCondition();

			int maxSize = blk->size();
			int elem = 1;

			bool printedBlock = false;//Make sure that the block number is printed only once
			bool BlockNotEmpty = false;//Check whether the block is empty after ignoring DeclStmts and other stmts

  			for (auto E : *blk) {
				//if this block has a terminator and the terminator has a condition, ignore it's last CFGElement
				if(termCond != nullptr){
				    if(elem == maxSize){
				        continue;
				    }
				    elem++;
				}				

     			//Get the statement
				clang::CFGStmt CS = E.castAs<clang::CFGStmt>();	
				Stmt *S = const_cast<Stmt*>(CS.getStmt());

				//Ignore DeclStmt
				std::string className = S->getStmtClassName();
				if(className == "DeclStmt"){
				    continue;
				}

				//If it's type is CallExp, check whether it has a primitive parent
				if(className == "CallExpr"){
					const auto& parents = Context->getParents(*S);
					const Stmt *paStmt = parents[0].get<Stmt>();
					std::string paName = paStmt->getStmtClassName();
					if(!(paName == "CompoundStmt")){
						continue;
					}
				}

				//One block ONLY prints once!
				if(printedBlock==false){
					spdlog::get("Logger")->info("Block: {}", blk_idx);
					printedBlock =true;
				}
	
				clang::SourceLocation Start = SourceManager::GetBeginOfStmt(Context, S);
				clang::SourceLocation End = SourceManager::GetEndOfStmt(Context, S);
				if (End.isInvalid() || Start.isInvalid()) {
					spdlog::get("Logger")->info("ProgElement: Location Invalid");
					continue;
				}
				else {
					llvm::StringRef SStr = SourceManager::GetSourceText(SM, Start, End);
					spdlog::get("Logger")->info("ProgElement {}: {}", std::to_string(elem_idx), SStr.str());
				}

				//Save the ProgElements
				ProgElements.emplace_back(S);
				//Save idx to the map
				elem_to_blk[elem_idx] = blk_idx;
				elem_idx++;
				BlockNotEmpty = true;
			}

			//Check wheher this block has a goto Stmt as terminator, if it has goto, print the goto terminator
			clang::CFGTerminator term = blk->getTerminator();
			Stmt *termStmt = term.getStmt();

			if(termStmt != nullptr){
			    std::string termClassName = termStmt->getStmtClassName();
			    if(termClassName == "GotoStmt"){
				//Delete the terminator from GotoElements
				for (auto gototerm : GotoElements) {
					clang::GotoStmt* GS = llvm::dyn_cast<clang::GotoStmt>(termStmt);
					clang::GotoStmt* GGS = llvm::dyn_cast<clang::GotoStmt>(gototerm);
					if (GGS->getBeginLoc()==GS->getBeginLoc()) {
						GotoElements.erase(gototerm);
						break;
					}
				}

				//Print the goto terminator
				clang::SourceLocation Start = SourceManager::GetBeginOfStmt(Context, termStmt);
				clang::SourceLocation End = SourceManager::GetEndOfStmt(Context, termStmt);
				if (End.isInvalid() || Start.isInvalid()) {
					spdlog::get("Logger")->info("ProgElement: Location Invalid");
					continue;
				}
				else {
					llvm::StringRef SStr = SourceManager::GetSourceText(SM, Start, End);
					spdlog::get("Logger")->info("ProgElement {}: {}", std::to_string(elem_idx), SStr.str());
				}
				//Save the goto terminator
				ProgElements.emplace_back(termStmt);
				//Save idx to the map
				elem_to_blk[elem_idx] = blk_idx;
				elem_idx++;
				}
			}
			if(BlockNotEmpty){
				blk_idx++;
			}
		}
	}

	//Save the goto stmts
	for (auto const& gototerm : GotoElements) {
		spdlog::get("Logger")->info("Block: {}", blk_idx);
		clang::SourceLocation Start = SourceManager::GetBeginOfStmt(Context, gototerm);
		clang::SourceLocation End = SourceManager::GetEndOfStmt(Context, gototerm);
		if (End.isInvalid() || Start.isInvalid()) {
			spdlog::get("Logger")->info("ProgElement: Location Invalid");
			continue;
		}
		else {
			llvm::StringRef SStr = SourceManager::GetSourceText(SM, Start, End);
			spdlog::get("Logger")->info("ProgElement {}: {}", std::to_string(elem_idx), SStr.str());
		}
		//Save the ProgElements
		ProgElements.emplace_back(gototerm);
		//Save idx to the map
		elem_to_blk[elem_idx] = blk_idx;
		elem_idx++;
		blk_idx++;
	}

	float last_srscore = -1, best_srscore = -1;
	float last_arscore = -1, best_arscore = -1;
	float last_rscore = -1, best_rscore = -1; //best_rscore is rscore for sample with best oscore
	float last_gscore = -1, best_gscore = -1; //best_gscore is gscore for sample with best oscore
	float last_oscore = -1, best_oscore = -1;
	float last_dscore = -1, best_dscore = -1;

	//Evaluate the original program
	float* eval_rslt0 = Reduction::getEvalResult();
	if (eval_rslt0 != NULL) {
		last_srscore = (eval_rslt0[0] == -1 || eval_rslt0[1] == -1) ? -1 :
			Reduction::getSizeRScore(eval_rslt0[0], eval_rslt0[1]);

		last_arscore = (eval_rslt0[2] == -1 || eval_rslt0[3] == -1) ? -1 :
			Reduction::getAttackSurfaceRScore(eval_rslt0[2], eval_rslt0[3]);

		last_rscore = (last_srscore == -1 || last_arscore == -1) ? -1 :
			Reduction::getRScore(last_srscore, last_arscore);

		last_gscore = (eval_rslt0[4] == -1 || eval_rslt0[5] == -1) ? -1 :
			Reduction::getGScore(eval_rslt0[4], eval_rslt0[5]);

		if (last_gscore != -1) { last_gscore *= GENFACTOR; }

		last_oscore = (last_rscore == -1 || last_gscore == -1) ? -1 :
			Reduction::getOScore(last_rscore, last_gscore);

		last_dscore = (last_oscore == -1) ? -1 :
			Reduction::getDScore(last_oscore);

		best_srscore = last_srscore;
		best_arscore = last_arscore;
		best_rscore = last_rscore;
		best_gscore = last_gscore;
		best_oscore = last_oscore;
		best_dscore = last_dscore;

		Prof->setBestSampleId(-1); //-1 means initial
		Prof->setBestSizeRScore(best_srscore);
		Prof->setBestAttackSurfaceRScore(best_arscore);
		Prof->setBestRScore(best_rscore);
		Prof->setBestGScore(best_gscore);
		Prof->setBestOScore(best_oscore);

		delete[] eval_rslt0;
	}
	else {
		spdlog::get("Logger")->info("Compiling Failure for Original Program. Abort");
		return;
	}

	spdlog::get("Logger")->info("");
	spdlog::get("Logger")->info("Initial SR-Score: {}; Initial AR-Score: {}; Initial R-Score: {}; Initial G-Score: {}; Initial O-Score: {}; Initial D-Score: {}",
		std::to_string(last_srscore), std::to_string(last_arscore),
		std::to_string(last_rscore), std::to_string(last_gscore),
		std::to_string(last_oscore), std::to_string(last_dscore));

	//Do MCMC
	std::set<clang::Stmt*> CurrRemovedStmts;
	int curr_samples = 0, curr_iter = 0; //No iter can cause non-termination when the reduced program becomes small enough.
	int best_sample_id = -1;

	while (curr_samples < MAX_SAMPLES && curr_iter < MAX_ITERS) {
		spdlog::get("Logger")->info("");
		spdlog::get("Logger")->info("Current Iteration: {}; Current Samples: {}",
			std::to_string(curr_iter),
			std::to_string(curr_samples));
		spdlog::get("Logger")->info("Current Best Sample Id: {}", best_sample_id);
		spdlog::get("Logger")->info("Current Best SR-Score: {}; AR-Score: {}; R-Score: {}; G-Score: {}; O-Score: {}; D-Score: {}",
			std::to_string(best_srscore), std::to_string(best_arscore),
			std::to_string(best_rscore), std::to_string(best_gscore),
			std::to_string(best_oscore), std::to_string(best_dscore));

		curr_iter += 1;

		
		std::vector<int> Selected_blk_Indices; //Record the selected blk
		std::vector<int> Selected_Indices; //Record the selected program elements
		std::vector<llvm::StringRef> Selected_Reverts;

		//If Do MCMC at the BasicBlock Level
		if (UseBasicBlock) {
			if (ELEM_SELECT_PROB == -1) { //Randomly select ONE Block
				int selected_blk_idx = rand() % blk_idx;
				Selected_blk_Indices.emplace_back(selected_blk_idx);
				//Get stmts in block
				for (auto iter = elem_to_blk.begin(); iter != elem_to_blk.end(); ++iter) {
					if (iter->second == selected_blk_idx) {
						Selected_Indices.emplace_back(iter->first);
						Stmt* TStmt = ProgElements[iter->first];
						//Record the statement's text:                                                              
						//(1) Get locations for Selected Stmt
						SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
						SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);
						if (BeginStmt.isInvalid() || EndStmt.isInvalid()) {
							spdlog::get("Logger")->warn("Invalid location for Selected Stmt:\nBeginStmt: {}\nEndStmt: {}", BeginStmt.printToString(SM), EndStmt.printToString(SM));
							continue; //To another Stmt
						}
						//(2) Save its text
						llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginStmt, EndStmt);
						Selected_Reverts.emplace_back(Revert);
					}
				}
			}
			else {
				for (int i = 0; i < blk_idx; i++) {
					float select_prob = ((float)rand()) / RAND_MAX;
					if (select_prob < ELEM_SELECT_PROB) { //Select this element
						Selected_blk_Indices.emplace_back(i);

						//Get stmts in block
						for (auto iter = elem_to_blk.begin(); iter != elem_to_blk.end(); ++iter) {
							if (iter->second == i) {
								Selected_Indices.emplace_back(iter->first);
								Stmt* TStmt = ProgElements[iter->first];
								//Record the statement's text:                                                              
								//(1) Get locations for Selected Stmt
								SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
								SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);
								if (BeginStmt.isInvalid() || EndStmt.isInvalid()) {
									spdlog::get("Logger")->warn("Invalid location for Selected Stmt:\nBeginStmt: {}\nEndStmt: {}", BeginStmt.printToString(SM), EndStmt.printToString(SM));
									continue; //To another Stmt
								}
								//(2) Save its text
								llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginStmt, EndStmt);
								Selected_Reverts.emplace_back(Revert);
							}
						}
					}
				}
			}
		}//Else do MCMC at the statement level
		else {
			if (ELEM_SELECT_PROB == -1) { //Randomly select ONE element ONLY
				int selected_idx = rand() % elem_idx;
				Selected_Indices.emplace_back(selected_idx);

				Stmt* TStmt = ProgElements[selected_idx];
				//Record the statement's text:                                                              
				//(1) Get locations for Selected Stmt
				SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
				SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);
				if (BeginStmt.isInvalid() || EndStmt.isInvalid()) {
					spdlog::get("Logger")->warn("Invalid location for Selected Stmt:\nBeginStmt: {}\nEndStmt: {}", BeginStmt.printToString(SM), EndStmt.printToString(SM));
					continue; //To another iteration
				}
				//(2) Save its text
				llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginStmt, EndStmt);
				Selected_Reverts.emplace_back(Revert);
			}
			else {
				for (int i = 0; i < elem_idx; i++) {
					float select_prob = ((float)rand()) / RAND_MAX;
					if (select_prob < ELEM_SELECT_PROB) { //Select this element
						Selected_Indices.emplace_back(i);

						Stmt* TStmt = ProgElements[i];
						//Record the statement's text:
						//(1) Get locations for Selected Stmt
						SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
						SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);
						if (BeginStmt.isInvalid() || EndStmt.isInvalid()) {
							spdlog::get("Logger")->warn("Invalid location for Selected Stmt:\nBeginStmt: {}\nEndStmt: {}", BeginStmt.printToString(SM), EndStmt.printToString(SM));
							continue;
						}
						//(2) Save its text
						llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginStmt, EndStmt);
						Selected_Reverts.emplace_back(Revert);
					}
				}
			}
		}
		

		//Print Selected Element Info
		int selected_element_num = Selected_Indices.size();
		for (int i = 0; i < selected_element_num; i++) {
			spdlog::get("Logger")->info("Selected ProgElement Index: {}", std::to_string(Selected_Indices[i]));
		}

		//*********************************
		//Temporarily ignored static checks
		//*********************************

		//Make the changes by either reducing or adding back the selected elements
		for (int i = 0; i < selected_element_num; i++) {
			Stmt* TStmt = ProgElements[Selected_Indices[i]];
			SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
			SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);

			if (CurrRemovedStmts.find(TStmt) == CurrRemovedStmts.end()) { //Reduce it
				spdlog::get("Logger")->info("Reducing ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
				removeSourceText(BeginStmt, EndStmt);
			}
			else { //Add it back
				spdlog::get("Logger")->info("Adding back ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
				TheRewriter.ReplaceText(SourceRange(BeginStmt, EndStmt), Selected_Reverts[i]);
			}
		}
		TheRewriter.overwriteChangedFiles();


		//Compile, run tests, and get o-score
		float* eval_rslt = Reduction::getEvalResult();
		float srscore = -1, arscore = -1, rscore = -1, gscore = -1, oscore = -1, dscore = -1;
		if (eval_rslt != NULL) {
			srscore = (eval_rslt[0] == -1 || eval_rslt[1] == -1) ? -1 :
				Reduction::getSizeRScore(eval_rslt[0], eval_rslt[1]);

			arscore = (eval_rslt[2] == -1 || eval_rslt[3] == -1) ? -1 :
				Reduction::getAttackSurfaceRScore(eval_rslt[2], eval_rslt[3]);

			rscore = (srscore == -1 || arscore == -1) ? -1 :
				Reduction::getRScore(srscore, arscore);

			gscore = (eval_rslt[4] == -1 || eval_rslt[5] == -1) ? -1 :
				Reduction::getGScore(eval_rslt[4], eval_rslt[5]);

			if (gscore != -1) { gscore *= GENFACTOR; }

			oscore = (rscore == -1 || gscore == -1) ? -1 : Reduction::getOScore(rscore, gscore);

			dscore = (oscore == -1) ? -1 : Reduction::getDScore(oscore);
		}

		bool revert_all = false;
		if (eval_rslt == NULL) { //Compiling error
			spdlog::get("Logger")->info("Compiling Failure. Revert");
			revert_all = true;
		}
		else if (rscore < 0 || gscore < 0 || oscore < 0) {
			spdlog::get("Logger")->info("Getting Evaluation Result Failure. Revert");
			revert_all = true;
		}
		if (revert_all) {
			for (int i = 0; i < selected_element_num; i++) {
				Stmt* TStmt = ProgElements[Selected_Indices[i]];
				SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
				SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);

				if (CurrRemovedStmts.find(TStmt) == CurrRemovedStmts.end()) { //Revert by adding back
					spdlog::get("Logger")->info("Reverting by Adding back ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
					TheRewriter.ReplaceText(SourceRange(BeginStmt, EndStmt), Selected_Reverts[i]); //Revert the change
				}
				else { //Revert by reducing
					spdlog::get("Logger")->info("Reverting by Reducing ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
					removeSourceText(BeginStmt, EndStmt);
				}
			}
			TheRewriter.overwriteChangedFiles();
		}
		else {
			spdlog::get("Logger")->info("Sample Id: {}", std::to_string(curr_samples));
			spdlog::get("Logger")->info("SR-Score: {}; AR-Score: {}; R-Score: {}; G-Score: {}; O-Score: {}; D-Score: {}",
				std::to_string(srscore), std::to_string(arscore),
				std::to_string(rscore), std::to_string(gscore),
				std::to_string(oscore), std::to_string(dscore));
			spdlog::get("Logger")->info("Last SR-Score: {}; Last AR-Score: {}; Last R-Score: {}; Last G-Score: {}; Last O-Score: {}; Last D-Score: {}",
				std::to_string(last_srscore), std::to_string(last_arscore),
				std::to_string(last_rscore), std::to_string(last_gscore),
				std::to_string(last_oscore), std::to_string(last_dscore));
			bool accepted = false;
			if (dscore >= last_dscore) { accepted = true; }
			else {
				float r = ((float)rand()) / RAND_MAX;
				spdlog::get("Logger")->info("Random Prob Generated: {}", std::to_string(r));
				spdlog::get("Logger")->info("(DScore / LastDScore): {}", std::to_string(dscore / last_dscore));
				if (r < (dscore / last_dscore)) { accepted = true; }
			}

			if (accepted) {
				spdlog::get("Logger")->info("Accepted? Yes");
				//Update CurrRemovedStmts
				for (int i = 0; i < selected_element_num; i++) {
					Stmt* TStmt = ProgElements[Selected_Indices[i]];
					if (CurrRemovedStmts.find(TStmt) == CurrRemovedStmts.end()) {
						CurrRemovedStmts.insert(TStmt);
					}
					else {
						CurrRemovedStmts.erase(TStmt);
					}
				}

				std::string SampleId = "sample" + std::to_string(curr_samples);
				FileManager::GetInstance()->saveSample(SampleId); //Save the sample program
				last_srscore = srscore;
				last_arscore = arscore;
				last_rscore = rscore;
				last_gscore = gscore;
				last_oscore = oscore;
				last_dscore = dscore;

				//Save the best
				if (oscore >= best_oscore) {
					best_srscore = srscore;
					best_arscore = arscore;
					best_rscore = rscore;
					best_gscore = gscore;
					best_oscore = oscore;
					best_dscore = dscore;
					best_sample_id = curr_samples;
					Prof->setBestSampleId(best_sample_id);
					Prof->setBestSizeRScore(best_srscore);
					Prof->setBestAttackSurfaceRScore(best_arscore);
					Prof->setBestRScore(best_rscore);
					Prof->setBestGScore(best_gscore);
					Prof->setBestOScore(best_oscore);
				}

				//Increase counter
				curr_samples += 1;
			}
			else {
				spdlog::get("Logger")->info("Accepted? No");
				spdlog::get("Logger")->info("Revert");
				for (int i = 0; i < selected_element_num; i++) {
					Stmt* TStmt = ProgElements[Selected_Indices[i]];
					SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, TStmt);
					SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, TStmt);

					if (CurrRemovedStmts.find(TStmt) == CurrRemovedStmts.end()) { //Revert by adding back
						spdlog::get("Logger")->info("Reverting by Adding back ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
						TheRewriter.ReplaceText(SourceRange(BeginStmt, EndStmt), Selected_Reverts[i]); //Revert the change
					}
					else { //Revert by reducing
						spdlog::get("Logger")->info("Reverting by Reducing ProgElement of Index: {}", std::to_string(Selected_Indices[i]));
						removeSourceText(BeginStmt, EndStmt);
					}
				}
				TheRewriter.overwriteChangedFiles();
			}
		}
		delete[] eval_rslt;
	}
}

void LocalReduction::reduceStmt(Stmt* S) {
	const clang::SourceManager& SM = Context->getSourceManager();
	SourceLocation BeginStmt = SourceManager::GetBeginOfStmt(Context, S);
	SourceLocation EndStmt = SourceManager::GetEndOfStmt(Context, S);

	if (BeginStmt.isInvalid() || EndStmt.isInvalid()) {
		spdlog::get("Logger")->warn("Invalid location:\nBeginStmt: {}\nEndStmt: {}\n",
			BeginStmt.printToString(SM),
			EndStmt.printToString(SM));
		return;
	}

	llvm::StringRef Revert =
		SourceManager::GetSourceText(SM, BeginStmt, EndStmt);

	RemovedElements.insert(S);
	removeSourceText(BeginStmt, EndStmt);
	TheRewriter.overwriteChangedFiles();
}

DDElement LocalReduction::CastElement(Stmt* S) { return S; }

bool LocalReduction::callOracle() {
	Profiler::GetInstance()->incrementLocalReductionCounter();

	if (Reduction::callOracle()) {
		Profiler::GetInstance()->incrementSuccessfulLocalReductionCounter();
		FileManager::GetInstance()->saveTemp("local", true);
		return true;
	}
	else {
		FileManager::GetInstance()->saveTemp("local", false);
		return false;
	}
}

/* Modify program by removing elements in ToBeRemoved. Then test the program,
   if successful, save these elements in RemovedElements. Otherwise, revert. */
bool LocalReduction::test(DDElementVector& ToBeRemoved) {
	const clang::SourceManager& SM = Context->getSourceManager();
	std::vector<clang::SourceRange> Ranges;
	std::vector<llvm::StringRef> Reverts;

	for (auto Element : ToBeRemoved) {
		if (Element.isNull())
			continue;

		clang::Stmt* S = Element.get<Stmt*>();
		clang::SourceLocation Start = SourceManager::GetBeginOfStmt(Context, S);
		clang::SourceLocation End = SourceManager::GetEndOfStmt(Context, S);

		if (End.isInvalid() || Start.isInvalid())
			return false;

		SourceRange Range(Start, End);
		Ranges.emplace_back(Range);
		llvm::StringRef Revert = SourceManager::GetSourceText(SM, Start, End);
		Reverts.emplace_back(Revert);
		removeSourceText(Start, End);
	}
	TheRewriter.overwriteChangedFiles();
	if (callOracle()) {
		for (auto& E : ToBeRemoved) {
			auto Children = getAllChildren(E.get<Stmt*>());
			RemovedElements.insert(Children.begin(), Children.end());
		}
		return true;
	}
	else {
		for (int i = 0; i < Reverts.size(); i++)
			TheRewriter.ReplaceText(Ranges[i], Reverts[i]);
		TheRewriter.overwriteChangedFiles();
		return false;
	}

	return false;
}

int LocalReduction::countReturnStmts(std::set<Stmt*>& Elements) {
	int NumReturns = 0;
	for (auto const& E : Elements)
		if (ReturnStmt* RS = llvm::dyn_cast<ReturnStmt>(E))
			NumReturns++;
	return NumReturns;
}

bool LocalReduction::noReturn(std::set<Stmt*>& FunctionStmts,
	std::set<Stmt*>& AllRemovedStmts,
	FunctionDecl* FD) {
	//================
	//spdlog::get("Logger")->info("noReturn executed");
	//================
	if (FD->getReturnType().getTypePtr()->isVoidType())
		return false;
	int FunctionReturns = countReturnStmts(FunctionStmts);
	int RemovedReturns = countReturnStmts(AllRemovedStmts);
	if (FunctionReturns == 0 || RemovedReturns == 0)
		return false;
	if (countReturnStmts(FunctionStmts) == countReturnStmts(AllRemovedStmts))
		return true;
	return false;
}

bool LocalReduction::danglingLabel(std::set<Stmt*>& Remaining) {
	//================
	//spdlog::get("Logger")->info("danglingLabel executed");
	//================  
	std::set<LabelStmt*> LabelDefs;
	std::set<LabelStmt*> LabelUses;

	for (auto const& S : Remaining) {
		if (!S)
			continue;
		if (GotoStmt* GS = llvm::dyn_cast<GotoStmt>(S))
			LabelUses.insert(GS->getLabel()->getStmt());
		else if (LabelStmt* LS = llvm::dyn_cast<LabelStmt>(S))
			LabelDefs.insert(LS);
	}

	return !(std::includes(LabelDefs.begin(), LabelDefs.end(), LabelUses.begin(),
		LabelUses.end()));
}

std::vector<DeclRefExpr*> LocalReduction::getDeclRefExprs(Expr* E) {
	std::vector<DeclRefExpr*> result;
	std::vector<Stmt*> Children = getAllChildren(E);
	for (auto const& S : Children) {
		if (!S)
			continue;
		if (DeclRefExpr* DRE = llvm::dyn_cast<DeclRefExpr>(S))
			result.emplace_back(DRE);
	}
	return result;
}

void LocalReduction::addDefUse(DeclRefExpr* DRE, std::set<Decl*>& DU) {
	if (VarDecl* VD = llvm::dyn_cast<VarDecl>(DRE->getDecl())) {
		if (auto T = llvm::dyn_cast_or_null<clang::ConstantArrayType>(
			VD->getType().getTypePtr()))
			return;
		if (VD->isLocalVarDeclOrParm() || VD->isStaticLocal()) {
			//==============
			//const clang::SourceManager &SM = Context->getSourceManager();
			//llvm::StringRef RStr = SourceManager::GetSourceText(SM, DRE->getDecl()->getSourceRange());
			//spdlog::get("Logger")->info("Ref: {}", RStr.str());
			//==============      
			DU.insert(DRE->getDecl());
		}
	}
}

bool LocalReduction::brokenDependency(std::set<Stmt*>& Remaining, FunctionDecl* FD) {
	//================
	//const clang::SourceManager &SM = Context->getSourceManager();
	//================  
	std::set<Decl*> Defs, Uses;
	for (auto const& S : Remaining) {
		if (!S)
			continue;
		if (BinaryOperator* BO = llvm::dyn_cast<BinaryOperator>(S)) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, BO->getSourceRange());
			//spdlog::get("Logger")->info("BinaryOperator Original: {}", OStr.str());
			//==============      
			if (BO->isCompoundAssignmentOp() || BO->isShiftAssignOp()) {
				for (auto C : getDeclRefExprs(BO->getLHS())) {
					//spdlog::get("Logger")->info("Added to Defs");
					addDefUse(C, Defs);
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
				for (auto C : getDeclRefExprs(BO->getRHS())) {
					//spdlog::get("Logger")->info("Added to Uses");	  
					addDefUse(C, Uses);
				}
			}
			else if (BO->isAssignmentOp()) {
				for (auto C : getDeclRefExprs(BO->getLHS())) {
					//spdlog::get("Logger")->info("Added to Defs");
					addDefUse(C, Defs);
				}
				for (auto C : getDeclRefExprs(BO->getRHS())) {
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
			}
			else {
				for (auto C : getDeclRefExprs(BO->getLHS())) {
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
				for (auto C : getDeclRefExprs(BO->getRHS())) {
					//spdlog::get("Logger")->info("Added to Uses");	  
					addDefUse(C, Uses);
				}
			}
		}
		else if (UnaryOperator* UO = llvm::dyn_cast<UnaryOperator>(S)) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, UO->getSourceRange());
			//spdlog::get("Logger")->info("UnaryOperator Original: {}", OStr.str());
			//==============      
			switch (UO->getOpcode()) {
			case clang::UO_PostInc:
			case clang::UO_PostDec:
			case clang::UO_PreInc:
			case clang::UO_PreDec:
				for (auto C : getDeclRefExprs(UO->getSubExpr())) {
					//spdlog::get("Logger")->info("Added to Defs");
					addDefUse(C, Defs);
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
				break;
			case clang::UO_AddrOf:
				for (auto C : getDeclRefExprs(UO->getSubExpr())) {
					//spdlog::get("Logger")->info("Added to Defs");
					addDefUse(C, Defs);
				}
				break;
			case clang::UO_Plus:
			case clang::UO_Minus:
			case clang::UO_Not:
			case clang::UO_Deref:
			case clang::UO_LNot:
				for (auto C : getDeclRefExprs(UO->getSubExpr())) {
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
			}
		}
		else if (DeclStmt* DS = llvm::dyn_cast<DeclStmt>(S)) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, DS->getSourceRange());
			//spdlog::get("Logger")->info("DeclStmt Original: {}", OStr.str());
			//==============      
			for (auto D : DS->decls()) {
				if (VarDecl* VD = llvm::dyn_cast<VarDecl>(D)) {
					if (VD->hasInit()) {
						//==============
						//llvm::StringRef RStr = SourceManager::GetSourceText(SM, D->getSourceRange());
						//spdlog::get("Logger")->info("Added to Defs");
						//spdlog::get("Logger")->info("Ref: {}", RStr.str());
						//==============
						Defs.insert(D);
					}
				}
			}
		}
		else if (CallExpr* CE = llvm::dyn_cast<CallExpr>(S)) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, CE->getSourceRange());
			//spdlog::get("Logger")->info("CallExpr Original: {}", OStr.str());
			//==============      
			for (int I = 0; I < CE->getNumArgs(); I++) {
				for (auto C : getDeclRefExprs(CE->getArg(I))) {
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
			}
		}
		else if (ReturnStmt* RS = llvm::dyn_cast<ReturnStmt>(S)) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, RS->getSourceRange());
			//spdlog::get("Logger")->info("ReturnStmt Original: {}", OStr.str());
			//==============      
			if (!FD->getReturnType().getTypePtr()->isVoidType()) {
				for (auto C : getDeclRefExprs(RS->getRetValue())) {
					//spdlog::get("Logger")->info("Added to Uses");
					addDefUse(C, Uses);
				}
			}
		}
		for (auto P : FD->parameters()) {
			//==============
			//llvm::StringRef OStr = SourceManager::GetSourceText(SM, P->getSourceRange());
			//spdlog::get("Logger")->info("FD Parameter Original: {}", OStr.str());
			//spdlog::get("Logger")->info("Added to Defs");
			//==============      
			Defs.insert(P);
		}
	}

	//===============
	/*
	for (auto Def : Defs) {
	  llvm::StringRef DStr = SourceManager::GetSourceText(SM, Def->getSourceRange());
	  spdlog::get("Logger")->info("Def Decl {}", DStr.str());
	}
	for (auto Use : Uses) {
	  llvm::StringRef UStr = SourceManager::GetSourceText(SM, Use->getSourceRange());
	  spdlog::get("Logger")->info("Use Decl {}", UStr.str());
	}
	std::set<Decl*> DiffSet = setDifference(Uses, Defs);
	for (auto DiffElement : DiffSet) {
	  llvm::StringRef DStr = SourceManager::GetSourceText(SM, DiffElement->getSourceRange());
	  spdlog::get("Logger")->info("DefUse Diff Decl {}", DStr.str());
	}
	bool DefUseIncludes = std::includes(Defs.begin(), Defs.end(), Uses.begin(), Uses.end());
	if (DefUseIncludes) spdlog::get("Logger")->info("DefUse Include Yes");
	else spdlog::get("Logger")->info("DefUse Include No");
	*/
	//===============
	return !(std::includes(Defs.begin(), Defs.end(), Uses.begin(), Uses.end()));
}

std::set<Stmt*> LocalReduction::toSet(std::vector<Stmt*>& Vec) {
	std::set<Stmt*> S(Vec.begin(), Vec.end());
	return S;
}

std::set<Stmt*> LocalReduction::setDifference(std::set<Stmt*>& A,
	std::set<Stmt*>& B) {
	std::set<Stmt*> Result;
	std::set_difference(A.begin(), A.end(), B.begin(), B.end(),
		std::inserter(Result, Result.begin()));
	return Result;
}

std::set<Decl*> LocalReduction::setDifference(std::set<Decl*>& A,
	std::set<Decl*>& B) {
	std::set<Decl*> Result;
	std::set_difference(A.begin(), A.end(), B.begin(), B.end(),
		std::inserter(Result, Result.begin()));
	return Result;
}


/* I had problems of using const for the following function ... */
/*
const FunctionDecl* LocalReduction::getFunctionDeclFromStmt(const Stmt* S) {
  for (const auto &I : Context->getParents(S)) {
	const Decl* D = I.get<Decl>();
	if (!D) continue;
	if (const FunctionDecl *FD = llvm::dyn_cast<FunctionDecl>(D)) return FD;
  }
  return NULL;
} */

/* Used for checking the program validity after reducing stmts.
   ToBeRemovedStmts need to be all from the same function FD. */
bool LocalReduction::isInvalidChunkByReduction(std::vector<Stmt*>& ToBeRemovedStmts,
	std::set<Stmt*>& AlreadyRemovedStmts,
	FunctionDecl* FD) {
	if (OptionManager::SkipLocalDep)
		return false;
	//Generate three sets of children
	std::vector<Stmt*> FunctionChildren = getAllChildren(FD->getBody());
	std::vector<Stmt*> ToBeRemovedChildren; //This saves to-be-removed children from ToBeRemovedStmts
	for (auto ToBeRemovedStmt : ToBeRemovedStmts) {
		auto Children = getAllChildren(ToBeRemovedStmt);
		ToBeRemovedChildren.insert(ToBeRemovedChildren.end(), Children.begin(), Children.end());
	}
	std::vector<Stmt*> AlreadyRemovedChildren; //This saves already-removed children from AlreadyRemovedStmts
	for (auto AlreadyRemovedStmt : AlreadyRemovedStmts) {
		auto Children = getAllChildren(AlreadyRemovedStmt);
		AlreadyRemovedChildren.insert(AlreadyRemovedChildren.end(), Children.begin(), Children.end());
	}

	//================
	//spdlog::get("Logger")->info("isInvalidChunkByReduction: Done Generating Three Sets of Children.");
	//================  

	//Compute FSet, ASet, & Remaining
	auto TempFSet = toSet(FunctionChildren);
	auto RSet = toSet(AlreadyRemovedChildren);
	auto FSet = setDifference(TempFSet, RSet);
	auto ASet = toSet(ToBeRemovedChildren);
	auto Remaining = setDifference(FSet, ASet);

	//================
	//spdlog::get("Logger")->info("isInvalidChunkByReduction: Done Generating FSet, ASet, & Remaining.");
	//================  

	if (noReturn(FSet, ASet, FD)) { //This means all return statements are removed, which is bad.
		spdlog::get("Logger")->info("Invalid Due To No Return");
		return true;
	}
	if (danglingLabel(Remaining)) {
		spdlog::get("Logger")->info("Invalid Due To Dangling Label");
		return true;
	}
	if (brokenDependency(Remaining, FD)) {
		spdlog::get("Logger")->info("Invalid Due To Broken Dependency");
		return true;
	}
	return false;
}

/* Used for checking the program validity after reverting stmts.
   ToBeRevertedStmts need to be all from the same function FD. */
bool LocalReduction::isInvalidChunkByReverting(std::vector<Stmt*>& ToBeRevertedStmts,
	std::set<Stmt*>& AlreadyRemovedStmts,
	FunctionDecl* FD) {
	if (OptionManager::SkipLocalDep)
		return false;
	//Generate three sets of children
	std::vector<Stmt*> FunctionChildren = getAllChildren(FD->getBody());
	std::vector<Stmt*> RemovedChildren; //This saves removed children after reverting
	for (auto AlreadyRemovedStmt : AlreadyRemovedStmts) {
		if (std::find(ToBeRevertedStmts.begin(), ToBeRevertedStmts.end(), AlreadyRemovedStmt) ==
			ToBeRevertedStmts.end()) { //This prevents any reverted stmt (and its children) from being added to RemovedChildren
			auto Children = getAllChildren(AlreadyRemovedStmt);
			RemovedChildren.insert(RemovedChildren.end(), Children.begin(), Children.end());
		}
	}

	//Compute FSet, ASet, & Remaining
	auto TempFSet = toSet(FunctionChildren);
	auto RSet = toSet(RemovedChildren);
	auto FSet = setDifference(TempFSet, RSet);
	std::set<Stmt*> ASet; //This should be empty

	if (noReturn(FSet, ASet, FD)) { //This means all return statements are removed, which is bad.
		spdlog::get("Logger")->info("Invalid Due To No Return");
		return true;
	}
	if (danglingLabel(FSet)) {
		spdlog::get("Logger")->info("Invalid Due To Dangling Label");
		return true;
	}
	if (brokenDependency(FSet, FD)) {
		spdlog::get("Logger")->info("Invalid Due To Broken Dependency");
		return true;
	}

	return false;
}

/* In my understanding, Chunk is a sequence of to-be-removed statements. */
bool LocalReduction::isInvalidChunk(DDElementVector& Chunk) {
	if (OptionManager::SkipLocalDep)
		return false;
	std::vector<Stmt*> FunctionStmts =
		getAllChildren(CurrentFunction->getBody());
	std::vector<Stmt*> AllRemovedStmts;
	for (auto S : Chunk) {
		auto Children = getAllChildren(S.get<Stmt*>());
		AllRemovedStmts.insert(AllRemovedStmts.end(), Children.begin(),
			Children.end());
	}
	auto TempFSet = toSet(FunctionStmts);
	auto FSet = setDifference(TempFSet, RemovedElements);
	auto ASet = toSet(AllRemovedStmts);
	auto Remaining = setDifference(FSet, ASet);

	if (noReturn(FSet, ASet, CurrentFunction))
		return true;
	if (danglingLabel(Remaining))
		return true;
	if (brokenDependency(Remaining, CurrentFunction))
		return true;
	return false;
}

void LocalReduction::doHierarchicalDeltaDebugging(Stmt* S) {
	if (S == NULL)
		return;
	clang::SourceLocation Start = SourceManager::GetBeginOfStmt(Context, S);
	const clang::SourceManager& SM = Context->getSourceManager();
	std::string Loc = Start.printToString(SM);
	if (IfStmt* IS = llvm::dyn_cast<IfStmt>(S)) {
		spdlog::get("Logger")->debug("HDD IF at " + Loc);
		reduceIf(IS);
	}
	else if (WhileStmt* WS = llvm::dyn_cast<WhileStmt>(S)) {
		spdlog::get("Logger")->debug("HDD WHILE at " + Loc);
		reduceWhile(WS);
	}
	else if (CompoundStmt* CS = llvm::dyn_cast<CompoundStmt>(S)) {
		spdlog::get("Logger")->debug("HDD Compound at " + Loc);
		reduceCompound(CS);
	}
	else if (LabelStmt* LS = llvm::dyn_cast<LabelStmt>(S)) {
		spdlog::get("Logger")->debug("HDD Label at " + Loc);
		reduceLabel(LS);
	}
	else if (ForStmt* FS = llvm::dyn_cast<ForStmt>(S)) {
		spdlog::get("Logger")->debug("HDD FOR at " + Loc);
		reduceFor(FS);
	}
	else if (SwitchStmt* SS = llvm::dyn_cast<SwitchStmt>(S)) {
		spdlog::get("Logger")->debug("HDD SWITCH at " + Loc);
		reduceSwitch(SS);
	}
	else if (DoStmt* DS = llvm::dyn_cast<DoStmt>(S)) {
		spdlog::get("Logger")->debug("HDD DO/WHILE at " + Loc);
		reduceDoWhile(DS);
	}
}

std::vector<Stmt*> LocalReduction::getBodyStatements(CompoundStmt* CS) {
	std::vector<Stmt*> Stmts;
	for (auto S : CS->body())
		if (S != NULL)
			Stmts.emplace_back(S);
	return Stmts;
}

void LocalReduction::reduceSwitch(SwitchStmt* SS) {
	const clang::SourceManager& SM = Context->getSourceManager();
	auto Body = SS->getBody();
	SourceLocation BeginSwitch = SourceManager::GetBeginOfStmt(Context, SS);
	SourceLocation EndSwitch = SourceManager::GetEndOfStmt(Context, SS);

	if (BeginSwitch.isInvalid() || EndSwitch.isInvalid()) {
		spdlog::get("Logger")->warn(
			"Invalid location:\nBeginSwitch: {}\nEndSwitch: {}",
			BeginSwitch.printToString(SM), EndSwitch.printToString(SM));
		return;
	}

	std::vector<clang::SwitchCase*> Cases;
	for (clang::SwitchCase* SC = SS->getSwitchCaseList(); SC != NULL;
		SC = SC->getNextSwitchCase()) {
		Cases.insert(Cases.begin(), SC);
	}
	for (int I = 0; I < Cases.size(); I++) {
		SourceLocation CurrLoc =
			SourceManager::GetRealLocation(Context, Cases[I]->getKeywordLoc());
		SourceLocation NextLoc;
		if (I < Cases.size() - 1) {
			NextLoc =
				SourceManager::GetRealLocation(Context, Cases[I + 1]->getKeywordLoc())
				.getLocWithOffset(-1);
		}
		else {
			NextLoc = EndSwitch.getLocWithOffset(-1);
		}
		llvm::StringRef Revert = SourceManager::GetSourceText(SM, CurrLoc, NextLoc);
		removeSourceText(CurrLoc, NextLoc);
		TheRewriter.overwriteChangedFiles();
		if (callOracle()) {
			auto TempChildren = getAllChildren(Cases[I]);
			RemovedElements.insert(TempChildren.begin(), TempChildren.end());
		}
		else {
			TheRewriter.ReplaceText(SourceRange(CurrLoc, NextLoc), Revert);
			TheRewriter.overwriteChangedFiles();
			Queue.push(Cases[I]);
		}
	}
}

void LocalReduction::reduceIf(IfStmt* IS) {
	const clang::SourceManager& SM = Context->getSourceManager();
	SourceLocation BeginIf = SourceManager::GetBeginOfStmt(Context, IS);
	SourceLocation EndIf = SourceManager::GetEndOfStmt(Context, IS);
	SourceLocation EndCond = SourceManager::GetEndOfCond(SM, IS->getCond());
	SourceLocation EndThen = SourceManager::GetEndOfStmt(Context, IS->getThen());

	if (BeginIf.isInvalid() || EndIf.isInvalid() || EndCond.isInvalid() ||
		EndThen.isInvalid()) {
		spdlog::get("Logger")->warn(
			"Invalid location:\nBeginIf: {}\nEndIf: {}\nEndCond: {}\nEndThen: {}",
			BeginIf.printToString(SM), EndIf.printToString(SM),
			EndCond.printToString(SM), EndThen.printToString(SM));
		return;
	}

	llvm::StringRef RevertIf = SourceManager::GetSourceText(SM, BeginIf, EndIf);

	if (IS->getElse()) {
		SourceLocation ElseLoc = IS->getElseLoc();
		if (ElseLoc.isInvalid())
			return;

		removeSourceText(BeginIf, EndCond);
		removeSourceText(ElseLoc, EndIf);
		TheRewriter.overwriteChangedFiles();
		if (callOracle()) {
			Queue.push(IS->getThen());
			std::vector<Stmt*> ElseVec = { IS->getCond(), IS->getElse() };
			for (auto C : ElseVec) {
				auto Children = getAllChildren(C);
				RemovedElements.insert(Children.begin(), Children.end());
			}
		}
		else {
			TheRewriter.ReplaceText(SourceRange(BeginIf, EndIf), RevertIf);
			removeSourceText(BeginIf, ElseLoc.getLocWithOffset(3));
			TheRewriter.overwriteChangedFiles();
			if (callOracle()) {
				Queue.push(IS->getElse());
				std::vector<Stmt*> ThenVec = { IS->getCond(), IS->getThen() };
				for (auto C : ThenVec) {
					auto Children = getAllChildren(C);
					RemovedElements.insert(Children.begin(), Children.end());
				}
			}
			else {
				TheRewriter.ReplaceText(SourceRange(BeginIf, EndIf), RevertIf);
				TheRewriter.overwriteChangedFiles();
				Queue.push(IS->getThen());
				Queue.push(IS->getElse());
			}
		}
	}
	else {
		removeSourceText(BeginIf, EndCond);
		TheRewriter.overwriteChangedFiles();
		if (callOracle()) {
			auto Children = getAllChildren(IS->getCond());
			RemovedElements.insert(Children.begin(), Children.end());
		}
		else {
			TheRewriter.ReplaceText(SourceRange(BeginIf, EndIf), RevertIf);
			TheRewriter.overwriteChangedFiles();
		}
		Queue.push(IS->getThen());
	}
}

void LocalReduction::reduceFor(ForStmt* FS) {
	const clang::SourceManager& SM = Context->getSourceManager();
	auto Body = FS->getBody();
	SourceLocation BeginFor = SourceManager::GetBeginOfStmt(Context, FS);
	SourceLocation EndFor = SourceManager::GetEndOfStmt(Context, FS);
	SourceLocation EndCond = FS->getRParenLoc();

	if (BeginFor.isInvalid() || EndFor.isInvalid() || EndCond.isInvalid()) {
		spdlog::get("Logger")->warn(
			"Invalid location:\nBeginFor: {}\nEndFor: {}\nEndCond: {}",
			BeginFor.printToString(SM), EndFor.printToString(SM),
			EndCond.printToString(SM));
		return;
	}

	llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginFor, EndFor);

	removeSourceText(BeginFor, EndCond);
	TheRewriter.overwriteChangedFiles();
	if (callOracle()) {
		std::vector<Stmt*> ForVec;
		if (FS->getCond())
			ForVec.emplace_back(FS->getCond());
		if (FS->getInit())
			ForVec.emplace_back(FS->getInit());
		if (FS->getInc())
			ForVec.emplace_back(FS->getInc());
		for (auto C : ForVec) {
			auto Children = getAllChildren(C);
			RemovedElements.insert(Children.begin(), Children.end());
		}
	}
	else {
		TheRewriter.ReplaceText(SourceRange(BeginFor, EndFor), Revert);
		TheRewriter.overwriteChangedFiles();
	}
	Queue.push(Body);
}

void LocalReduction::reduceWhile(WhileStmt* WS) {
	const clang::SourceManager& SM = Context->getSourceManager();
	auto Body = WS->getBody();
	SourceLocation BeginWhile = SourceManager::GetBeginOfStmt(Context, WS);
	SourceLocation EndWhile = SourceManager::GetEndOfStmt(Context, WS);
	SourceLocation EndCond = SourceManager::GetEndOfCond(SM, WS->getCond());

	if (BeginWhile.isInvalid() || EndWhile.isInvalid() || EndCond.isInvalid()) {
		spdlog::get("Logger")->warn("Invalid location:\nBeginWhile: {}\nEndWhile: "
			"{}\nEndCond: {}",
			BeginWhile.printToString(SM),
			EndWhile.printToString(SM),
			EndCond.printToString(SM));
		return;
	}

	llvm::StringRef Revert =
		SourceManager::GetSourceText(SM, BeginWhile, EndWhile);

	removeSourceText(BeginWhile, EndCond);
	TheRewriter.overwriteChangedFiles();
	if (callOracle()) {
		auto Children = getAllChildren(WS->getCond());
		RemovedElements.insert(Children.begin(), Children.end());
	}
	else {
		TheRewriter.ReplaceText(SourceRange(BeginWhile, EndWhile), Revert);
		TheRewriter.overwriteChangedFiles();
	}
	Queue.push(Body);
}

void LocalReduction::reduceDoWhile(DoStmt* DS) {
	const clang::SourceManager& SM = Context->getSourceManager();
	auto Body = DS->getBody();
	SourceLocation BeginDo = SourceManager::GetBeginOfStmt(Context, DS);
	SourceLocation EndDo = SourceManager::GetEndOfStmt(Context, DS);
	SourceLocation EndCond = SourceManager::GetEndOfCond(SM, DS->getCond());

	if (BeginDo.isInvalid() || EndDo.isInvalid() || EndCond.isInvalid()) {
		spdlog::get("Logger")->warn("Invalid location:\nBeginDo: {}\nEndDo: "
			"{}\nEndCond: {}",
			BeginDo.printToString(SM),
			EndDo.printToString(SM),
			EndCond.printToString(SM));
		return;
	}

	llvm::StringRef Revert = SourceManager::GetSourceText(SM, BeginDo, EndDo);

	removeSourceText(BeginDo, BeginDo.getLocWithOffset(1));
	removeSourceText(DS->getWhileLoc(), EndDo);
	TheRewriter.overwriteChangedFiles();
	if (callOracle()) {
		auto Children = getAllChildren(DS->getCond());
		RemovedElements.insert(Children.begin(), Children.end());
	}
	else {
		TheRewriter.ReplaceText(SourceRange(BeginDo, EndDo), Revert);
		TheRewriter.overwriteChangedFiles();
	}
	Queue.push(Body);
}

void LocalReduction::reduceCompound(CompoundStmt* CS) {
	auto Stmts = getBodyStatements(CS);
	filterElements(Stmts);

	DDElementVector Elements;
	if (Stmts.size() == 0)
		return;

	Elements.resize(Stmts.size());
	std::transform(Stmts.begin(), Stmts.end(), Elements.begin(), CastElement);

	DDElementSet Removed = doDeltaDebugging(Elements);
	for (auto S : Stmts) {
		if (Removed.find(S) == Removed.end())
			Queue.push(S);
	}
}

void LocalReduction::reduceLabel(LabelStmt* LS) {
	Queue.push(LS->getSubStmt());
}

void LocalReduction::filterElements(std::vector<clang::Stmt*>& Vec) {
	auto I = Vec.begin();
	while (I != Vec.end()) {
		clang::Stmt* S = *I;
		if (DeclStmt* DS = llvm::dyn_cast<DeclStmt>(S))
			Vec.erase(I);
		else if (clang::NullStmt* NS = llvm::dyn_cast<clang::NullStmt>(S))
			Vec.erase(I);
		else
			I++;
	}
}

bool LocalElementCollectionVisitor::VisitFunctionDecl(FunctionDecl* FD) {
	spdlog::get("Logger")->debug("Visit Function Decl: {}",
		FD->getNameInfo().getAsString());
	if (FD->isThisDeclarationADefinition())
		Consumer->Functions.emplace_back(FD);
	return true;
}
