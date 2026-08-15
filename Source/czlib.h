/*
 * czlib.h
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
 * Unicode support by Jim Park -- 08/24/2007
 */

#ifndef __CZLIB_H__
#define __CZLIB_H__

#include "compressor.h"
#include <zlib.h>

class CZlib : public ICompressor {
  public:
    virtual ~CZlib() {}

    virtual int Init(int level, unsigned int dict_size, unsigned int dataSize) {
      stream = new z_stream;
      if (!stream) return Z_MEM_ERROR;

      stream->zalloc = (alloc_func)Z_NULL;
      stream->zfree = (free_func)Z_NULL;
      stream->opaque = (voidpf)Z_NULL;
      return deflateInit2(stream, level,
        Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    }

    virtual int End() {
      int ret = deflateEnd(stream);
      delete stream;
      return ret;
    }

    virtual int Compress(bool finish) {
      return deflate(stream, finish?Z_FINISH:0);
    }

    virtual void SetNextIn(char *in, unsigned int size) {
      stream->next_in = (unsigned char*)in;
      stream->avail_in = size;
    }

    virtual void SetNextOut(char *out, unsigned int size) {
      stream->next_out = (unsigned char*)out;
      stream->avail_out = size;
    }

    virtual char* GetNextOut() {
      return (char*)stream->next_out;
    }

    virtual unsigned int GetAvailIn() {
      return stream->avail_in;
    }

    virtual unsigned int GetAvailOut() {
      return stream->avail_out;
    }

    virtual const TCHAR* GetName() {
      return _T("zlib");
    }

    virtual const TCHAR* GetErrStr(int err) {
      switch (err)
      {
      case Z_STREAM_ERROR:
        return _T("invalid stream - bad call");
      case Z_DATA_ERROR:
        return _T("data error");
      case Z_MEM_ERROR:
        return _T("not enough memory");
      case Z_BUF_ERROR:
        return _T("buffer error - bad call");
      case Z_VERSION_ERROR:
        return _T("version error");
      default:
        return _T("unknown error");
      }
    }

  private:
    z_stream *stream;
};

#endif
