/*
 * fileform.c
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
 * Unicode support by Jim Park -- 08/13/2007
 */

#include "../Platform.h"
#include "fileform.h"
#include "util.h"
#include "state.h"
#include "resource.h"
#include "lang.h"
#include "ui.h"
#include "exec.h"
#include "../crc32.h"
#include "../tchar.h"

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
#ifdef NSIS_COMPRESS_USE_ZLIB
#include "../zlib/ZLIB.H"
#endif

#ifdef NSIS_COMPRESS_USE_LZMA
#include "../7zip/LZMADecode.h"
#define z_stream lzma_stream
#define inflateInit(x) lzmaInit(x)
#define inflateReset(x) lzmaInit(x)
#define inflate(x) lzmaDecode(x)
#define Z_OK LZMA_OK
#define Z_STREAM_END LZMA_STREAM_END
#endif

#ifdef NSIS_COMPRESS_USE_BZIP2
#include "../bzip2/bzlib.h"

#define z_stream DState
#define inflateInit(x) BZ2_bzDecompressInit(x)
#define inflateReset(x) BZ2_bzDecompressInit(x)

#define inflate(x) BZ2_bzDecompress(x)
#define Z_OK BZ_OK
#define Z_STREAM_END BZ_STREAM_END
#endif//NSIS_COMPRESS_USE_BZIP2

#ifdef NSIS_COMPRESS_USE_ZSTD

#include "../zstd/lib/zstd.h"

#define z_stream ZStdContext
#define Z_OK 0
#define Z_ERR -1
#define Z_STREAM_END 1

typedef struct ZStdContext_s {
  ZSTD_DStream*   dstream;
  ZSTD_outBuffer  outBuffer;
  ZSTD_inBuffer   inBuffer;
  unsigned char*  next_in;
  unsigned int    avail_in;
  unsigned char*  next_out;
  unsigned int    avail_out;
} ZStdContext;

int inflate(ZStdContext* ctx)
{
  ctx->inBuffer.src = ctx->next_in;
  ctx->inBuffer.size = ctx->avail_in;
  ctx->inBuffer.pos = 0;
  ctx->outBuffer.dst = ctx->next_out;
  ctx->outBuffer.size = ctx->avail_out;
  ctx->outBuffer.pos = 0;
  size_t ret = ZSTD_decompressStream(ctx->dstream, &ctx->outBuffer, &ctx->inBuffer);
  ctx->avail_in = ctx->inBuffer.size - ctx->inBuffer.pos;
  ctx->next_in = (unsigned char*)ctx->inBuffer.src + ctx->inBuffer.pos;
  ctx->avail_out = ctx->outBuffer.size - ctx->outBuffer.pos;
  ctx->next_out = (unsigned char*)ctx->outBuffer.dst + ctx->outBuffer.pos;
  if (ret == 0) return Z_STREAM_END;
  if (ZSTD_isError(ret)) return Z_ERR;
  return Z_OK;
}

void inflateReset(ZStdContext* ctx)
{
  if (!ctx->dstream)
  {
    ctx->dstream = ZSTD_createDStream();
    ZSTD_DCtx_setParameter(ctx->dstream, ZSTD_d_format, ZSTD_f_zstd1_magicless);
  }
  ZSTD_initDStream(ctx->dstream);
}

#endif//NSIS_COMPRESS_USE_ZSTD

#endif//NSIS_CONFIG_COMPRESSION_SUPPORT

struct block_header g_blocks[BLOCKS_NUM];
header *g_header;
int g_flags;
UINT g_filehdrsize;
int g_is_uninstaller;

HANDLE g_db_hFile=INVALID_HANDLE_VALUE;

#if defined(NSIS_CONFIG_COMPRESSION_SUPPORT) && defined(NSIS_COMPRESS_WHOLE)
HANDLE dbd_hFile=INVALID_HANDLE_VALUE;
static int dbd_size, dbd_pos, dbd_srcpos, dbd_fulllen;
#endif//NSIS_COMPRESS_WHOLE

static MAXSIZETYPE m_length;
static UINT m_pos;

#define _calc_percent() (MulDiv(min(m_pos,m_length),100,m_length))
#ifdef NSIS_COMPRESS_WHOLE
static int NSISCALL calc_percent()
{
  return _calc_percent();
}
#else
#define calc_percent() _calc_percent()
#endif

