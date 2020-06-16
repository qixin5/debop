# Debop

Debop is a multi-goal debloating technique. In its current implementation, Debop supports an objective function that encodes three goals: size reduction, attack surface reduction, and generality. It leverages a Markov Chain Monte Carlo (MCMC) sampling technique to explore the search space and search for a debloated program that maximizes the objective.

## Requirements
* CMake >= 3.10.2
* Clang and LLVM >= 9.0.0
* spdlog >= 1.3.1
* ROPgadget >= 5.8

## Installation
Make sure you have installed all the required libraries (shown above). Please refer to https://github.com/llvm/llvm-project.git to install LLVM and Clang. Please refer to https://github.com/gabime/spdlog to install spdlog. Please refer to https://github.com/JonathanSalwan/ROPgadget to install ROPgadget.

### Linux
Once you have all the requirements, do the following steps.

1. In CMakeLists.txt, change to your own paths the last two `include_directories` (lines ending with "Change to your own path!").
2. Run `mkdir build && cd build`.
3. Run `cmake ..`.
4. Run `make`.

## Quick Test
0. Test ROPgadget by running `ROPgadget --version` from command line.
1. In the `debop` directory, go to `quicktest` by running `cd test/quicktest`.
2. Run `./run_debop`.
3. After running, check that you have a `debop-out` directory containing a few sample C files and a `log.txt` file. In `log.txt` file, there is a reporting summary shown at the end. If you've found all these, the running was successful.

## Usage
```
DEBOP_BIN [OPTION] ORACLE_FILE SOURCE_FILE
```
DEBOP_BIN: The debop binary (XXX/debop_release/build/bin/reducer).
ORACLE_FILE: The oracle script used to compile source, run tests, and compute scores. It should compute a total of six scores: (1) original program size, (2) reduced program size, (3) original program gadgets, (4) reduced program gadgets, (5) number of total tests, and (6) number of passed tests. The resulting scores should be redirected to a file named "eval_rslt.txt". See XXX/debop_release/test/quicktest/test.sh for an example.
SOURCE_FILE: The program source file(s).
OPTION:
*-m: Number of samples.
*-i: Number of sampling iterations. (Default is 100. Consider passing a large value.)
*-a: Alpha value (weight for attack surface reduction).
*-e: Beta value (weight for generality).
*-k: K value (for computing density values).
*-s: No value needed. Save temp files.

###Note
It is strongly recommended that you provide Debop with a source file with all the code unexercised by the oracle inputs eliminated. This would make Debop's search space significantly smaller. To produce such a file, please refer to https://github.com/qixin5/debcov.

## Contact
If you have questions, please contact Qi Xin via qxin6@gatech.edu.