/*
 * clzma.h
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

#ifndef __CLZMA_H__
#define __CLZMA_H__

#include "Platform.h"

#ifndef _WIN32
# include <pthread.h>
#endif

#include "compressor.h"
#include "7zip/LzmaSDK/LzmaEnc.h"

#define LZMA_BAD_CALL -1
#define LZMA_INIT_ERROR -2
#define LZMA_THREAD_ERROR -3
#define LZMA_IO_ERROR -4
#define LZMA_MEM_ERROR -5

class CLZMA:
  public ICompressor
{
private:
  struct InputStream
  {
    ISeqInStream vt;
    CLZMA *owner;
  } inputStream;

  struct OutputStream
  {
    ISeqOutStream vt;
    CLZMA *owner;
  } outputStream;

  CLzmaEncHandle encoder;
  ISzAlloc allocator;

#ifdef _WIN32
  HANDLE hCompressionThread;
#else
  pthread_t hCompressionThread;
#endif
  HANDLE hNeedIOEvent;
  HANDLE hIOReadyEvent;

  BYTE *next_in; /* next input byte */
  UINT avail_in; /* number of bytes available at next_in */

  BYTE *next_out; /* next output byte should be put there */
  UINT avail_out; /* remaining free space at next_out */

  int res;

  BOOL finish;
  BOOL compressor_finished;

  static void *SzAlloc(ISzAllocPtr alloc, size_t size);
  static void SzFree(ISzAllocPtr alloc, void *address);
  static SRes Read(ISeqInStreamPtr stream, void *data, size_t *size);
  static size_t Write(ISeqOutStreamPtr stream, const void *data, size_t size);

  void GetMoreIO();
  int CompressReal();

#ifdef _WIN32
  static DWORD WINAPI lzmaCompressThread(LPVOID lpParameter);
#else
  static void* lzmaCompressThread(void *lpParameter);
#endif

public:
  CLZMA();
  virtual ~CLZMA();

  virtual int Init(int level, unsigned int dicSize);
  virtual int End();
  virtual int Compress(bool flush);

  virtual void SetNextIn(char *in, unsigned int size);
  virtual void SetNextOut(char *out, unsigned int size);

  virtual char *GetNextOut();
  virtual unsigned int GetAvailIn();
  virtual unsigned int GetAvailOut();
  virtual const TCHAR *GetName();

  virtual const TCHAR* GetErrStr(int err);
};

#endif
