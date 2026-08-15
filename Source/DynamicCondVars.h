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

#ifndef ___DYNAMIC_COND_VARS__H___
#define ___DYNAMIC_COND_VARS__H___

#ifdef __cplusplus
extern "C"
#endif

#include <stdbool.h>

bool ConditionVarsSupported();

#ifdef _WIN32

void NSIS_InitializeConditionVariable(void* conditionVariable);
void NSIS_WakeConditionVariable(void* conditionVariable);
void NSIS_WakeAllConditionVariable(void* conditionVariable);
bool NSIS_SleepConditionVariableCS(void* conditionVariable, void* criticalSection, int milliseconds);

#endif

#endif//!___DYNAMIC_COND_VARS__H___
