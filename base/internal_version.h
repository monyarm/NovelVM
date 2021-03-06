#if !defined(INCLUDED_FROM_BASE_VERSION_CPP) && !defined(RC_INVOKED)
#error This file may only be included by base/version.cpp or dists/novelvm.rc
#endif

// Reads revision number from file
// (this is used when building with Visual Studio)
#ifdef NOVELVM_INTERNAL_REVISION
#include "internal_revision.h"
#endif

#ifdef RELEASE_BUILD
#undef NOVELVM_REVISION
#endif

#ifndef NOVELVM_REVISION
#define NOVELVM_REVISION
#endif

#define NOVELVM_VERSION "0.0.1git"

#define NOVELVM_VER_MAJOR 0
#define NOVELVM_VER_MINOR 0
#define NOVELVM_VER_PATCH 1
