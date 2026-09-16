// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <iostream>

// The same shape as cameraunlock-core's own tests: a per-file failure count, a Check
// that names what it asserted, and a Run<Area>Tests() the runner in test_main.cpp
// declares and sums. No framework, because these lock pure functions and a framework
// would be a dependency the mod's build does not otherwise have.
namespace olht_tests {

inline int& Failures() {
    static int failures = 0;
    return failures;
}

inline void Check(bool condition, const char* what) {
    if (condition) {
        std::cout << "  [PASS] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++Failures();
    }
}

inline bool NearEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// Claims the failures accumulated by one file's tests, so each Run<Area>Tests() can
// report its own count and start the next file from zero.
inline int TakeFailures() {
    const int failures = Failures();
    Failures() = 0;
    return failures;
}

}  // namespace olht_tests
