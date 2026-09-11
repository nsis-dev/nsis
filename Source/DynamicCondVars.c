/*
 * DynamicCondVars.c
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

#include "Platform.h"
#include <stdbool.h>

#ifndef _WIN32

  bool ConditionVarsSupported() { return true; }

#else

  /* Note: init is not synchronized. ConditionVarsSupported() must first be
   * called from a single thread (makensis calls it during CZstd::Init,
   * before zstd spawns its worker pool), after which it is safe to call
   * the NSIS_* wrappers from any thread. */
  static bool isInitialized = false;
  static bool isAvailable = false;
  typedef void (WINAPI *InitCondVarFn)(void*);
  typedef void (WINAPI *WakeCondVarFn)(void*);
  typedef bool (WINAPI *SleepCondVarFn)(void*, void*, int);
  static InitCondVarFn _InitializeConditionVariable;
  static WakeCondVarFn _WakeConditionVariable;
  static WakeCondVarFn _WakeAllConditionVariable;
  static SleepCondVarFn _SleepConditionVariableCS;

  bool ConditionVarsSupported()
  {
    if(isInitialized) return isAvailable;

    HMODULE kernel32 = GetModuleHandleA("kernel32");
    _InitializeConditionVariable = (InitCondVarFn)(void*)GetProcAddress(kernel32, "InitializeConditionVariable");
    _WakeConditionVariable = (WakeCondVarFn)(void*)GetProcAddress(kernel32, "WakeConditionVariable");
    _WakeAllConditionVariable = (WakeCondVarFn)(void*)GetProcAddress(kernel32, "WakeAllConditionVariable");
    _SleepConditionVariableCS = (SleepCondVarFn)(void*)GetProcAddress(kernel32, "SleepConditionVariableCS");

    isAvailable = _InitializeConditionVariable && _WakeConditionVariable && _WakeAllConditionVariable && _SleepConditionVariableCS;
    isInitialized = true;
    return isAvailable;
  }

void NSIS_InitializeConditionVariable(void* cv) { _InitializeConditionVariable(cv); }
void NSIS_WakeConditionVariable(void* cv) { _WakeConditionVariable(cv); }
void NSIS_WakeAllConditionVariable(void* cv) { _WakeAllConditionVariable(cv); }
bool NSIS_SleepConditionVariableCS(void* cv, void* cs, int timeout) { return _SleepConditionVariableCS(cv, cs, timeout); }

#endif
