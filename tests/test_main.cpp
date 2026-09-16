// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <iostream>

int RunUe3MathTests();
int RunHeadPoseTests();
int RunConfigTests();
int RunFrameTests();
int RunFrameZoomTests();
int RunAimProjectionTests();
int RunCameraCacheTests();
int RunWindowPlacementTests();
int RunLightCompositionTests();
int RunBuildProfileTests();

int main() {
    std::cout << "Outlast Head Tracking Tests\n";
    std::cout << "===========================\n";

    int failures = 0;
    failures += RunUe3MathTests();
    failures += RunHeadPoseTests();
    failures += RunConfigTests();
    failures += RunFrameTests();
    failures += RunFrameZoomTests();
    failures += RunAimProjectionTests();
    failures += RunCameraCacheTests();
    failures += RunWindowPlacementTests();
    failures += RunLightCompositionTests();
    failures += RunBuildProfileTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