#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
#if defined(NSIS_CONFIG_CRC_SUPPORT) || defined(NSIS_COMPRESS_WHOLE)
INT_PTR CALLBACK verProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetTimer(hwndDlg,1,250,NULL);
    uMsg = WM_TIMER;
  }
  if (uMsg == WM_TIMER)
  {
    TCHAR bt[64];
    int percent=calc_percent();
#ifdef NSIS_COMPRESS_WHOLE
    TCHAR *msg=g_header?_LANG_UNPACKING:_LANG_VERIFYINGINST;
#else
    TCHAR *msg=_LANG_VERIFYINGINST;
#endif
    wsprintf(bt,msg,percent);
    my_SetWindowText(hwndDlg,bt);
    my_SetDialogItemText(hwndDlg,IDC_STR,bt);
  }
  return FALSE;
}

DWORD verify_time;

void handle_ver_dlg(BOOL kill)
{
  static HWND hwnd;

  if (kill)
  {
    if (hwnd) DestroyWindow(hwnd);
    hwnd = NULL;

    return;
  }

  if (hwnd)
  {
    MessageLoop(0);
  }
  else if (GetTickCount() > verify_time)
  {
#ifdef NSIS_COMPRESS_WHOLE
    if (g_hwnd)
    {
      if (g_exec_flags.status_update & 1)
      {
        TCHAR bt[64];
        wsprintf(bt, _T("... %d%%"), calc_percent());
        update_status_text(0, bt);
      }
    }
    else
#endif
    {
      hwnd = CreateDialog(
        g_hInstance,
        MAKEINTRESOURCE(IDD_VERIFY),
        0,
        verProc
      );
      ShowWindow(hwnd, SW_SHOW);
    }
  }
}

#endif//NSIS_CONFIG_CRC_SUPPORT || NSIS_COMPRESS_WHOLE
#endif//NSIS_CONFIG_VISIBLE_SUPPORT

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
static z_stream g_inflate_stream;
#endif

