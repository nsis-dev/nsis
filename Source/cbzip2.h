/*
 * cbzip2.h
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

#ifndef __CBZIP2_H__
#define __CBZIP2_H__

#include "compressor.h"
#include "bzip2/bzlib.h"

class CBzip2 : public ICompressor {
  public:
    virtual ~CBzip2() {}

    virtual int Init(int level, unsigned int dict_size, unsigned int dataSize) {
      last_ret = !BZ_STREAM_END;
      stream = new bz_stream;
      if (!stream) return BZ_MEM_ERROR;
      return BZ2_bzCompressInit(stream, level, 0, 30);
    }

    virtual int End() {
      int ret = BZ2_bzCompressEnd(stream);
      delete stream;
      return ret;
    }

    virtual int Compress(bool finish) {
      // act like zlib when it comes to stream ending
      if (last_ret == BZ_STREAM_END && finish)
        return C_FINISHED;
      last_ret = BZ2_bzCompress(stream, finish?BZ_FINISH:0);
      
      if (last_ret < 0)
        return last_ret;

      return C_OK;
    }

    virtual void SetNextIn(char *in, unsigned int size) {
      stream->next_in = (unsigned char*) in;
      stream->avail_in = size;
    }

    virtual void SetNextOut(char *out, unsigned int size) {
      stream->next_out = (unsigned char*) out;
      stream->avail_out = size;
    }

    virtual char* GetNextOut() {
      return (char*) stream->next_out;
    }

    virtual unsigned int GetAvailIn() {
      return stream->avail_in;
    }

    virtual unsigned int GetAvailOut() {
      return stream->avail_out;
    }

    virtual const TCHAR* GetName() {
      return _T("bzip2");
    }

    virtual const TCHAR* GetErrStr(int err) {
      switch (err)
      {
      case BZ_SEQUENCE_ERROR:
        return _T("sequence error - bad call");
      case BZ_PARAM_ERROR:
        return _T("parameter error - bad call");
      case BZ_MEM_ERROR:
        return _T("not enough memory");
      case BZ_CONFIG_ERROR:
        return _T("config error");
      default:
        return _T("unknown error");
      }
    }

  private:
    bz_stream *stream;
    int last_ret;
};

#endif
