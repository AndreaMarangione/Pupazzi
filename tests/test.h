// Shared by the test programs. Each program is a list of doctest TEST_CASEs (third_party/doctest)
// that run in order, in the same process: some continue from the game the previous one left. The
// name of each test case and every EXPECT are printed as they run, so the output reads as a report;
// doctest counts the checks and sums them up at the end (and the exit code says whether all passed).
//   ./build/native/test_rules                 all the test cases of a program
//   ./build/native/test_rules -ltc            their names
//   ./build/native/test_rules -tc="1)*"       only some (careful: some need the ones before)
#pragma once
#include "doctest/doctest.h"
#include <stdio.h>

// A check: a line of the report ("ok" or "ERRORE" and what it checks) and a doctest assertion at the
// line of the call.
#define EXPECT(ok, what)                                                   \
  do {                                                                     \
    const bool expect_ok_ = (ok);                                          \
    const char* expect_what_ = (what);                                     \
    printf("%s %s\n", expect_ok_ ? "  ok    " : "  ERRORE", expect_what_); \
    CHECK_MESSAGE(expect_ok_, doctest::String(expect_what_));              \
  } while (0)

// Runs the test cases (doctest options on the command line, the program's own are ignored by
// doctest) and returns the exit code: 0 if every check passed.
int run_tests(int argc, char** argv);