const TCHAR * NSISCALL loadHeaders(int cl_flags)
{
  MAXSIZETYPE left;
#ifdef NSIS_CONFIG_CRC_SUPPORT
  crc32_t crc = 0;
  int do_crc = 0;
#endif//NSIS_CONFIG_CRC_SUPPORT

  void *data;
  firstheader h;
  header *header;

  HANDLE db_hFile;
  enum { opentotalwait = 1500, opentrywait = 250, maxopentries = opentotalwait / opentrywait };
  UINT opentries = 0;

#ifdef C_ASSERT
{C_ASSERT(sizeof(firstheader) == sizeof(int) * 7);}
{C_ASSERT(sizeof(struct block_header) == sizeof(UINT_PTR) + sizeof(int));}
{C_ASSERT(LASIF_FITCTLW >> LASIS_FITCTLW == 1);}
{C_ASSERT(LASIF_LR_LOADFROMFILE == LR_LOADFROMFILE);}
{C_ASSERT(opentotalwait == opentrywait * maxopentries);}
#endif

#ifdef NSIS_CONFIG_CRC_SUPPORT
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
  verify_time = GetTickCount() + 1000;
#endif
#endif//NSIS_CONFIG_CRC_SUPPORT

  GetModuleFileName(NULL, state_exe_path, NSIS_MAX_STRLEN);
retry:
  g_db_hFile = db_hFile = myOpenFile(state_exe_path, GENERIC_READ, OPEN_EXISTING);
  if (db_hFile == INVALID_HANDLE_VALUE)
  {
    UINT error = GetLastError();
    if (error == ERROR_SHARING_VIOLATION && ++opentries <= maxopentries)
    {
        Sleep(opentrywait);
        goto retry;
    }
    return _LANG_CANTOPENSELF;
  }

  mystrcpy(state_exe_directory, state_exe_path);
  mystrcpy(state_exe_file, trimslashtoend(state_exe_directory));

  left = m_length = GetFileSize(db_hFile,NULL);
  while (left > 0)
  {
    static char temp[32768];
    DWORD l = min(left, (g_filehdrsize ? 32768UL : 512UL));
    if (!ReadSelfFile(temp, l))
    {
#if defined(NSIS_CONFIG_CRC_SUPPORT) && defined(NSIS_CONFIG_VISIBLE_SUPPORT)
      handle_ver_dlg(TRUE);
#endif//NSIS_CONFIG_CRC_SUPPORT
      return _LANG_INVALIDCRC;
    }

    if (!g_filehdrsize)
    {
      mini_memcpy(&h, temp, sizeof(firstheader));
      if (
           (h.flags & (~FH_FLAGS_MASK)) == 0 &&
           h.siginfo == FH_SIG &&
           h.nsinst[2] == FH_INT3 &&
           h.nsinst[1] == FH_INT2 &&
           h.nsinst[0] == FH_INT1
         )
      {
        g_filehdrsize = m_pos;

#if defined(NSIS_CONFIG_CRC_SUPPORT) || defined(NSIS_CONFIG_SILENT_SUPPORT)
        cl_flags |= h.flags;
#endif

#ifdef NSIS_CONFIG_SILENT_SUPPORT
        g_exec_flags.silent |= cl_flags & FH_FLAGS_SILENT;
#endif

        if ((MAXEXEDATASIZETYPE) h.length_of_all_following_data > left)
          return _LANG_INVALIDCRC;

#ifdef NSIS_CONFIG_CRC_SUPPORT
        if ((cl_flags & FH_FLAGS_FORCE_CRC) == 0)
        {
          if (cl_flags & FH_FLAGS_NO_CRC)
            break;
        }

        do_crc++;

#ifndef NSIS_CONFIG_CRC_ANAL
        left = h.length_of_all_following_data - 4;
        // end crc checking at crc :) this means you can tack stuff on the end and it'll still work.
#else //!NSIS_CONFIG_CRC_ANAL
        left -= 4;
#endif//NSIS_CONFIG_CRC_ANAL
        // this is in case the end part is < 512 bytes.
        if (l > (DWORD)left) l=(DWORD)left;

#else//!NSIS_CONFIG_CRC_SUPPORT
        // no crc support, no need to keep on reading
        break;
#endif//!NSIS_CONFIG_CRC_SUPPORT
      }
    }
#ifdef NSIS_CONFIG_CRC_SUPPORT

#ifdef NSIS_CONFIG_VISIBLE_SUPPORT

#ifdef NSIS_CONFIG_SILENT_SUPPORT
    else if ((cl_flags & FH_FLAGS_SILENT) == 0)
#endif//NSIS_CONFIG_SILENT_SUPPORT
    {
      handle_ver_dlg(FALSE);
    }
#endif//NSIS_CONFIG_VISIBLE_SUPPORT

#ifndef NSIS_CONFIG_CRC_ANAL
    if (left < m_length)
#endif//NSIS_CONFIG_CRC_ANAL
      crc = CRC32(crc, (unsigned char*)temp, l);

#endif//NSIS_CONFIG_CRC_SUPPORT
    m_pos += l;
    left -= l;
  }
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
#ifdef NSIS_CONFIG_CRC_SUPPORT
  handle_ver_dlg(TRUE);
#endif//NSIS_CONFIG_CRC_SUPPORT
#endif//NSIS_CONFIG_VISIBLE_SUPPORT
  if (!g_filehdrsize)
    return _LANG_INVALIDCRC;

#ifdef NSIS_CONFIG_CRC_SUPPORT
  if (do_crc)
  {
    crc32_t fcrc;
    SetSelfFilePointer(m_pos);
    if (!ReadSelfFile(&fcrc, sizeof(crc32_t)) || crc != fcrc)
      return _LANG_INVALIDCRC;
  }
#endif//NSIS_CONFIG_CRC_SUPPORT

  data = (void *)GlobalAlloc(GPTR,h.length_of_header);

#ifdef NSIS_COMPRESS_WHOLE
  inflateReset(&g_inflate_stream);

  {
    TCHAR fno[MAX_PATH];
    my_GetTempFileName(fno, state_temp_dir);
    dbd_hFile=CreateFile(fno,GENERIC_WRITE|GENERIC_READ,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);
    if (dbd_hFile == INVALID_HANDLE_VALUE)
      return _LANG_ERRORWRITINGTEMP;
  }
  dbd_srcpos = SetSelfFilePointer(g_filehdrsize + sizeof(firstheader));
#ifdef NSIS_CONFIG_CRC_SUPPORT
  dbd_fulllen = dbd_srcpos - sizeof(h) + h.length_of_all_following_data - ((h.flags & FH_FLAGS_NO_CRC) ? 0 : sizeof(crc32_t));
#else
  dbd_fulllen = dbd_srcpos - sizeof(h) + h.length_of_all_following_data;
#endif//NSIS_CONFIG_CRC_SUPPORT
#else
  SetSelfFilePointer(g_filehdrsize + sizeof(firstheader));
#endif//NSIS_COMPRESS_WHOLE

  if (GetCompressedDataFromDataBlockToMemory(-1, data, h.length_of_header) != h.length_of_header)
  {
    return _LANG_INVALIDCRC;
  }

  header = g_header = data;

  g_flags = header->flags;

#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
  if (h.flags & FH_FLAGS_UNINSTALL)
    g_is_uninstaller++;
#endif

  // set offsets to real memory offsets rather than installer's header offset
  left = BLOCKS_NUM;
  while (left--)
  {
#ifdef DEBUG
    if ((UINT_PTR) h.length_of_header < header->blocks[left].offset)
      return _LANG_GENERIC_ERROR; // Should never happen
#endif
    header->blocks[left].offset += (UINT_PTR) data;
  }

#ifdef NSIS_COMPRESS_WHOLE
  header->blocks[NB_DATA].offset = dbd_pos;
#else
  header->blocks[NB_DATA].offset = SetFilePointer(db_hFile,0,NULL,FILE_CURRENT);
#endif

  mini_memcpy(&g_blocks, &header->blocks, sizeof(g_blocks));

  return 0;
}

