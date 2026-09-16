/*
 * clzma.cpp
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

#include <algorithm> // for std::min
#include <stdlib.h>
#include <string.h>
#include "clzma.h"

using namespace std;

#ifndef _WIN32
struct evnet_t
{
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  bool signaled;
};

HANDLE CreateEvent(void *, BOOL, BOOL, TCHAR *)
{
  evnet_t *event = (evnet_t *) malloc(sizeof(evnet_t));
  if (!event)
    return 0;
  if (pthread_cond_init(&event->cond, NULL))
  {
    free(event);
    return 0;
  }
  if (pthread_mutex_init(&event->mutex, NULL))
  {
    pthread_cond_destroy(&event->cond);
    free(event);
    return 0;
  }
  event->signaled = false;
  return (HANDLE) event;
}

BOOL SetEvent(HANDLE _event)
{
  evnet_t *event = (evnet_t *) _event;
  if (pthread_mutex_lock(&event->mutex))
    return FALSE;
  event->signaled = true;
  pthread_cond_signal(&event->cond);
  if (pthread_mutex_unlock(&event->mutex))
    return FALSE;
  return TRUE;
}

BOOL ResetEvent(HANDLE _event)
{
  evnet_t *event = (evnet_t *) _event;
  if (pthread_mutex_lock(&event->mutex))
    return FALSE;
  event->signaled = false;
  if (pthread_mutex_unlock(&event->mutex))
    return FALSE;
  return TRUE;
}

BOOL CloseHandle(HANDLE _event)
{
  BOOL ret = TRUE;
  evnet_t *event = (evnet_t *) _event;
  if (!event)
    return FALSE;
  if (pthread_cond_destroy(&event->cond))
    ret = FALSE;
  if (pthread_mutex_destroy(&event->mutex))
    ret = FALSE;
  free(event);
  return ret;
}

#define WAIT_OBJECT_0 0
#define INFINITE 0
DWORD WaitForSingleObject(HANDLE _event, DWORD) {
  DWORD ret = WAIT_OBJECT_0;
  evnet_t *event = (evnet_t *) _event;
  if (pthread_mutex_lock(&event->mutex))
    return !WAIT_OBJECT_0;
  if (!event->signaled)
  {
    if (pthread_cond_wait(&event->cond, &event->mutex))
    {
      ret = !WAIT_OBJECT_0;
    }
  }
  event->signaled = false;
  pthread_mutex_unlock(&event->mutex);
  return ret;
}

#define WaitForMultipleObjects(x, list, y, t) WaitForSingleObject(list[0], t)

#endif

#ifdef _WIN32
DWORD CLZMA::lzmaCompressThread(LPVOID lpParameter)
#else
void* CLZMA::lzmaCompressThread(void *lpParameter)
#endif
{
  CLZMA *Compressor = (CLZMA *) lpParameter;
  if (!Compressor)
    return 0;

  Compressor->CompressReal();
  return 0;
}

void *CLZMA::SzAlloc(ISzAllocPtr, size_t size)
{
  return size ? malloc(size) : NULL;
}

void CLZMA::SzFree(ISzAllocPtr, void *address)
{
  free(address);
}

SRes CLZMA::Read(ISeqInStreamPtr stream, void *data, size_t *size)
{
  CLZMA *self = ((const InputStream *) stream)->owner;
  size_t requested = *size;
  *size = 0;

  while (requested)
  {
    if (!self->avail_in)
    {
      if (self->finish)
        return SZ_OK;
      self->GetMoreIO();
      if (!self->avail_in)
        return self->finish ? SZ_OK : SZ_ERROR_READ;
      if (self->compressor_finished)
        return SZ_ERROR_READ;
    }

    const size_t count = min(requested, (size_t) self->avail_in);
    memcpy(data, self->next_in, count);
    self->avail_in -= (UINT) count;
    self->next_in += count;
    data = (BYTE *) data + count;
    requested -= count;
    *size += count;
  }
  return SZ_OK;
}

size_t CLZMA::Write(ISeqOutStreamPtr stream, const void *data, size_t size)
{
  CLZMA *self = ((const OutputStream *) stream)->owner;
  const size_t requested = size;

  while (size)
  {
    if (!self->avail_out)
    {
      self->GetMoreIO();
      if (!self->avail_out)
        return requested - size;
    }

    const size_t count = min(size, (size_t) self->avail_out);
    memcpy(self->next_out, data, count);
    self->avail_out -= (UINT) count;
    self->next_out += count;
    data = (const BYTE *) data + count;
    size -= count;
  }
  return requested;
}

CLZMA::CLZMA(): encoder(NULL)
{
  allocator.Alloc = SzAlloc;
  allocator.Free = SzFree;
  inputStream.vt.Read = Read;
  inputStream.owner = this;
  outputStream.vt.Write = Write;
  outputStream.owner = this;
  encoder = LzmaEnc_Create(&allocator);

#ifdef _WIN32
  hCompressionThread = NULL;
#else
  hCompressionThread = 0;
#endif
  hNeedIOEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  hIOReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  finish = FALSE;
  compressor_finished = TRUE;
  hCompressionThread = 0;
  SetNextOut(NULL, 0);
  SetNextIn(NULL, 0);
}

CLZMA::~CLZMA()
{
  End();
  if (hNeedIOEvent)
  {
    CloseHandle(hNeedIOEvent);
    hNeedIOEvent = NULL;
  }
  if (hIOReadyEvent)
  {
    CloseHandle(hIOReadyEvent);
    hIOReadyEvent = NULL;
  }
  if (encoder)
  {
    LzmaEnc_Destroy(encoder, &allocator, &allocator);
    encoder = NULL;
  }
}

int CLZMA::Init(int level, unsigned int dicSize)
{
  End();
  compressor_finished = FALSE;
  finish = FALSE;
  res = C_OK;

  if (!encoder || !hNeedIOEvent || !hIOReadyEvent)
  {
    return LZMA_INIT_ERROR;
  }

  ResetEvent(hNeedIOEvent);
  ResetEvent(hIOReadyEvent);

  res = C_OK;

  CLzmaEncProps props;
  LzmaEncProps_Init(&props);
  // NCoderPropID::kAlgorithm
  props.algo = 1;
  // NCoderPropID::kDictionarySize
  props.dictSize = dicSize;
  // NCoderPropID::kNumFastBytes
  props.fb = 64;
  props.btMode = 1;
  props.numHashBytes = 4;
  props.writeEndMark = 1;
  props.numThreads = 1;

  const SRes result = LzmaEnc_SetProps(encoder, &props);
  if (result == SZ_ERROR_MEM)
    return LZMA_MEM_ERROR;
  return result == SZ_OK ? C_OK : LZMA_INIT_ERROR;
}

int CLZMA::End()
{
  // has compressor not finished?
  if (hCompressionThread && !compressor_finished)
  {
    // kill compression thread
    avail_in = 0;
    avail_out = 0;
    compressor_finished = TRUE;

    SetEvent(hIOReadyEvent);
#ifdef _WIN32
    WaitForSingleObject(hCompressionThread, INFINITE);
#else
    pthread_join(hCompressionThread, NULL);
#endif
  }
  if (hCompressionThread)
  {
#ifdef _WIN32
    CloseHandle(hCompressionThread);
    hCompressionThread = NULL;
#else
    pthread_detach(hCompressionThread);
    hCompressionThread = 0;
#endif
  }
  SetNextOut(NULL, 0);
  SetNextIn(NULL, 0);
  return C_OK;
}

int CLZMA::CompressReal()
{
  try
  {
    Byte properties[LZMA_PROPS_SIZE];
    SizeT propertiesSize = sizeof(properties);
    SRes result = LzmaEnc_WriteProperties(encoder, properties, &propertiesSize);

    if (result == SZ_OK && Write(&outputStream.vt, properties, propertiesSize) != propertiesSize)
      result = SZ_ERROR_WRITE;
    if (result == SZ_OK)
      result = LzmaEnc_Encode(encoder, &outputStream.vt, &inputStream.vt, NULL,
                             &allocator, &allocator);

    if (res == C_OK)
    {
      if (result == SZ_OK)
        res = C_FINISHED;
      else if (result == SZ_ERROR_MEM)
        res = LZMA_MEM_ERROR;
      else if (result == SZ_ERROR_READ || result == SZ_ERROR_WRITE)
        res = compressor_finished ? LZMA_THREAD_ERROR : LZMA_IO_ERROR;
      else
        res = LZMA_IO_ERROR;
    }
  }
  catch (...)
  {
    if (res == C_OK)
      res = LZMA_IO_ERROR;
  }

  compressor_finished = TRUE;
  SetEvent(hNeedIOEvent);
  return C_OK;
}

int CLZMA::Compress(bool flush)
{
  if (compressor_finished)
  {
    // act like zlib when it comes to stream ending
    if (flush)
      return C_OK;
    else
      return LZMA_BAD_CALL;
  }

  finish = flush;

  if (!hCompressionThread)
  {
#ifdef _WIN32
    DWORD dwThreadId;

    hCompressionThread = CreateThread(0, 0, lzmaCompressThread, (LPVOID) this, 0, &dwThreadId);
    if (!hCompressionThread)
#else
    if (pthread_create(&hCompressionThread, NULL, lzmaCompressThread, (LPVOID) this))
#endif
      return LZMA_INIT_ERROR;
  }
  else
  {
    SetEvent(hIOReadyEvent);
  }

  HANDLE waitList[2] = {hNeedIOEvent, (HANDLE) hCompressionThread};
  if (WaitForMultipleObjects(2, waitList, FALSE, INFINITE) != WAIT_OBJECT_0)
  {
    // thread ended or WaitForMultipleObjects failed
    compressor_finished = TRUE;
    SetEvent(hIOReadyEvent);
    return LZMA_THREAD_ERROR;
  }

  if (compressor_finished)
  {
    return res;
  }

  return C_OK;
}

void CLZMA::GetMoreIO()
{
  SetEvent(hNeedIOEvent);
  if (WaitForSingleObject(hIOReadyEvent, INFINITE) != WAIT_OBJECT_0)
  {
    compressor_finished = TRUE;
    res = LZMA_THREAD_ERROR;
  }
}

void CLZMA::SetNextIn(char *in, unsigned int size)
{
  next_in = (LPBYTE) in;
  avail_in = size;
}

void CLZMA::SetNextOut(char *out, unsigned int size)
{
  next_out = (LPBYTE) out;
  avail_out = size;
}

char* CLZMA::GetNextOut()
{
  return (char *) next_out;
}

unsigned int CLZMA::GetAvailIn()
{
  return avail_in;
}

unsigned int CLZMA::GetAvailOut()
{
  return avail_out;
}

const TCHAR* CLZMA::GetName()
{
  return _T("lzma");
}

const TCHAR* CLZMA::GetErrStr(int err)
{
  switch (err)
  {
  case LZMA_BAD_CALL:
    return _T("bad call");
  case LZMA_INIT_ERROR:
    return _T("initialization failed");
  case LZMA_THREAD_ERROR:
    return _T("thread synchronization error");
  case LZMA_IO_ERROR:
    return _T("input/output error");
  case LZMA_MEM_ERROR:
    return _T("not enough memory");
  default:
    return _T("unknown error");
  }
}
