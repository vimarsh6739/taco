#ifndef TACO_VERSION_H
#define TACO_VERSION_H

// This file contains version/config info, gathered at cmake config time.

#define TACO_BUILD_TYPE "Release"
#define TACO_BUILD_DATE "2023-04-17"

#define TACO_BUILD_COMPILER_ID      "GNU"
#define TACO_BUILD_COMPILER_VERSION "7.5.0"

#define TACO_VERSION_MAJOR "0"
#define TACO_VERSION_MINOR "1"
// if taco starts using a patch number, add  here
// if taco starts using a tweak number, add  here

// For non-git builds, this will be an empty string.
#define TACO_VERSION_GIT_SHORTHASH "2b8ece4c"

#define TACO_FEATURE_OPENMP 0
#define TACO_FEATURE_PYTHON 0
#define TACO_FEATURE_CUDA   0

#endif /* TACO_VERSION_H */
