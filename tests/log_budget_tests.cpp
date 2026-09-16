// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The two log gates (log_once.h). Both are arithmetic that decides whether a line is
// written, and both have one place a fault could hide: a latch that re-arms turns a
// once-per-session diagnostic into a per-frame stream, and a budget whose Exhausted()
// fires a line early or late either loses the "the rest are not written down" notice or
// repeats it. Neither is visible in game - the symptom is a log file that grows with the
// length of the session rather than with what happened in it.

#include "test_support.h"

#include "log_once.h"

#include <iostream>

namespace {

using namespace OutlastHeadTracking;
using olht_tests::Check;

void TheLatchIsClaimedExactlyOnce() {
    bool latch = false;
    Check(ClaimOnce(latch), "the first offer of a latch claims it");
    Check(!ClaimOnce(latch), "and the second does not");
    Check(!ClaimOnce(latch), "and it never re-arms");
}

void ABudgetSpendsExactlyItsAllowance() {
    LogBudget budget(3);
    Check(budget.Take() && budget.Take() && budget.Take(),
          "a budget of three lines gives out three");
    Check(!budget.Take(), "and refuses the fourth");
    Check(!budget.Take(), "and every one after it");
}

// The closing "that is N, the rest are not written down" line hangs off this, so it has
// to be false while lines remain and true on the call that spends the last one. True
// early repeats the notice; true late means it is never written and the reader cannot
// tell a log that stopped from a condition that did.
void ExhaustedTurnsTrueOnTheLastLineTaken() {
    LogBudget budget(2);
    Check(!budget.Exhausted(), "a fresh budget is not exhausted");
    Check(budget.Take() && !budget.Exhausted(),
          "nor is one with a line still in hand after the first is taken");
    Check(budget.Take() && budget.Exhausted(),
          "the take that spends the last line reports the budget exhausted");
    Check(budget.Exhausted(), "and it stays exhausted");
}

// A zero allowance is what a caller gets from a configured cap of 0, and it must write
// nothing at all rather than one line. Exhausted() is true from the start, so the closing
// notice is not written either - there is nothing for it to close.
void AZeroBudgetWritesNothing() {
    LogBudget budget(0);
    Check(budget.Exhausted(), "a budget of no lines starts exhausted");
    Check(!budget.Take(), "and hands none out");
}

}  // namespace

int RunLogBudgetTests() {
    std::cout << "\n-- Log gates --\n";
    TheLatchIsClaimedExactlyOnce();
    ABudgetSpendsExactlyItsAllowance();
    ExhaustedTurnsTrueOnTheLastLineTaken();
    AZeroBudgetWritesNothing();
    return olht_tests::TakeFailures();
}