#ifdef NSIS_COMPRESS_USE_ZSTD
  #define IBUFSIZE ZSTD_BLOCKSIZE_MAX + 3
  #define OBUFSIZE ZSTD_BLOCKSIZE_MAX
#else
  #define IBUFSIZE 16384
  #define OBUFSIZE 32768
#endif

// returns -3 if compression error/eof/etc

#if !defined(NSIS_COMPRESS_WHOLE) || !defined(NSIS_CONFIG_COMPRESSION_SUPPORT)

// Decompress data.
int NSISCALL _dodecomp(int offset, HANDLE hFileOut, unsigned char *outbuf, int outbuflen)
{
  static char inbuffer[IBUFSIZE+OBUFSIZE];
  char *outbuffer;
  int outbuffer_len=outbuf?outbuflen:OBUFSIZE;
  int retval=0;
  int input_len;

  outbuffer = outbuf?(char*)outbuf:(inbuffer+IBUFSIZE);

  if (offset>=0)
  {
    UINT_PTR datofs=g_blocks[NB_DATA].offset+offset;
#if (NSIS_MAX_EXEDATASIZE+0) > 0x7fffffffUL
#error "SetFilePointer is documented to only support signed 32-bit offsets in lDistanceToMove"
#endif
    const int pos=(int)datofs;
    SetSelfFilePointer(pos);
  }

  if (!ReadSelfFile((LPVOID)&input_len,sizeof(int))) return -3;

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
  if (input_len & 0x80000000) // compressed
  {
    TCHAR progress[64];
    int input_len_total;
    DWORD ltc = GetTickCount(), tc;

    inflateReset(&g_inflate_stream);
    input_len_total = input_len &= 0x7fffffff; // take off top bit.

    while (input_len > 0)
    {
      int l=min(input_len,IBUFSIZE);
      int err;

      if (!ReadSelfFile((LPVOID)inbuffer,l))
        return -3;

      g_inflate_stream.next_in = (unsigned char*) inbuffer;
      g_inflate_stream.avail_in = l;
      input_len-=l;

      for (;;)
      {
        int u;

        g_inflate_stream.next_out = (unsigned char*) outbuffer;
        g_inflate_stream.avail_out = (unsigned int)outbuffer_len;

        err=inflate(&g_inflate_stream);

        if (err<0) return -4;

        u=BUGBUG64TRUNCATE(int, (size_t)((char*)g_inflate_stream.next_out - outbuffer));

        tc=GetTickCount();
        if (g_exec_flags.status_update & 1 && (tc - ltc > 200 || !input_len))
        {
          wsprintf(progress, _T("... %d%%"), MulDiv(input_len_total - input_len, 100, input_len_total));
          update_status_text(0, progress);
          ltc=tc;
        }

        // if there's no output, more input is needed
        if (!u)
          break;

        if (!outbuf)
        {
          if (!myWriteFile(hFileOut,outbuffer,u)) return -2;
          retval+=u;
        }
        else
        {
          retval+=u;
          outbuffer_len-=u;
          outbuffer=(char*)g_inflate_stream.next_out;
        }
        if (err==Z_STREAM_END) return retval;
      }
    }
  }
  else
#endif//NSIS_CONFIG_COMPRESSION_SUPPORT
  {
    if (!outbuf)
    {
      while (input_len > 0)
      {
        DWORD l=min(input_len,outbuffer_len);
        if (!ReadSelfFile((LPVOID)inbuffer,l)) return -3;
        if (!myWriteFile(hFileOut,inbuffer,l)) return -2;
        retval+=l;
        input_len-=l;
      }
    }
    else
    {
      int l=min(input_len,outbuflen);
      if (!ReadSelfFile((LPVOID)outbuf,l)) return -3;
      retval=l;
    }
  }
  return retval;
}
#else//NSIS_COMPRESS_WHOLE

