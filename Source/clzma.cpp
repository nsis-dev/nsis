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

int CLZMA::ConvertError(HRESULT result)
{
  if (result != S_OK)
  {
    if (result == E_OUTOFMEMORY)
      return LZMA_MEM_ERROR;
    else
      return LZMA_IO_ERROR;
  }
  return C_OK;
}

CLZMA::CLZMA(): _encoder(NULL)
{
  _encoder = new NCompress::NLZMA::CEncoder();
  _encoder->SetWriteEndMarkerMode(true);
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

  AddRef(); // will be manually deleted, not released
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
  if (_encoder)
  {
    delete _encoder;
    _encoder = NULL;
  }
}

int CLZMA::Init(int level, unsigned int dicSize, unsigned int dataSize)
{
  End();

  compressor_finished = FALSE;
  finish = FALSE;
  res = C_OK;

  if (!hNeedIOEvent || !hIOReadyEvent)
  {
    return LZMA_INIT_ERROR;
  }

  ResetEvent(hNeedIOEvent);
  ResetEvent(hIOReadyEvent);

  res = C_OK;

  PROPID propdIDs [] =
  {
    NCoderPropID::kAlgorithm,
    NCoderPropID::kDictionarySize,
    NCoderPropID::kNumFastBytes
  };
  const int kNumProps = COUNTOF(propdIDs);
  PROPVARIANT props[kNumProps];
  // NCoderPropID::kAlgorithm
  props[0].vt = VT_UI4;
  props[0].ulVal = 2;
  // NCoderPropID::kDictionarySize
  props[1].vt = VT_UI4;
  props[1].ulVal = dicSize;
  // NCoderPropID::kNumFastBytes
  props[2].vt = VT_UI4;
  props[2].ulVal = 64;
  if (_encoder->SetCoderProperties(propdIDs, props, kNumProps) != 0)
    return LZMA_INIT_ERROR;
  return _encoder->SetStreams(this, this, 0, 0) == S_OK ? C_OK : LZMA_INIT_ERROR;
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
    HRESULT hResult = _encoder->WriteCoderProperties(this);
    if (hResult == S_OK)
    {
      while (true)
      {
        UINT64 inSize, outSize;
        INT32 finished;
        hResult = _encoder->CodeOneBlock(&inSize, &outSize, &finished);
        if (hResult != S_OK && res == C_OK)
          res = ConvertError(hResult);
        if (res != C_OK)
          break;
        if (finished)
        {
          res = C_FINISHED;
          break;
        }
      }
    }
    else
    {
      if (res == C_OK)
        res = ConvertError(hResult);
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

HRESULT CLZMA::Read(void *data, UINT32 size, UINT32 *processedSize)
{
  return ReadPart(data, size, processedSize);
}

HRESULT CLZMA::ReadPart(void *data, UINT32 size, UINT32 *processedSize)
{
  if (processedSize)
    *processedSize = 0;
  while (size)
  {
    if (!avail_in)
    {
      if (finish)
      {
        return S_OK;
      }
      GetMoreIO();
      if (!avail_in)
      {
        if (finish)
        {
          return S_OK;
        }
        return E_ABORT;
      }
      if (compressor_finished)
        return E_ABORT;
    }
    UINT32 l = min(size, avail_in);
    memcpy(data, next_in, l);
    avail_in -= l;
    size -= l;
    next_in += l;
    data = LPBYTE(data) + l;
    if (processedSize)
      *processedSize += l;
  }
  return S_OK;
}

HRESULT CLZMA::Write(const void *data, UINT32 size, UINT32 *processedSize)
{
  return WritePart(data, size, processedSize);
}

HRESULT CLZMA::WritePart(const void *data, UINT32 size, UINT32 *processedSize)
{
  if (processedSize)
    *processedSize = 0;
  while (size)
  {
    if (!avail_out)
    {
      GetMoreIO();
      if (!avail_out)
        return E_ABORT;
    }
    UINT32 l = min(size, avail_out);
    memcpy(next_out, data, l);
    avail_out -= l;
    size -= l;
    next_out += l;
    data = LPBYTE(data) + l;
    if (processedSize)
      *processedSize += l;
  }
  return S_OK;
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
