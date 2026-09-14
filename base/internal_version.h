#if !defined(INCLUDED_FROM_BASE_VERSION_CPP) && !defined(RC_INVOKED)
#error This file may only be included by base/version.cpp or dists/scummvm.rc
#endif

// Reads revision number from file
// (this is used when building with Visual Studio)
#ifdef SCUMMVM_INTERNAL_REVISION
#include "internal_revision.h"
#endif

#ifdef RELEASE_BUILD
#undef SCUMMVM_REVISION
#endif

#ifndef SCUMMVM_REVISION
#define SCUMMVM_REVISION
#endif

#define NOVELVM_VERSION "2026.3.1git"

#define NOVELVM_VER_MAJOR 2026
#define NOVELVM_VER_MINOR 3
#define NOVELVM_VER_PATCH 1
