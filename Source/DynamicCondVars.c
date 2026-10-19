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
#if defined(_MSC_VER) && _MSC_VER < 1800
typedef unsigned char bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#ifndef _WIN32

  bool ConditionVarsSupported() { return true; }

#else

  /* Thread-safe one-time init via InterlockedCompareExchange (available
   * since Win95, so XP-safe). makensis calls this during CZstd::Init
   * before the worker pool spawns, but concurrent first calls are safe. */
  static volatile LONG initState = 0; /* 0=uninit, 1=initializing, 2=done */
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
    if (initState == 2) return isAvailable;

    if (InterlockedCompareExchange(&initState, 1, 0) == 0)
    {
      HMODULE kernel32 = GetModuleHandleA("kernel32");
      _InitializeConditionVariable = (InitCondVarFn)(void*)GetProcAddress(kernel32, "InitializeConditionVariable");
      _WakeConditionVariable = (WakeCondVarFn)(void*)GetProcAddress(kernel32, "WakeConditionVariable");
      _WakeAllConditionVariable = (WakeCondVarFn)(void*)GetProcAddress(kernel32, "WakeAllConditionVariable");
      _SleepConditionVariableCS = (SleepCondVarFn)(void*)GetProcAddress(kernel32, "SleepConditionVariableCS");

      isAvailable = _InitializeConditionVariable && _WakeConditionVariable && _WakeAllConditionVariable && _SleepConditionVariableCS;
      initState = 2;
    }
    else
    {
      /* Another thread is initializing; spin briefly then observe. */
      while (initState != 2) Sleep(0);
    }
    return isAvailable;
  }

void NSIS_InitializeConditionVariable(void* cv) { _InitializeConditionVariable(cv); }
void NSIS_WakeConditionVariable(void* cv) { _WakeConditionVariable(cv); }
void NSIS_WakeAllConditionVariable(void* cv) { _WakeAllConditionVariable(cv); }
bool NSIS_SleepConditionVariableCS(void* cv, void* cs, int timeout) { return _SleepConditionVariableCS(cv, cs, timeout); }

#endif