static char _inbuffer[IBUFSIZE];
static char _outbuffer[OBUFSIZE];
extern MAXSIZETYPE m_length;
extern UINT m_pos;
extern INT_PTR CALLBACK verProc(HWND, UINT, WPARAM, LPARAM);
extern INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);
static int NSISCALL __ensuredata(int amount)
{
  int needed=amount-(dbd_size-dbd_pos);
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
  verify_time=GetTickCount()+500;
#endif
  if (needed>0)
  {
    SetSelfFilePointer(dbd_srcpos);
    SetFilePointer(dbd_hFile,dbd_size,NULL,FILE_BEGIN);
    m_length=needed;
    m_pos=0;
    for (;;)
    {
      int err;
      int l=min(IBUFSIZE,dbd_fulllen-dbd_srcpos);
      if (!ReadSelfFile((LPVOID)_inbuffer,l)) return -1;
      dbd_srcpos+=l;
      g_inflate_stream.next_in=(unsigned char*)_inbuffer;
      g_inflate_stream.avail_in=l;
      do
      {
        DWORD r;
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
        if (g_header)
#ifdef NSIS_CONFIG_SILENT_SUPPORT
          if (!g_exec_flags.silent)
#endif
          {
            m_pos=m_length-(amount-(dbd_size-dbd_pos));

            handle_ver_dlg(FALSE);
          }
#endif//NSIS_CONFIG_VISIBLE_SUPPORT
        g_inflate_stream.next_out=(unsigned char*)_outbuffer;
        g_inflate_stream.avail_out=OBUFSIZE;
        err=inflate(&g_inflate_stream);
        if (err<0)
        {
          return -3;
        }
        r=BUGBUG64TRUNCATE(DWORD,(UINT_PTR)g_inflate_stream.next_out)-BUGBUG64TRUNCATE(DWORD,(UINT_PTR)_outbuffer);
        if (r)
        {
          if (!myWriteFile(dbd_hFile,_outbuffer,r))
          {
            return -2;
          }
          dbd_size+=r;
        }
        else if (g_inflate_stream.avail_in || !l) return -3;
        else break;
      }
      while (g_inflate_stream.avail_in);
      if (amount-(dbd_size-dbd_pos) <= 0) break;
    }
    SetFilePointer(dbd_hFile,dbd_pos,NULL,FILE_BEGIN);
  }
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
  handle_ver_dlg(TRUE);
#endif//NSIS_CONFIG_VISIBLE_SUPPORT
  return 0;
}


int NSISCALL _dodecomp(int offset, HANDLE hFileOut, unsigned char *outbuf, int outbuflen)
{
  DWORD r;
  int input_len;
  int retval;
  if (offset>=0)
  {
    UINT_PTR datofs=g_blocks[NB_DATA].offset+offset;
#if (NSIS_MAX_EXEDATASIZE+0) > 0x7fffffffUL
#error "SetFilePointer is documented to only support signed 32-bit offsets in lDistanceToMove"
#endif
    dbd_pos=(int)datofs;
    SetFilePointer(dbd_hFile,dbd_pos,NULL,FILE_BEGIN);
  }
  retval=__ensuredata(sizeof(int));
  if (retval<0) return retval;

  if (!myReadFile(dbd_hFile,(LPVOID)&input_len,sizeof(int))) return -3;
  dbd_pos+=sizeof(int);

  retval=__ensuredata(input_len);
  if (retval < 0) return retval;

  if (!outbuf)
  {
    while (input_len > 0)
    {
      DWORD l=min(input_len,IBUFSIZE);
      if (!myReadFile(dbd_hFile,(LPVOID)_inbuffer,r=l)) return -3;
      if (!myWriteFile(hFileOut,_inbuffer,r)) return -2;
      retval+=r;
      input_len-=r;
      dbd_pos+=r;
    }
  }
  else
  {
    if (!ReadFile(dbd_hFile,(LPVOID)outbuf,min(input_len,outbuflen),&r,NULL)) return -3;
    retval=r;
    dbd_pos+=r;
  }
  return retval;
}
#endif//NSIS_COMPRESS_WHOLE

BOOL NSISCALL ReadSelfFile(LPVOID lpBuffer, DWORD nNumberOfBytesToRead)
{
  return myReadFile(g_db_hFile,lpBuffer,nNumberOfBytesToRead);
}

DWORD NSISCALL SetSelfFilePointer(LONG lDistanceToMove)
{
  return SetFilePointer(g_db_hFile,lDistanceToMove,NULL,FILE_BEGIN);
}
