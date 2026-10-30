/*
 * DynamicCondVars.h
 * 
 * This file is a part of NSIS.
 * 
 * Copyright (C) 1999-2026 Nullsoft and Contributors
 * 
 * Licensed under the zlib/libpng license (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * Licence details can be found in the file COPYING.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.
 *
 */

#ifndef NSIS_DYNAMIC_COND_VARS_H
#define NSIS_DYNAMIC_COND_VARS_H

#if defined(_MSC_VER) && _MSC_VER < 1800
typedef unsigned char bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool ConditionVarsSupported();

#ifdef _WIN32

/* Drop-in replacements for the Vista+ condition-variable APIs. They are
 * called through -D mappings applied to the bundled zstd sources only
 * (see Source/SConscript, Source/Tests/SConscript), so the vendored
 * zstd/lib/common/threading.h stays pristine and makensis links no
 * Vista+ imports directly. void* is used instead of PCONDITION_VARIABLE
 * so this header builds with pre-Vista SDKs too; on x86 the WINAPI
 * decoration still matches (pointer-sized single argument). */

VOID WINAPI NSIS_InitializeConditionVariable(void* conditionVariable);
VOID WINAPI NSIS_WakeConditionVariable(void* conditionVariable);
VOID WINAPI NSIS_WakeAllConditionVariable(void* conditionVariable);
BOOL WINAPI NSIS_SleepConditionVariableCS(void* conditionVariable, void* criticalSection, DWORD milliseconds);

#endif

#ifdef __cplusplus
}
#endif

#endif//!NSIS_DYNAMIC_COND_VARS_H
