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

  static bool isInitialized = false;
  static bool isAvailable = false;
  static void (WINAPI *_InitializeConditionVariable)(void*);
  static void (WINAPI *_WakeConditionVariable)(void*);
  static void (WINAPI *_WakeAllConditionVariable)(void*);
  static bool (WINAPI *_SleepConditionVariableCS)(void*, void*, int);

  bool ConditionVarsSupported()
  {
    if(isInitialized) return isAvailable;

    HMODULE kernel32 = GetModuleHandleA("kernel32");
    (FARPROC)_InitializeConditionVariable = GetProcAddress(kernel32, "InitializeConditionVariable");
    (FARPROC)_WakeConditionVariable = GetProcAddress(kernel32, "WakeConditionVariable");
    (FARPROC)_WakeAllConditionVariable = GetProcAddress(kernel32, "WakeAllConditionVariable");
    (FARPROC)_SleepConditionVariableCS = GetProcAddress(kernel32, "SleepConditionVariableCS");

    isAvailable = _InitializeConditionVariable && _WakeConditionVariable && _WakeAllConditionVariable && _SleepConditionVariableCS;
    isInitialized = true;
    return isAvailable;
  }

void NSIS_InitializeConditionVariable(void* cv) { _InitializeConditionVariable(cv); }
void NSIS_WakeConditionVariable(void* cv) { _WakeConditionVariable(cv); }
void NSIS_WakeAllConditionVariable(void* cv) { _WakeAllConditionVariable(cv); }
bool NSIS_SleepConditionVariableCS(void* cv, void* cs, int timeout) { return _SleepConditionVariableCS(cv, cs, timeout); }

#endif
