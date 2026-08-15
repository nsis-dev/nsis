/*
 * czstd.h
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

#ifndef __CZSTD_H__
#define __CZSTD_H__

#include "compressor.h"
#include "DynamicCondVars.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"
#include "zstd/lib/zstd_errors.h"

class CZstd : public ICompressor {
  public:
    CZstd() : cstream(NULL), frameFinished(false) { }

    virtual ~CZstd()
    {
      if (cstream) ZSTD_freeCStream(cstream);
    }

    virtual int Init(int level, unsigned int dict_size, unsigned int dataSize) {
      size_t res;
      if (!cstream)
      {
        cstream = ZSTD_createCStream();
        if (!cstream) return -ZSTD_error_memory_allocation;

        #if defined(ZSTD_MULTITHREAD)
        if (ConditionVarsSupported())
        {
          res = ZSTD_CCtx_setParameter(cstream, ZSTD_c_nbWorkers, getCoreCountLogical());
          if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);
        }
        #endif
        
        res = ZSTD_CCtx_setParameter(cstream, ZSTD_c_format, ZSTD_f_zstd1_magicless);
        if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);
      }

      res = ZSTD_CCtx_reset(cstream, ZSTD_reset_session_only);
      if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);

      res = ZSTD_CCtx_setParameter(cstream, ZSTD_c_compressionLevel, level);
      if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);

      if (dataSize != C_UNKNOWN_SIZE)
      {
        res = ZSTD_CCtx_setPledgedSrcSize(cstream, dataSize);
        if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);
      }

      frameFinished = false;
      return C_OK;
    }

    virtual int End() { return C_OK; }

    virtual int Compress(bool doFinish)
    {
      if (frameFinished) return C_FINISHED;

      size_t res = ZSTD_compressStream2(cstream, &output, &input, doFinish ? ZSTD_e_end : ZSTD_e_continue);

      if (doFinish && res == 0)
      {
        frameFinished = true;
        return C_FINISHED;
      }

      if (ZSTD_isError(res)) return -ZSTD_getErrorCode(res);
      return C_OK;
    }

    virtual void SetNextIn(char *in, unsigned int size) {
      input.src = in;
      input.size = size;
      input.pos = 0;
    }

    virtual void SetNextOut(char *out, unsigned int size) {
      output.dst = out;
      output.size = size;
      output.pos = 0;
    }

    virtual char* GetNextOut() { return ((char*)output.dst) + output.pos; }

    virtual unsigned int GetAvailIn() { return input.size - input.pos; }

    virtual unsigned int GetAvailOut() { return output.size - output.pos; }

    virtual const TCHAR* GetName() { return _T("zstd"); }

    virtual const TCHAR* GetErrStr(int err)
    {
      const char *zstdMessage = ZSTD_getErrorString((ZSTD_ErrorCode)(-err));
      if (!zstdMessage) return _T("unknown zstd error");

      #ifdef UNICODE
        static TCHAR lastError[257] = { 0 };
        mbstowcs(lastError, zstdMessage, 256);
        return lastError;
      #else
        return zstdMessage;
      #endif
    }

  private:
    ZSTD_CStream *cstream;
    ZSTD_inBuffer input;
    ZSTD_outBuffer output;
    bool frameFinished;

    static int getCoreCountLogical()
    {
      static int numCores = 0;
      if (numCores) return numCores;
      numCores = sysReadCoreCountLogical();
      if (numCores < 1) numCores = 1;
      return numCores;
    }

#if defined(_WIN32) || defined(WIN32)
    static int sysReadCoreCountLogical()
    {
      SYSTEM_INFO sysinfo;
      GetSystemInfo(&sysinfo);
      return sysinfo.dwNumberOfProcessors;
    }
#elif defined(__APPLE__)
    static int sysReadCoreCountLogical()
    {
      int32_t numCores = 0;
      size_t size = sizeof(int32_t);
      if (sysctlbyname("hw.logicalcpu", &numCores, &size, NULL, 0) != 0) return -1;
      return numCores;
    }
#elif defined(__linux__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__CYGWIN__) || defined(__FreeBSD__)
    static int sysReadCoreCountLogical()
    {
      return (int)sysconf(_SC_NPROCESSORS_ONLN);
    }
#else
    static int sysReadCoreCountLogical() { return -1; }
#endif
};

#endif
