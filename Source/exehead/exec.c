/*
 * exec.c
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
#include <shlobj.h>
#include <olectl.h>
#include "fileform.h"
#include "util.h"
#include "state.h"
#include "ui.h"
#include "components.h"
#include "exec.h"
#include "plugin.h"
#include "lang.h"
#include "resource.h"
#include "api.h"
#include "../tchar.h"

#define EXEC_ERROR 0x7FFFFFFF

#ifdef NSIS_CONFIG_COMPONENTPAGE
HWND g_SectionHack;
#endif

#ifdef NSIS_SUPPORT_STACK
typedef struct _stack_t {
  struct _stack_t *next;
  TCHAR text[NSIS_MAX_STRLEN];
} stack_t;

static stack_t *g_st;
#endif


exec_flags_t g_exec_flags_last_used;
execflags_and_osinfo g_execflags_and_osinfo;

extra_parameters plugin_extra_parameters = {
  &g_exec_flags,
  &ExecuteCodeSegment,
  &validate_filename,
  &RegisterPluginCallback
};

#if defined(NSIS_SUPPORT_ACTIVEXREG) || defined(NSIS_SUPPORT_CREATESHORTCUT)
HRESULT g_hres;
#endif

static int NSISCALL ExecuteEntry(entry *entry_);

#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
// Convert OLE-supported file formats to a caller-owned bitmap compatible with STM_SETIMAGE.
static HBITMAP NSISCALL LoadOLEPictureBitmap(LPCTSTR path, int width, int height)
{
  IPicture *picture = NULL;
  IDispatch *dispatch = NULL;
  LPOLESTR olepath;
  VARIANT filename;
  HBITMAP bitmap = NULL;
  HDC screen = NULL, target = NULL;
  HGDIOBJ old;
  OLE_HANDLE picturehandle;
  BITMAP source;
  LONG picturewidth, pictureheight;
  HRESULT hr, init;
  DWORD attributes;
#ifndef _UNICODE
  WCHAR widepath[NSIS_MAX_STRLEN];
#endif

  attributes = GetFileAttributes(path);
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return NULL;
#ifndef _UNICODE
  if (!MultiByteToWideChar(CP_ACP, 0, path, -1, widepath, COUNTOF(widepath))) return NULL;
  olepath = widepath;
#else
  olepath = (LPOLESTR)path;
#endif

  init = OleInitialize(NULL);
  VariantInit(&filename);
  V_VT(&filename) = VT_BSTR;
  V_BSTR(&filename) = SysAllocString(olepath);
  if (V_BSTR(&filename))
  {
    hr = OleLoadPictureFileEx(filename, 0, 0, LP_DEFAULT, &dispatch);
    if (SUCCEEDED(hr))
      hr = dispatch->lpVtbl->QueryInterface(dispatch, &IID_IPicture, (void**)&picture);
  }
  else
    hr = E_OUTOFMEMORY;
  VariantClear(&filename);
  if (dispatch) dispatch->lpVtbl->Release(dispatch);
  if (SUCCEEDED(hr) &&
      SUCCEEDED(picture->lpVtbl->get_Width(picture, &picturewidth)) &&
      SUCCEEDED(picture->lpVtbl->get_Height(picture, &pictureheight)) &&
      SUCCEEDED(picture->lpVtbl->get_Handle(picture, &picturehandle)) &&
      GetObject((HGDIOBJ)(ULONG_PTR)picturehandle, sizeof(source), &source) &&
      (screen = GetDC(NULL)) != NULL)
  {
    if (!width) width = source.bmWidth;
    if (!height) height = source.bmHeight;
    if (width > 0 && height > 0 &&
        (target = CreateCompatibleDC(screen)) != NULL &&
        (bitmap = CreateCompatibleBitmap(screen, width, height)) != NULL)
    {
      old = SelectObject(target, bitmap);
      if (!old || old == HGDI_ERROR)
        hr = E_FAIL;
      else
      {
        SetStretchBltMode(target, HALFTONE);
        SetBrushOrgEx(target, 0, 0, NULL);
        hr = picture->lpVtbl->Render(picture, target, 0, 0, width, height,
                                     0, pictureheight, picturewidth, -pictureheight, NULL);
        SelectObject(target, old);
      }
      if (FAILED(hr))
      {
        DeleteObject(bitmap);
        bitmap = NULL;
      }
    }
  }
  if (target) DeleteDC(target);
  if (screen) ReleaseDC(NULL, screen);
  if (picture) picture->lpVtbl->Release(picture);
  if (SUCCEEDED(init)) OleUninitialize();
  return bitmap;
}
#endif

/**
 * If v is negative, then the address to resolve is actually
 * stored in the global user variables.  Convert the value
 * to integer and return.
 */
int NSISCALL resolveaddr(int v)
{
  if (v < 0)
  {
    return myatoi(g_usrvars[-(v+1)]);
  }
  return v;
}

int NSISCALL ExecuteCodeSegment(int pos, HWND hwndProgress)
{
  while (pos >= 0)
  {
    int rv;
    if (g_entries[pos].which == EW_RET) return 0;
    rv=ExecuteEntry(g_entries + pos);
    if (rv == EXEC_ERROR) return EXEC_ERROR;

    rv=resolveaddr(rv);

    if (!rv) { rv++; pos++; }
    else
    {
      int t=pos;
      rv--; // rv is decremented here by 1, since it was +1 on the other end.
      pos=rv; // set new position
      rv-=t; // set rv to delta for progress adjustment
    }

    if (hwndProgress)
    {
      extern int progress_bar_pos, progress_bar_len;
      progress_bar_pos+=rv;
      SendMessage(hwndProgress,PBM_SETPOS,MulDiv(progress_bar_pos,30000,progress_bar_len),0);
    }
  }
  return 0;
}

#ifdef NSIS_SUPPORT_CODECALLBACKS

int NSISCALL ExecuteCallbackFunction(int num)
{
  return ExecuteCodeSegment(*(&g_header->code_onInit + num), NULL);
}

#endif

static TCHAR g_bufs[5][NSIS_MAX_STRLEN];
static int *g_parms;

void NSISCALL update_status_text_buf1(int strtab)
{
  update_status_text(strtab, g_bufs[1]);
}

#ifdef _WIN64
static INT_PTR NSISCALL GetIntPtrFromParm(int id)
{
  return strtoiptr(GetNSISStringTT(g_parms[id]));
}
#else
#define GetIntPtrFromParm(id_) ( (INT32)(GetIntFromParmEx(id_).LowPart) )
#endif
#define GetHwndFromParm(id_) ( (HWND)GetIntPtrFromParm(id_) )
#define GetIntFromParm(id_) ( (INT32)(UINT32)GetIntPtrFromParm(id_) )
static LARGE_INTEGER GetIntFromParmEx(int id)
{
  LARGE_INTEGER v;
  const TCHAR *p = GetNSISStringTT(g_parms[id]);
  v.LowPart = myatoi(p), v.HighPart = *p;
  return v; // HighPart is non-zero if the string is not empty
}

// NB - USE CAUTION when rearranging code to make use of the new return value of
// this function - be sure the parm being accessed is not modified before the call.
// Use a negative number to get the string validated as a file name
// Note: Calling GetNSISString has the side effect that the buffer holding
// the string to expand gets modified.
// When calling this function with numbers like 0x13, it means create the string
// from the string ID found in entry.offset[3] and put it into g_bufs[1].
static TCHAR * NSISCALL GetStringFromParm(int id_)
{
  int id = id_ < 0 ? -id_ : id_;
  TCHAR *result = GetNSISString(g_bufs[id >> 4], g_parms[id & 0xF]);
  if (id_ < 0) validate_filename(result);
  return result;
}

#ifdef NSIS_SUPPORT_REGISTRYFUNCTIONS
static HKEY NSISCALL GetRegRootKey(int RootKey)
{
  if (RootKey < 0) return (HKEY) (UINT_PTR) RootKey;
  return (HKEY) ((UINT_PTR) HKEY_CURRENT_USER + RootKey + g_exec_flags.all_user_var); // SHCTX[32|64|ANY]: HKEY_CURRENT_USER + 1 == HKEY_LOCAL_MACHINE
}
static HKEY NSISCALL RegOpenScriptKey(REGSAM RS)
{
  HKEY hKey;
  return RegKeyOpen(GetRegRootKey(g_parms[1]), GetStringFromParm(0x22), RS|KEY_FROMSCRIPT, &hKey) ? NULL : hKey;
}
static HKEY NSISCALL RegCreateScriptKey(int RootKey, LPCTSTR SubKey, REGSAM RS)
{
  HKEY hKey;
  return RegKeyCreate(GetRegRootKey(RootKey), SubKey, RS|KEY_FROMSCRIPT, &hKey) ? NULL : hKey;
}

// RegDeleteKey on Win9x will delete a tree of keys, WinNT will only delete a key without subkeys.
// RegDeleteKeyEx on 32-bit Windows accepts but ignores the KEY_WOW64_xxKEY flags and always uses the 
// one and only native key. Our RegKeyOpen will intentionally fail if a incompatible WoW64 flag is used.
#define DRTF_ONLYIFNOSUBKEYS DELREGKEY_ONLYIFNOSUBKEYS
#define DRTF_ONLYIFNOVALUES  DELREGKEY_ONLYIFNOVALUES
static LONG NSISCALL DeleteRegTree(HKEY hThisKey, LPCTSTR SubKey, REGSAM SamviewAndFlags)
{
  HKEY hKey;
  UINT onlyifnosubkeys = SamviewAndFlags & DRTF_ONLYIFNOSUBKEYS;
  UINT onlyifnovalues  = SamviewAndFlags & DRTF_ONLYIFNOVALUES, valuesexistcheckinsubkeys = TRUE;
  REGSAM samview = SamviewAndFlags & (KEY_WOW64_32KEY|KEY_WOW64_64KEY);
  LONG retval = RegKeyOpen(hThisKey, SubKey, KEY_ENUMERATE_SUB_KEYS|KEY_QUERY_VALUE|samview, &hKey);
  if (retval == ERROR_SUCCESS)
  {
    TCHAR child[MAX_PATH+1]; // NB - don't change this to static (recursive function)
    if (onlyifnovalues)
    {
      DWORD cchName = 0;
      retval = RegEnumValue(hKey, 0, child, &cchName, NULL, NULL, NULL, NULL);
      if (retval != ERROR_NO_MORE_ITEMS) goto notempty;
      if (!valuesexistcheckinsubkeys) SamviewAndFlags &= ~DRTF_ONLYIFNOVALUES;
    }
    while (RegEnumKey(hKey, 0, child, COUNTOF(child)) == ERROR_SUCCESS)
    {
      if (onlyifnosubkeys) notempty: return (RegCloseKey(hKey), ERROR_CAN_NOT_COMPLETE);
      if ((retval = DeleteRegTree(hKey, child, SamviewAndFlags)) != ERROR_SUCCESS) break;
    }
    RegCloseKey(hKey);
    {
      typedef LONG (WINAPI * RegDeleteKeyExType)(HKEY, LPCTSTR, REGSAM, DWORD);
      RegDeleteKeyExType RDKE = (RegDeleteKeyExType)
      #if !defined(_WIN64) || defined(_M_IA64)
        myGetProcAddress(MGA_RegDeleteKeyEx);
      if (!RDKE)
        retval = RegDeleteKey(hThisKey, SubKey);
      else
      #else
        RegDeleteKeyEx;
      #endif
        retval = RDKE(hThisKey, SubKey, samview, 0);
    }
  }
  return retval;
}
static LONG NSISCALL RegDeleteScriptKey(int RootKey, LPCTSTR SubKey, REGSAM SamviewAndFlags)
{
  HKEY hKey;
  if (!SubKey[0]) return ERROR_CAN_NOT_COMPLETE; // Don't allow scripts to delete a HKEY root
  SamviewAndFlags |= KEY_FROMSCRIPT;
  hKey = GetRegKeyAndSAM(GetRegRootKey(RootKey), &SamviewAndFlags);
  return hKey ? DeleteRegTree(hKey, SubKey, SamviewAndFlags) : ERROR_INVALID_HANDLE; // ERROR_CANTOPEN?
}
#endif//NSIS_SUPPORT_REGISTRYFUNCTIONS

// returns EXEC_ERROR on error
// returns 0, advance position by 1
// otherwise, returns new_position+1
static int NSISCALL ExecuteEntry(entry *entry_)
{
  TCHAR *buf0 = g_bufs[0];
  TCHAR *buf1 = g_bufs[1];
  TCHAR *buf2 = g_bufs[2];
  TCHAR *buf3 = g_bufs[3];
  //char *buf4 = g_bufs[4];

  TCHAR *var0;
  TCHAR *var1;
  //char *var2;
  //char *var3;
  //char *var4;
  //char *var5;

#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
  // Saves 8 bytes
  HWND mainHwnd = g_hwnd;
#define g_hwnd mainHwnd
#endif

  int exec_error = 0;

  entry lent = *entry_;

#define which (lent.which)
#define parm0 (lent.offsets[0])
#define parm1 (lent.offsets[1])
#define parm2 (lent.offsets[2])
#define parm3 (lent.offsets[3])
#define parm4 (lent.offsets[4])
#define parm5 (lent.offsets[5])

  var0 = g_usrvars[parm0];
  var1 = g_usrvars[parm1];
  // not used yet
  //var2 = g_usrvars[parm2];
  //var3 = g_usrvars[parm3];
  //var4 = g_usrvars[parm4];
  //var5 = g_usrvars[parm5];

#if __GNUC__ >= 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
  g_parms = lent.offsets;
#if __GNUC__ >= 12
#pragma GCC diagnostic pop
#endif

  switch (which)
  {
    case EW_NOP:
      log_printf2(_T("Jump: %d"),parm0);
    return parm0;
    case EW_ABORT:
      {
        log_printf2(_T("Aborting: \"%s\""),GetStringFromParm(0x00));
        update_status_text(parm0,0);
      }
    return EXEC_ERROR;
    case EW_QUIT:
      g_quit_flag++;
      if (g_hwnd) PostQuitMessage(0); // make sure we bail out fast.
    return EXEC_ERROR;
    case EW_CALL:
      {
        int v=resolveaddr(parm0)-1;  // address is -1, since we encode it as +1
        log_printf2(_T("Call: %d"),v);
        return ExecuteCodeSegment(v,NULL);
      }
    case EW_UPDATETEXT:
      log_printf2(_T("DetailPrint: %s"),GetStringFromParm(0x00));
      update_status_text(parm0,0);
    break;
    case EW_SLEEP:
      {
        int x=GetIntFromParm(0);
        log_printf2(_T("Sleep(%d)"),x);
        Sleep(max(x,1));
      }
    break;
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
    case EW_BRINGTOFRONT:
      log_printf(_T("BringToFront"));
      SetForegroundWindow(g_hwnd);
    break;
#endif//NSIS_CONFIG_VISIBLE_SUPPORT
    case EW_SETFLAG:
      {
        // TODO push/pop flags instead -- https://sourceforge.net/p/nsis/patches/222/
        static int g_statusuphack = 0;
        if (parm2 <= 0)
        {
          if (parm2 < 0)
            g_statusuphack=FIELDN(g_exec_flags,parm0);
          else
            FIELDN(g_exec_flags_last_used,parm0)=FIELDN(g_exec_flags,parm0);
          FIELDN(g_exec_flags,parm0)=GetIntFromParm(1);
          log_printf3(_T("SetFlag: %d=%d"),parm0,FIELDN(g_exec_flags,parm0));
        }
        else
        {
          FIELDN(g_exec_flags,parm0)=FIELDN(g_exec_flags_last_used,parm0);
          if (parm3 < 0)
            FIELDN(g_exec_flags,parm0)=g_statusuphack;
        }
      }
    break;
    case EW_IFFLAG:
    {
      int f=lent.offsets[!FIELDN(g_exec_flags,parm2)];
      FIELDN(g_exec_flags,parm2)&=parm3;
      return f;
    }
    case EW_GETFLAG:
      myitoa(var0,FIELDN(g_exec_flags,parm1));
    break;
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
    case EW_CHDETAILSVIEW:
      if (insthwndbutton) ShowWindow(insthwndbutton,parm1);
      if (insthwnd) ShowWindow(insthwnd,parm0);
    break;
#endif//NSIS_CONFIG_VISIBLE_SUPPORT
    case EW_SETFILEATTRIBUTES:
    {
      TCHAR *buf1=GetStringFromParm(-0x10);
      log_printf3(_T("SetFileAttributes: \"%s\":%08X"),buf1,parm1);
      if (!SetFileAttributes(buf1,parm1))
      {
        exec_error++;
        log_printf(_T("SetFileAttributes failed."));
      }
    }
    break;
    case EW_CREATEDIR: {
      TCHAR *buf1=GetStringFromParm(-0x10);
      log_printf3(_T("CreateDirectory: \"%s\" (%d)"),buf1,parm1);
      {
        TCHAR *p = skip_root(buf1), c = _T('c');
        if (p)
        {
          while (c)
          {
            DWORD ec;
            p = findchar(p, _T('\\'));
            c = *p, *p = 0;
            if (!c && parm2 && UserIsAdminGrpMember()) // Lock down the final directory?
            {
              ec = CreateRestrictedDirectory(buf1);
              if (ec) exec_error++; // Report error to add_plugins_dir_initializer
            }
            else
              ec = CreateNormalDirectory(buf1);
            if (ec)
            {
              if (ec != ERROR_ALREADY_EXISTS)
              {                
                log_printf3(_T("CreateDirectory: can't create \"%s\" (err=%d)"),buf1,ec);
                exec_error++;
              }
              else if ((GetFileAttributes(buf1) & FILE_ATTRIBUTE_DIRECTORY) == 0)
              {
                log_printf2(_T("CreateDirectory: can't create \"%s\" - a file already exists"),buf1);
                exec_error++;
              }
            }
            else
            {
              log_printf2(_T("CreateDirectory: \"%s\" created"),buf1);
            }
            *p++ = c;
          }
        }
      }
      if (parm1)
      {
        update_status_text_buf1(LANG_OUTPUTDIR);
        mystrcpy(state_output_directory,buf1);
        if (!SetCurrentDirectory(buf1))
        {
            log_printf3(_T("SetCurrentDirectory(%s) failed (%d)"),buf1,GetLastError());
            exec_error++;
        }
      }
      else update_status_text_buf1(LANG_CREATEDIR);
    }
    break;
    case EW_IFFILEEXISTS:
    {
      TCHAR *buf0=GetStringFromParm(0x00);
      if (file_exists(buf0))
      {
        log_printf3(_T("IfFileExists: file \"%s\" exists, jumping %d"),buf0,parm1);
        return parm1;
      }
      log_printf3(_T("IfFileExists: file \"%s\" does not exist, jumping %d"),buf0,parm2);
    }
    return parm2;
#ifdef NSIS_SUPPORT_RENAME
    case EW_RENAME:
      {
        TCHAR *buf3=GetStringFromParm(-0x30);
        TCHAR *buf2=GetStringFromParm(-0x21);
#ifdef NSIS_CONFIG_LOG
        TCHAR *buf1=
#endif
          GetStringFromParm(0x13); // For update_status_text_buf1 and log_printf
        log_printf2(_T("Rename: %s"),buf1);
        if (MoveFile(buf3,buf2))
        {
          update_status_text_buf1(LANG_RENAME);
        }
        else
        {
#ifdef NSIS_SUPPORT_MOVEONREBOOT
          if (parm2 && file_exists(buf3))
          {
            MoveFileOnReboot(buf3,buf2);
            update_status_text_buf1(LANG_RENAMEONREBOOT);
            log_printf2(_T("Rename on reboot: %s"),buf1);
          }
          else
#endif
          {
            exec_error++;
            log_printf2(_T("Rename failed: %s"),buf1);
          }
        }
      }
    break;
#endif//NSIS_SUPPORT_RENAME
#ifdef NSIS_SUPPORT_FNUTIL
    case EW_GETFULLPATHNAME:
      {
        TCHAR *fp;
        TCHAR *p=var1;
        TCHAR *buf0=GetStringFromParm(0x00);
        if (!GetFullPathName(buf0,NSIS_MAX_STRLEN,p,&fp))
        {
          exec_error++;
          *p=0;
        }
        else if (fp>buf0 && *fp)
        {
          WIN32_FIND_DATA *fd=file_exists(buf0);
          if (fd)
          {
            mystrcpy(fp,fd->cFileName);
          }
          else
          {
            exec_error++;
            *p=0;
          }
        }
        if (!parm2) GetShortPathName(p,p,NSIS_MAX_STRLEN);
      }
    break;
    case EW_SEARCHPATH:
      {
        TCHAR *fp;
        TCHAR *p=var0;
        TCHAR *buf0=GetStringFromParm(-0x01);
        if (!SearchPath(NULL,buf0,NULL,NSIS_MAX_STRLEN,p,&fp))
        {
          exec_error++;
          p[0]=0;
        }
      }
    break;
    case EW_GETTEMPFILENAME:
      {
        TCHAR *textout=var0;
        if (!my_GetTempFileName(textout, GetStringFromParm(-0x11)))
          exec_error++;
      }
    break;
#endif
#ifdef NSIS_SUPPORT_FILE
    case EW_EXTRACTFILE:
      {
        HANDLE hOut;
        int ret;
        TCHAR *buf3 = GetStringFromParm(0x31);
        int overwriteflag = parm0 & 7;

        log_printf4(_T("File: overwriteflag=%d, allowskipfilesflag=%d, name=\"%s\""),overwriteflag,(parm0>>3)&MB_ABORTRETRYIGNORE,buf3);
        if (validpathspec(buf3))
        {
          mystrcpy(buf0,buf3);
        }
        else mystrcat(addtrailingslash(mystrcpy(buf0,state_output_directory)),buf3);
        validate_filename(buf0);
      _tryagain:
        if (overwriteflag >= 3) // check date and time
        {
          WIN32_FIND_DATA *ffd=file_exists(buf0);
          // if it doesn't exist, overwrite flag will be off (though it doesn't really matter)
          int cmp=0;
          if (ffd)
          {
            cmp=CompareFileTime(&ffd->ftLastWriteTime, (FILETIME*)(lent.offsets + 3));
          }
          overwriteflag=!(cmp & (0x80000000 | (overwriteflag - 3)));
        }
        // remove read only flag if overwrite mode is on
        if (!overwriteflag)
        {
          remove_ro_attr(buf0);
        }
        hOut=myOpenFile(buf0,GENERIC_WRITE,(overwriteflag==1)?CREATE_NEW:CREATE_ALWAYS);
        if (hOut == INVALID_HANDLE_VALUE)
        {
          if (overwriteflag)
          {
            update_status_text(LANG_SKIPPED,buf3);
            if (overwriteflag==2) exec_error++;
            log_printf3(_T("File: skipped: \"%s\" (overwriteflag=%d)"),buf0,overwriteflag);
            break;
          }
          log_printf2(_T("File: error creating \"%s\""),buf0);

          mystrcpy(buf2,g_usrvars[0]); // save $0
          mystrcpy(g_usrvars[0],buf0); // copy file name to $0
          GetNSISString(buf1,parm5); // use $0
          mystrcpy(g_usrvars[0],buf2); // restore $0

          // Modified by ramon 23 May 2003
          switch (my_MessageBox(buf1, parm0>>3))
          {
            case IDRETRY:
              log_printf(_T("File: error, user retry"));
              goto _tryagain;
            case IDIGNORE:
              log_printf(_T("File: error, user cancel"));
              g_exec_flags.exec_error++;
              return 0;
            default:
              log_printf(_T("File: error, user abort"));
              update_status_text(LANG_CANTWRITE,buf0);
            return EXEC_ERROR;
          }
        }

        update_status_text(LANG_EXTRACT,buf3);
        {
          g_exec_flags.status_update++;
          ret=GetCompressedDataFromDataBlock(parm2,hOut);
          g_exec_flags.status_update--;
        }

        log_printf3(_T("File: wrote %d to \"%s\""),ret,buf0);

        if (parm3 != 0xffffffff || parm4 != 0xffffffff)
          SetFileTime(hOut,(FILETIME*)(lent.offsets+3),NULL,(FILETIME*)(lent.offsets+3));

        CloseHandle(hOut);

        if (ret < 0)
        {
          if (ret == -2)
          {
            GetNSISString(buf0,LANG_ERRORWRITING);
            mystrcat(buf0,buf3);
          }
          else
          {
            GetNSISString(buf0,LANG_ERRORDECOMPRESSING);
          }
          log_printf2(_T("%s"),buf0);
          my_MessageBox(buf0,MB_OK|MB_ICONSTOP|(IDOK<<21));
          return EXEC_ERROR;
        }
      }
    break;
#endif//NSIS_SUPPORT_FILE
#ifdef NSIS_SUPPORT_DELETE
    case EW_DELETEFILE:
      {
        TCHAR *buf0=GetStringFromParm(0x00);
        log_printf2(_T("Delete: \"%s\""),buf0);
        myDelete(buf0,parm1);
      }
    break;
#endif//NSIS_SUPPORT_DELETE
#ifdef NSIS_SUPPORT_MESSAGEBOX
    case EW_MESSAGEBOX: // MessageBox
      {
        int v;
        TCHAR *buf3=GetStringFromParm(0x31);
        log_printf3(_T("MessageBox: %d,\"%s\""),parm0,buf3);
        v=my_MessageBox(buf3,parm0);
        if (v)
        {
          if (v==parm2)
          {
            return parm3;
          }
          if (v==parm4)
          {
            return parm5;
          }
        }
        else exec_error++;
      }
    break;
#endif//NSIS_SUPPORT_MESSAGEBOX
#ifdef NSIS_SUPPORT_RMDIR
    case EW_RMDIR:
      {
        TCHAR *buf1=GetStringFromParm(-0x10);
        log_printf2(_T("RMDir: \"%s\""),buf1);

        myDelete(buf1,parm1);
      }
    break;
#endif//NSIS_SUPPORT_RMDIR
#ifdef NSIS_SUPPORT_STROPTS
    case EW_STRLEN:
    {
      TCHAR *buf0=GetStringFromParm(0x01);
      myitoa(var0,mystrlen(buf0));
    }
    break;
    case EW_ASSIGNVAR:
      {
        LARGE_INTEGER newlenex=GetIntFromParmEx(2);
        int start=GetIntFromParm(3), newlen=newlenex.LowPart;
        TCHAR *p=var0, *buf0=GetStringFromParm(0x01);
        int srclen=mystrlen(buf0);
        *p=0;
        if (!newlenex.HighPart) newlen=srclen; // "StrCpy $1 $2 $3" where $3=""
        if (newlen)
        {
          if (start<0) start=srclen+start;
          if (start>=0)
          {
            if (start>srclen) start=srclen;
            mystrcpy(p,buf0+start);
            if (newlen)
            {
              if (newlen<0) newlen=mystrlen(p)+newlen;
              if (newlen<0) newlen=0;
              if (newlen < NSIS_MAX_STRLEN) p[newlen]=0;
            }
          }
        }
      }
    break;
    case EW_STRCMP:
    {
      TCHAR *buf2=GetStringFromParm(0x20);
      TCHAR *buf3=GetStringFromParm(0x31);
      if (!parm4) {
        // case insensitive
        if (!lstrcmpi(buf2,buf3)) return parm2;
      }
      else {
        // case sensitive
        if (!lstrcmp(buf2,buf3)) return parm2;
      }
    }
    return parm3;
#endif//NSIS_SUPPORT_STROPTS
#ifdef NSIS_SUPPORT_ENVIRONMENT
    case EW_READENVSTR:
      {
        TCHAR *p=var0;
        TCHAR *buf0=GetStringFromParm(0x01);
        if (!ExpandEnvironmentStrings(buf0,p,NSIS_MAX_STRLEN)
            || (parm2 && !lstrcmp(buf0, p)))
        {
          exec_error++;
          *p=0;
        }
        p[NSIS_MAX_STRLEN-1]=0;
      }
    break;
#endif//NSIS_SUPPORT_ENVIRONMENT
#ifdef NSIS_SUPPORT_INTOPTS
    case EW_INTCMP:
      {
        UINT supp64=sizeof(void*) > 4, opu=supp64 ? (BYTE) parm5 : parm5, op64=supp64 ? (SHORT) parm5 < 0 : FALSE;
        INT_PTR v=GetIntPtrFromParm(0), v2=GetIntPtrFromParm(1); // Note: This needs to be INT64 if supp64 is ever set to true for 32-bit builds!
        if (!opu) { // signed:
          if (op64) {
            if ((INT64)v < (INT64)v2) return parm3;
            if ((INT64)v > (INT64)v2) return parm4;
          }
          else {
            if ((signed int)v < (signed int)v2) return parm3;
            if ((signed int)v > (signed int)v2) return parm4;
          }
        }
        else { // unsigned:
          if (op64) {
            if ((UINT64)v < (UINT64)v2) return parm3;
            if ((UINT64)v > (UINT64)v2) return parm4;
          }
          else {
            if ((unsigned int)v < (unsigned int)v2) return parm3;
            if ((unsigned int)v > (unsigned int)v2) return parm4;
          }
        }
      }
    return parm2;
    case EW_INTOP:
      {
        int v,v2;
        TCHAR *p=var0;
        v=GetIntFromParm(1);
        v2=GetIntFromParm(2);
        switch (parm3)
        {
          case 0: v+=v2; break;
          case 1: v-=v2; break;
          case 2: v*=v2; break;
          case 3: if (v2) v/=v2; else { v=0; exec_error++; } break;
          case 4: v|=v2; break;
          case 5: v&=v2; break;
          case 6: v^=v2; break;
          case 7: v=!v; break;
          case 8: v=v||v2; break;
          case 9: v=v&&v2; break;
          case 10: if (v2) v%=v2; else { v=0; exec_error++; } break;
          case 11: v=v<<v2; break;
          case 12: v=v>>v2; break;
          case 13: v=(unsigned int)v>>(unsigned int)v2; break;
        }
        myitoa(p,v);
      }
    break;
    case EW_INTFMT: {
      TCHAR *buf0=GetStringFromParm(0x01);
      INT_PTR val=GetIntPtrFromParm(2), op64=sizeof(void*) > 4 && parm3;
      wsprintf(var0,buf0,op64 ? val : (UINT)val);
    }
    break;
#endif//NSIS_SUPPORT_INTOPTS
#ifdef NSIS_SUPPORT_STACK
    case EW_PUSHPOP:
      {
        stack_t *s=g_st;
        int cnt=parm2;
        if (cnt) //Exch contributed by Fritz Elfert
        {
          while (cnt--&&s) s=s->next;
          if (!s)
          {
            log_printf2(_T("Exch: stack < %d elements"),parm2);
            my_MessageBox(GetNSISStringTT(LANG_INSTCORRUPTED),MB_OK|MB_ICONSTOP|(IDOK<<21));
            return EXEC_ERROR;
          }
          mystrcpy(buf0,s->text);
          mystrcpy(s->text,g_st->text);
          mystrcpy(g_st->text,buf0);
        }
        else if (parm1)
        {
          if (!s)
          {
            log_printf(_T("Pop: stack empty"));
            exec_error++;
            break;
          }
          mystrcpy(var0,s->text);
          g_st=s->next;
          GlobalFree((HGLOBAL)s);
        }
        else
        {
          s=(stack_t*)GlobalAlloc(GPTR,sizeof(stack_t));
          GetNSISString(s->text,parm0);
          s->next=g_st;
          g_st=s;
        }
      }
    break;
#endif//NSIS_SUPPORT_STACK
#ifdef NSIS_SUPPORT_HWNDS
    case EW_FINDWINDOW:
    case EW_SENDMESSAGE:
      {
        LRESULT v;
        INT_PTR b3=GetIntPtrFromParm(3);
        INT_PTR b4=GetIntPtrFromParm(4);
        if (parm5&1) b3=(INT_PTR)GetStringFromParm(0x33);
        if (parm5&2) b4=(INT_PTR)GetStringFromParm(0x44);

        if (which == EW_SENDMESSAGE)
        {
          HWND hwnd=GetHwndFromParm(1);
          int msg=GetIntFromParm(2);

          if (parm5>>2) exec_error += !SendMessageTimeout(hwnd,msg,b3,b4,SMTO_NORMAL,parm5>>2,(PDWORD_PTR)&v);
          // Jim Park: This sends script messages.  Some messages require
          // settings for Unicode.  This means the user's script may need
          // to change for Unicode NSIS.
          else v=SendMessage(hwnd,msg,b3,b4);
        }
        else
        {
          TCHAR *buf0=GetStringFromParm(0x01);
          TCHAR *buf1=GetStringFromParm(0x12);
          v=(LRESULT)FindWindowEx((HWND)b3,(HWND)b4,buf0[0]?buf0:NULL,buf1[0]?buf1:NULL);
        }

        if (parm0>=0) iptrtostr(var0,v);
      }
    break;
    case EW_ISWINDOW:
      if (IsWindow(GetHwndFromParm(0))) return parm1;
    return parm2;
#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
    case EW_GETDLGITEM:
      iptrtostr(
        var0,
        (INT_PTR)GetDlgItem(
          GetHwndFromParm(1),
          GetIntFromParm(2)
        )
      );
    break;
    case EW_SETCTLCOLORS:
    {
      ctlcolors *c = (ctlcolors *)(g_blocks[NB_CTLCOLORS].offset + parm1);
      SetWindowLongPtr(GetHwndFromParm(0), GWLP_USERDATA, (LONG_PTR) c);
    }
    break;
    case EW_LOADANDSETIMAGE:
    {
      RECT r;
      HANDLE hNewImage, hPrevImage;
      HWND hCtl=(parm3 & LASIF_HWND) ? GetHwndFromParm(2) : GetDlgItem(g_hwnd, parm2);
      UINT it=parm3 & LASIM_IMAGE, exeres=parm3 & LASIF_EXERES, fitw=(UINT)parm3 >> LASIS_FITCTLW, fith=(parm3 & LASIF_FITCTLH) != 0;
      LPCTSTR imgid = (parm3 & LASIF_STRID) ? GetStringFromParm(0x11) : MAKEINTRESOURCE(parm1);
      GetClientRect(hCtl, &r);
      hNewImage=LoadImage(exeres ? g_hInstance : NULL, imgid, it, fitw*r.right, fith*r.bottom, parm3 & LASIM_LR);
      if (!hNewImage && IMAGE_BITMAP == it && !exeres && (parm3 & LASIF_LR_LOADFROMFILE))
        hNewImage=LoadOLEPictureBitmap(imgid, fitw*r.right, fith*r.bottom);
      hPrevImage=(HANDLE)SendMessage(hCtl, STM_SETIMAGE, it, (LPARAM)hNewImage);
      if (hPrevImage && IMAGE_BITMAP == it) DeleteObject(hPrevImage); // Delete the old bitmap
      if (parm0 >= 0) iptrtostr(var0, (INT_PTR)hNewImage); // Optional output handle
    }
    break;
    case EW_CREATEFONT:
    {
      static LOGFONT f;
      const HDC hdc=GetDC(g_hwnd);
      f.lfHeight=-MulDiv(GetIntFromParm(2),GetDeviceCaps(hdc,LOGPIXELSY),72);
      ReleaseDC(g_hwnd,hdc);
      f.lfWeight=GetIntFromParm(3);
      f.lfItalic=parm4&1;
      f.lfUnderline=parm4&2;
      f.lfStrikeOut=parm4&4;
      f.lfCharSet=DEFAULT_CHARSET;
      GetNSISString(f.lfFaceName,parm1);
      iptrtostr(var0,(INT_PTR)CreateFontIndirect(&f));
    }
    break;
    case EW_SHOWWINDOW:
    {
      HWND hw=GetHwndFromParm(0);
      int a=GetIntFromParm(1);
      if (parm2) log_printf(_T("HideWindow"));
      if (!parm3)
        ShowWindow(hw,a);
      else
        EnableWindow(hw,a);
    }
    break;
#endif//NSIS_CONFIG_ENHANCEDUI_SUPPORT
#endif//NSIS_SUPPORT_HWNDS
#ifdef NSIS_SUPPORT_SHELLEXECUTE
    case EW_SHELLEXEC: // this uses improvements of Andras Varga
      {
        SHELLEXECUTEINFO sei;
        TCHAR *buf0=GetStringFromParm(0x00); // Verb
        TCHAR *buf3=GetStringFromParm(0x31); // File
        TCHAR *buf2=GetStringFromParm(0x22); // Parameters
        GetStringFromParm(0x15); // For update_status_text_buf1
        update_status_text_buf1(LANG_EXECSHELL);
        sei.fMask=parm4;
        sei.hwnd=g_hwnd, sei.nShow=parm3;
        sei.lpVerb=buf0[0]?buf0:NULL, sei.lpFile=buf3, sei.lpParameters=buf2[0]?buf2:NULL, sei.lpDirectory=state_output_directory;
        if (!myShellExecuteEx(&sei))
        {
          log_printf5(_T("ExecShell: warning: error (\"%s\": file:\"%s\" params:\"%s\")=%d"),buf0,buf3,buf2,GetLastError());
          exec_error++;
        }
        else
        {
          if (SEE_MASK_NOCLOSEPROCESS & sei.fMask)
          {
            WaitForProcess(sei.hProcess);
            CloseHandle(sei.hProcess);
          }
          log_printf4(_T("ExecShell: success (\"%s\": file:\"%s\" params:\"%s\")"),buf0,buf3,buf2);
        }
      }
    break;
#endif//NSIS_SUPPORT_SHELLEXECUTE
#ifdef NSIS_SUPPORT_EXECUTE
    case EW_EXECUTE:
      {
        HANDLE hProc;
        TCHAR *buf0=GetStringFromParm(0x00);
        log_printf2(_T("Exec: command=\"%s\""),buf0);
        update_status_text(LANG_EXECUTE,buf0);

        hProc=myCreateProcess(buf0);

        if (hProc)
        {
          log_printf2(_T("Exec: success (\"%s\")"),buf0);
          if (parm2)
          {
            DWORD lExitCode=WaitForProcess(hProc);
            if (parm1>=0) myitoa(var1,lExitCode);
            else if (lExitCode) exec_error++;
          }
          CloseHandle(hProc);
        }
        else
        {
          exec_error++;
          log_printf2(_T("Exec: failed createprocess (\"%s\")"),buf0);
        }
      }
    break;
#endif//NSIS_SUPPORT_EXECUTE
#ifdef NSIS_SUPPORT_GETFILETIME
    case EW_GETFILETIME:
      // this new implementation based on one by Dave Bau
      // used FindFirstFile instead of GetFileTime to better handle files that are locked.
      // also allows GetFileTime to be passed a wildcard.
      {
        WIN32_FIND_DATA *ffd;
        TCHAR *highout=var0;
        TCHAR *lowout=var1;
        TCHAR *buf0=GetStringFromParm(0x02);

        ffd=file_exists(buf0);
        if (ffd)
        {
          myitoa(lowout,ffd->ftLastWriteTime.dwLowDateTime);
          myitoa(highout,ffd->ftLastWriteTime.dwHighDateTime);
        }
        else
        {
          *lowout=*highout=0;
          exec_error++;
        }
      }
    break;
#endif//NSIS_SUPPORT_GETFILETIME
#ifdef NSIS_SUPPORT_GETDLLVERSION
    case EW_GETDLLVERSION:
      {
        TCHAR *highout=var0, *lowout=var1;
        DWORD s1, d;
        VS_FIXEDFILEINFO *pvsf1;
        TCHAR *buf1=GetStringFromParm(-0x12);
        s1=((DWORD(WINAPI*)(LPCTSTR,DWORD*))myGetProcAddress(MGA_GetFileVersionInfoSize))(buf1,&d);
        *lowout=*highout=0;
        exec_error++;
        if (s1)
        {
          void *b1;
          b1=GlobalAlloc(GPTR,s1);
          if (b1)
          {
            FARPROC gfvi = myGetProcAddress(MGA_GetFileVersionInfo), vqv = myGetProcAddress(MGA_VerQueryValue);
            UINT uLen;
            if ( ((BOOL(WINAPI*)(LPCTSTR,DWORD,DWORD,LPVOID))gfvi)(buf1,0,s1,b1)
              && ((BOOL(WINAPI*)(LPCVOID,LPCTSTR,LPVOID*,UINT*))vqv)(b1,_T("\\"),(void*)&pvsf1,&uLen) )
            {
              myitoa(highout,(&pvsf1->dwFileVersionMS)[parm3]);
              myitoa(lowout,(&pvsf1->dwFileVersionLS)[parm3]);

              exec_error--;
            }
            GlobalFree(b1);
          }
        }
      }
      break;
#endif//NSIS_SUPPORT_GETDLLVERSION
#ifdef NSIS_SUPPORT_ACTIVEXREG
    case EW_REGISTERDLL:
      {
        exec_error++;
        if (SUCCEEDED(g_hres))
        {
          HANDLE h=NULL;
          TCHAR *buf1=GetStringFromParm(-0x10);
          TCHAR *buf0=GetStringFromParm(0x01);

          if (parm4)
            h=GetModuleHandle(buf1);
          if (!h)
            h=LoadLibraryEx(buf1, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
          if (h)
          {
            // Jim Park: Need to use our special NSISGetProcAddress to convert
            // buf0 to char before calling GetProcAddress() which only takes 
            // chars.
            FARPROC funke = NSISGetProcAddress(h,buf0);
            if (funke)
            {
              exec_error--;
              if (parm2)
              {
                update_status_text_buf1(parm2);
                if (funke()) exec_error++;
              }
              else
              {
                void (*func)(HWND,int,TCHAR*,void*,void*);
                func=(void*)funke;
                func(
                  g_hwnd,
                  NSIS_MAX_STRLEN,
                  (TCHAR*)g_usrvars,
#ifdef NSIS_SUPPORT_STACK
                  (void*)&g_st,
#else
                  NULL,
#endif//NSIS_SUPPORT_STACK
                  &plugin_extra_parameters
                );
              }
            }
            else
            {
              update_status_text(LANG_CANNOTFINDSYMBOL,buf0);
              log_printf3(_T("Error registering DLL: %s not found in %s"),buf0,buf1);
            }
            if (!parm3 && Plugins_CanUnload(h)) FreeLibrary(h);
          }
          else
          {
            update_status_text_buf1(LANG_COULDNOTLOAD);
            log_printf2(_T("Error registering DLL: Could not load %s"),buf1);
          }
        }
        else
        {
          update_status_text_buf1(LANG_NOOLE);
          log_printf(_T("Error registering DLL: Could not initialize OLE"));
        }
      }
    break;
#endif
#ifdef NSIS_SUPPORT_CREATESHORTCUT
    case EW_CREATESHORTCUT:
    {
      TCHAR *buf1=GetStringFromParm(-0x10);
      TCHAR *buf2=GetStringFromParm(-0x21);
      TCHAR *buf0=GetStringFromParm(0x02);
      TCHAR *buf3=GetStringFromParm(-0x33);
      TCHAR *buf4=GetStringFromParm(0x45);
      const int icoi = (parm4>>CS_II_SHIFT)&(CS_II_MASK>>CS_II_SHIFT), nwd = parm4&CS_NWD,
        sc = (parm4>>CS_SC_SHIFT)&(CS_SC_MASK>>CS_SC_SHIFT), hk = (parm4>>CS_HK_SHIFT)&(CS_HK_MASK>>CS_HK_SHIFT);

      HRESULT hres;
      IShellLink* psl;

      if (!validpathspec(buf2))
        GetStringFromParm(0x21);

      log_printf8(_T("CreateShortcut: out: \"%s\", in: \"%s %s\", icon: %s,%d, sw=%d, hk=%d"),
        buf1,buf2,buf0,buf3,icoi,sc,hk);

      hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IShellLink, (void **) &psl);
      if (SUCCEEDED(hres))
      {
        IPersistFile* ppf;

        hres = psl->lpVtbl->QueryInterface(psl,&IID_IPersistFile, (void **) &ppf);
        if (SUCCEEDED(hres))
        {
          hres = psl->lpVtbl->SetPath(psl,buf2);
          if (!nwd) psl->lpVtbl->SetWorkingDirectory(psl,state_output_directory);
          if (sc) psl->lpVtbl->SetShowCmd(psl,sc);
          psl->lpVtbl->SetHotkey(psl,(unsigned short) hk);
          if (buf3[0]) psl->lpVtbl->SetIconLocation(psl,buf3,icoi);
          psl->lpVtbl->SetArguments(psl,buf0);
          psl->lpVtbl->SetDescription(psl,buf4);

          if (SUCCEEDED(hres))
          {
#ifdef _UNICODE
            hres = ppf->lpVtbl->Save(ppf,buf1,TRUE);
#else
            WCHAR *wsz = (LPWSTR) buf2; // buf2 + buf3 = WCHAR wsz[NSIS_MAX_STRLEN]
            hres = E_FAIL;
            if (MultiByteToWideChar(CP_ACP,0,buf1,-1,wsz,NSIS_MAX_STRLEN))
              hres = ppf->lpVtbl->Save(ppf,wsz,TRUE);
#endif
          }
          ppf->lpVtbl->Release(ppf);
        }
        psl->lpVtbl->Release(psl);
      }

      if (FAILED(hres))
      {
        exec_error++;
        update_status_text_buf1(LANG_ERRORCREATINGSHORTCUT);
      }
      else
      {
        update_status_text_buf1(LANG_CREATESHORTCUT);
      }
    }
    break;
#endif//NSIS_SUPPORT_CREATESHORTCUT
#ifdef NSIS_SUPPORT_COPYFILES
    case EW_COPYFILES: // CopyFile (added by NOP)
      {
        int res;
        SHFILEOPSTRUCT op;
        TCHAR *buf0=GetStringFromParm(0x00);
        TCHAR *buf1=GetStringFromParm(0x11);
        TCHAR *buf2=GetStringFromParm(0x23); // LANG_COPYTO + buf1
        log_printf3(_T("CopyFiles \"%s\"->\"%s\""),buf0,buf1);

        if (!file_exists(buf0))
        {
          // workaround for bug #774966
          //
          // on nt4, SHFileOperation silently fails if the source
          // file doesn't exist. do a manual check instead.

          update_status_text(LANG_COPYFAILED,0);
          exec_error++;
          break;
        }

        op.hwnd=g_hwnd;
        op.wFunc=FO_COPY;
        buf0[mystrlen(buf0)+1]=0;
        buf1[mystrlen(buf1)+1]=0;

        op.pFrom=buf0;
        op.pTo=buf1;
        op.lpszProgressTitle=buf2;
        op.fFlags=parm2;
        update_status_text(0,buf2);
        res=SHFileOperation(&op);
        if (res)
        { // some of these changes were from Edgewise (wiked_edge@yahoo.com)
          update_status_text(LANG_COPYFAILED,0);
          exec_error++;
        }
      }
    break;
#endif//NSIS_SUPPORT_COPYFILES
#ifdef NSIS_SUPPORT_REBOOT
    case EW_REBOOT:
      if (parm0!=0xbadf00d)
      {
        my_MessageBox(GetNSISStringTT(LANG_INSTCORRUPTED),MB_OK|MB_ICONSTOP|(IDOK<<21));
        return EXEC_ERROR;
      }

      g_exec_flags.reboot_called++;
      // a following EW_QUIT will make sure the installer quits right away

    break;
#endif//NSIS_SUPPORT_REBOOT
#ifdef NSIS_SUPPORT_INIFILES
    case EW_WRITEINI:
      {
        TCHAR *sec=0, *key=0, *str=0;
#ifdef NSIS_CONFIG_LOG
        mystrcpy(buf1,_T("<RM>"));
        mystrcpy(buf2,buf1);
#endif
        if (parm0) sec=GetStringFromParm(0x00);
        if (parm1) key=GetStringFromParm(0x11);
        if (parm4) str=GetStringFromParm(0x22);
        buf3=GetStringFromParm(-0x33);
        log_printf5(_T("WriteINIStr: wrote [%s] %s=%s in %s"),buf0,buf1,buf2,buf3);
        if (!WritePrivateProfileString(sec,key,str,buf3)) exec_error++;
      }
    break;
    case EW_READINISTR:
      {
        // GetPrivateProfileString can't read CR & LF characters inside values from INI files
        // so we use "\n" as a detection system to see if we did successfully read a value
        const TCHAR errstr[] = _T("\n");
        TCHAR *p=var0;
        TCHAR *buf0=GetStringFromParm(0x01);
        TCHAR *buf1=GetStringFromParm(0x12);
        TCHAR *buf2=GetStringFromParm(-0x23);
        GetPrivateProfileString(buf0,buf1,errstr,p,NSIS_MAX_STRLEN-1,buf2);
        if (p[0] == _T('\n')) // we got the default string "\n" instead of a real value
        {
          exec_error++;
          p[0]=0;
        }
      }
    break;
#endif//NSIS_SUPPORT_INIFILES
#ifdef NSIS_SUPPORT_REGISTRYFUNCTIONS
    case EW_DELREG:
      {
        long res=!ERROR_SUCCESS, rootkey=parm1;
        const TCHAR *rkn UNUSED=RegKeyHandleToName((HKEY)rootkey);
        if (!parm4) // TOK_DELETEREGVALUE
        {
          HKEY hKey=RegOpenScriptKey(KEY_SET_VALUE);
          if (hKey)
          {
            TCHAR *buf3=GetStringFromParm(0x33);
            res = RegDeleteValue(hKey,buf3);
            log_printf4(_T("DeleteRegValue: \"%s\\%s\" \"%s\""),rkn,buf2,buf3);
            RegCloseKey(hKey);
          }
        }
        else // TOK_DELETEREGKEY
        {
          TCHAR *buf2=GetStringFromParm(0x22);
          log_printf3(_T("DeleteRegKey: \"%s\\%s\""),rkn,buf2);
          res = RegDeleteScriptKey(rootkey,buf2,parm4 >> DELREGKEYFLAGSSHIFT); // SHR is 1 byte smaller than AND
        }
        if (res != ERROR_SUCCESS)
          exec_error++;
      }
    break;
    case EW_WRITEREG: // write registry value
      {
        HKEY hKey;
        int rootkey=parm0, type=parm4, rtype=parm5;
        TCHAR *buf0=GetStringFromParm(0x02);
        TCHAR *buf1=GetStringFromParm(0x11);
        const TCHAR *rkn UNUSED=RegKeyHandleToName((HKEY)rootkey);

        exec_error++;
        if ((hKey = RegCreateScriptKey(rootkey, buf1, KEY_SET_VALUE)))
        {
          LPBYTE data = (LPBYTE) buf2;
          DWORD size = 0;
          if (type == REG_SZ)
          {
            GetStringFromParm(0x23);
            size = (mystrlen((TCHAR *) data) + 1)*sizeof(TCHAR);
            if (rtype == REG_SZ)
            {
              log_printf5(_T("WriteRegStr: \"%s\\%s\" \"%s\"=\"%s\""),rkn,buf1,buf0,data);
            }
            else
            {
              log_printf5(_T("WriteRegExpandStr: \"%s\\%s\" \"%s\"=\"%s\""),rkn,buf1,buf0,data);
            }
          }
          if (type == REG_DWORD)
          {
            *(LPDWORD) data = GetIntFromParm(3);
            size = sizeof(DWORD);
            log_printf5(_T("WriteRegDWORD: \"%s\\%s\" \"%s\"=\"0x%08x\""),rkn,buf1,buf0,*(LPDWORD) data);
          }
          if (type == REG_BINARY)
          {
#ifdef NSIS_CONFIG_LOG
            TCHAR binbuf[128];
            LPCTSTR logf = rtype==REG_NONE?_T("WriteRegNone"):rtype==REG_MULTI_SZ?_T("WriteRegMultiStr"):_T("WriteRegBin");
#endif
            // use buf2, buf3 and buf4
            size = GetCompressedDataFromDataBlockToMemory(parm3, data, (3 * NSIS_MAX_STRLEN)*sizeof(TCHAR));
            LogData2Hex(binbuf, COUNTOF(binbuf), data, size);
            log_printf6(_T("%s: \"%s\\%s\" \"%s\"=\"%s\""),logf,rkn,buf1,buf0,binbuf);
          }
          
          if (size >= 0 && RegSetValueEx(hKey,buf0,0,rtype,data,size) == ERROR_SUCCESS)
          {
            exec_error--;
          }
          else
          {
            log_printf4(_T("WriteReg: error writing into \"%s\\%s\" \"%s\""),rkn,buf1,buf0);
          }

          RegCloseKey(hKey);
        }
        else { log_printf3(_T("WriteReg: error creating key \"%s\\%s\""),rkn,buf1); }
      }
    break;
    case EW_READREGSTR: // read registry string
      {
        HKEY hKey=RegOpenScriptKey(KEY_READ);
        TCHAR *p=var0;
        TCHAR *buf3=GetStringFromParm(0x33); // buf3 == key name
        p[0]=0;
        if (hKey)
        {
          DWORD l = NSIS_MAX_STRLEN*sizeof(TCHAR), t;

          // Jim Park: If plain text in p or binary data in p,
          // user must be careful in accessing p correctly.
          if (RegQueryValueEx(hKey,buf3,NULL,&t,(LPBYTE)p,&l) != ERROR_SUCCESS ||
              (t != REG_DWORD && t != REG_SZ && t != REG_EXPAND_SZ))
          {
            p[0]=0;
            exec_error++;
          }
          else
          {
            if (t==REG_DWORD)
            {
              exec_error += !parm4;
              myitoa(p,*((DWORD*)p));
            }
            else
            {
              exec_error += parm4;
              p[NSIS_MAX_STRLEN-1]=0; // RegQueryValueEx adds a null terminator, UNLESS the value is NSIS_MAX_STRLEN long
            }
          }
          RegCloseKey(hKey);
        }
        else exec_error++;
     }
    break;
    case EW_REGENUM:
      {
        HKEY hKey=RegOpenScriptKey(KEY_READ);
        TCHAR *p=var0;
        int b=GetIntFromParm(3);
        p[0]=0; // "" on error. This assumes that RegEnumKey and RegEnumValue do not party on our buffer!
        if (hKey)
        {
          DWORD d=NSIS_MAX_STRLEN-1; // -1 is not required here?
          if (parm4)
            RegEnumKey(hKey,b,p,d);
          else if (RegEnumValue(hKey,b,p,&d,NULL,NULL,NULL,NULL) != ERROR_SUCCESS)
            exec_error++;
          p[NSIS_MAX_STRLEN-1]=0; // Not required?
          RegCloseKey(hKey);
        }
        else exec_error++;
      }

    break;
#endif//NSIS_SUPPORT_REGISTRYFUNCTIONS
#ifdef NSIS_SUPPORT_FILEFUNCTIONS
    case EW_FCLOSE:
      {
        HANDLE handle = (HANDLE) strtoiptr(var0);
        if (handle) CloseHandle(handle);
      }
    break;
    case EW_FOPEN:
      {
        HANDLE h;
        TCHAR *handleout=var0;
        TCHAR *buf1=GetStringFromParm(-0x13);
        h=myOpenFile(buf1,parm1,parm2);
        if (h == INVALID_HANDLE_VALUE)
        {
          *handleout=0;
          exec_error++;
        }
        else
        {
          iptrtostr(handleout,(INT_PTR)h);
        }
      }
    break;
    case EW_FPUTS:
#ifdef _UNICODE
    case EW_FPUTWS:
      // Jim Park/Wizou: in Unicode version of NSIS, EW_FPUTS still deals with ANSI files (conversion is done). We add EW_FPUTWS to deal with Unicode files.
#endif
      {
        int l; // number of bytes to write
        TCHAR *t=var0;
        const int writeCodPt = parm2, ansi = EW_FPUTS == which;
        if (writeCodPt) // FileWriteByte or FileWriteWord
        {
          // Note: In Unicode version, we put a WORD in buf1[0] and will write 1 or 2 bytes, depending on FileWriteByte/Word.
          ((_TUCHAR *)buf1)[0]=(_TUCHAR) GetIntFromParm(1);
          l=(ansi)?1:sizeof(TCHAR);
        }
#ifdef _UNICODE
        else if (which==EW_FPUTS)
        {
          GetStringFromParm(0x21); // load string in buf2, convert it to ANSI in buf1
          StrWideToACP(buf2, (LPSTR) buf1, NSIS_MAX_STRLEN);
          l=lstrlenA((LPCSTR)buf1);
        }
#endif
        else
        {
          l=mystrlen(GetStringFromParm(0x11))*sizeof(TCHAR);
        }
        if (*t)
        {
          const HANDLE hFile = (HANDLE) strtoiptr(t);
#ifdef _UNICODE
          if ((ansi | writeCodPt) || !parm3 || SUCCEEDED(UTF16LEBOM(hFile,(INT_PTR)hFile)))
#endif
            if (myWriteFile(hFile,buf1,l))
              break; // Success
        }
        exec_error++;
      }
    break;
    case EW_FGETS:
#ifdef _UNICODE
    case EW_FGETWS:
      // Jim Park/Wizou: in Unicode version of NSIS, EW_FGETS still deals with ANSI files (conversion is done). We add EW_FGETWS to deal with Unicode files.
#endif
      {
        TCHAR *textout=var1;
        int rpos=0, ungetseek=sizeof(TCHAR);
        TCHAR *hptr=var0;
        int maxlen=GetIntFromParm(2);
        if (maxlen<1) break;
        if (maxlen > NSIS_MAX_STRLEN-1) maxlen=NSIS_MAX_STRLEN-1;
        if (*hptr)
        {
          TCHAR lc=0;
          HANDLE h=(HANDLE)strtoiptr(hptr);
          while (rpos<maxlen)
          {
            TCHAR c;
#ifdef _UNICODE
            if (which==EW_FGETS)
            {
              char tmpc[2];
              DWORD mbtwcflags=MB_ERR_INVALID_CHARS, cbio;
              if (!ReadFile(h,tmpc,2-parm3,&cbio,NULL) || !cbio) break;
              ungetseek=cbio;
              c = (unsigned char) tmpc[0]; // FileReadByte
              if (!parm3) for(;;) // Try to parse as DBCS first, if that fails try again as a single byte
              {
                // BUGBUG: Limited to UCS-2/BMP, surrogate pairs are not supported.
                if (MultiByteToWideChar(CP_ACP,mbtwcflags,tmpc,cbio,&c,1)) break;
                c=0xfffd; // Unicode replacement character
                // If we read 2 bytes and it was not a DBCS character, we need to seek -1
                if (--cbio) SetFilePointer(h,-(--ungetseek),NULL,FILE_CURRENT); else break;
              }
            }
            else
#endif
            {
#ifdef _UNICODE
              if (!parm3 && 0 == rpos && FAILED(UTF16LEBOM(h,FALSE))) break;
#endif
              // Read 1 TCHAR (FileReadUTF16LE, (Ansi)FileRead, FileReadWord)
              if (!myReadFile(h,&c,sizeof(TCHAR))) break;
            }
            if (parm3)
            {
              myitoa(textout,(UINT)(_TUCHAR)c);
              return 0;
            }
            if (lc == _T('\r') || lc == _T('\n'))
            {
              if (lc == c || (c != _T('\r') && c != _T('\n')))
                SetFilePointer(h,-((int)ungetseek),NULL,FILE_CURRENT);
              else
                textout[rpos++]=c;
              break;
            }
            textout[rpos++]=c;
            lc=c;
            if (!c) break;
          }
        }
        textout[rpos]=0;
        if (!rpos) exec_error++;
      }
    break;
    case EW_FSEEK:
      {
        TCHAR *t=var0;
        if (*t)
        {
          // TODO: Use SetFilePointerEx for > 4GB support on _WIN64
          DWORD v=SetFilePointer((HANDLE)strtoiptr(t),GetIntFromParm(2),NULL,parm3);

          if (parm1>=0)
          {
            myitoa(var1,v);
          }
        }
      }
    break;
#endif//NSIS_SUPPORT_FILEFUNCTIONS
#ifdef NSIS_SUPPORT_FINDFIRST
    case EW_FINDCLOSE:
      {
        HANDLE hFind = (HANDLE) strtoiptr(var0);
        if (hFind) FindClose(hFind);
      }
    break;
    case EW_FINDNEXT:
      {
        TCHAR *textout=var0;
        HANDLE hFind = (HANDLE) strtoiptr(var1);
        WIN32_FIND_DATA fd;
        if (hFind && FindNextFile(hFind,&fd))
        {
          mystrcpy(textout,fd.cFileName);
        }
        else
        {
          exec_error++;
          *textout=0;
        }

      }
    break;
    case EW_FINDFIRST:
      {
        TCHAR *textout=var0;
        TCHAR *handleout=var1;
        HANDLE h;
        WIN32_FIND_DATA fd;
        TCHAR *buf0=GetStringFromParm(0x02);
        h=FindFirstFile(buf0,&fd);
        if (h == INVALID_HANDLE_VALUE)
        {
          *handleout=0;
          *textout=0;
          exec_error++;
        }
        else
        {
          iptrtostr(handleout,(INT_PTR)h);
          mystrcpy(textout,fd.cFileName);
        }
      }
    break;
#endif//NSIS_SUPPORT_FINDFIRST
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
    case EW_WRITEUNINSTALLER:
      {
        int ret=-666;
        HANDLE hFile;
        TCHAR *buf1=GetStringFromParm(-0x10);

        if (!validpathspec(buf1))
          GetStringFromParm(-0x13);

        remove_ro_attr(buf1);
        hFile=myOpenFile(buf1,GENERIC_WRITE,CREATE_ALWAYS);
        if (hFile != INVALID_HANDLE_VALUE)
        {
          int dboffset = parm1;
          if (parm2)
          {
            unsigned char *filebuf;
            int filehdrsize = g_filehdrsize;
            filebuf=(unsigned char *)GlobalAlloc(GPTR,filehdrsize);
            if (filebuf)
            {
              SetSelfFilePointer(0);
              ReadSelfFile((char*)filebuf,filehdrsize);
              {
                // parm1 = uninstdata_offset
                // parm2 = m_unicon_size
                unsigned char* seeker;
                unsigned char* unicon_data = seeker = (unsigned char*)GlobalAlloc(GPTR,parm2);
                if (unicon_data) {
                  GetCompressedDataFromDataBlockToMemory(parm1,unicon_data,parm2);
                  while (*seeker) {
                    struct icondata {
                      DWORD dwSize;
                      DWORD dwOffset;
                    } id = *(struct icondata *) seeker;
                    seeker += sizeof(struct icondata);
                    mini_memcpy(filebuf+id.dwOffset, seeker, id.dwSize);
                    seeker += id.dwSize;
                  }
                  GlobalFree(unicon_data);
                }
              }
              myWriteFile(hFile,(char*)filebuf,filehdrsize);
              GlobalFree(filebuf);
              dboffset = -1;
            }
          }
          ret=GetCompressedDataFromDataBlock(dboffset, hFile);
          CloseHandle(hFile);
        }
        log_printf3(_T("created uninstaller: %d, \"%s\""),ret,buf1);
        {
          int str = LANG_CREATEDUNINST;
          if (ret < 0)
          {
            str = LANG_ERRORCREATING;
            DeleteFile(buf1);
            exec_error++;
          }
          update_status_text_buf1(str);
        }
      }
    break;
#endif//NSIS_CONFIG_UNINSTALL_SUPPORT
#ifdef NSIS_CONFIG_LOG
    case EW_LOG:
      if (parm0)
      {
        log_printf2(_T("settings logging to %d"),parm1);
        log_dolog=parm1;
        log_printf2(_T("logging set to %d"),parm1);
#if !defined(NSIS_CONFIG_LOG_ODS) && !defined(NSIS_CONFIG_LOG_STDOUT)
        if (parm1)
          build_g_logfile();
        else
          log_write(1);
#endif
      }
      else
      {
        TCHAR *buf0=GetStringFromParm(0x01);
        log_printf2(_T("%s"),buf0);
      }
    break;
#endif//NSIS_CONFIG_LOG
#ifdef NSIS_CONFIG_COMPONENTPAGE
    case EW_SECTIONSET:
    {
      int x=GetIntFromParm(0);
      if ((unsigned int)x < (unsigned int)num_sections)
      {
        section *sec=g_sections+x;
        if (parm2>=0) // get something
        {
          int res=((int*)sec)[parm2];
          if (!parm2)
          {
            // getting text
            mystrcpy(var1,sec->name);
          }
          else
          {
            // getting number
            myitoa(var1,res);
          }
        }
        else // set something
        {
          parm2=-parm2-1;
          if (parm2)
          {
            // not setting text, get int
            parm1=GetIntFromParm(1);
          }
          else
          {
            // setting text
            GetNSISString(sec->name,parm4);
            sec->flags|=SF_NAMECHG;
            // parm1 is zero so name_ptr will be set to zero
            // if name_ptr is zero, it won't be used after .onInit
          }

          ((int*)sec)[parm2]=parm1;

          if (parm3) // update flags
          {
            SectionFlagsChanged(x);
          }
        }
      }
      else exec_error++;
    }
    break;
    case EW_INSTTYPESET:
    {
      int x = GetIntFromParm(0);

      if ((unsigned int)x < (unsigned int)NSIS_MAX_INST_TYPES)
      {
        if (parm3) // current install type
        {
          if (parm2) // set install type
          {
            SetInstType(x);
            RefreshSectionGroups();
          }
          else // get install type
          {
            myitoa(var1, GetInstType(0));
          }
        }
        else // install type text
        {
          if (parm2) // set text
          {
            g_header->install_types[x] = parm1;
          }
          else // get text
          {
            GetNSISString(var1,g_header->install_types[x]);
          }
        }
      }
      else exec_error++;
    }
    break;
#endif//NSIS_CONFIG_COMPONENTPAGE

    case EW_GETOSINFO:
    {
      switch(parm3)
      {
#ifdef NSIS_SUPPORT_FNUTIL
        case GETOSINFO_KNOWNFOLDER:
        {
          TCHAR *outstr = var1;
          IID kfid;
          HRESULT(WINAPI*SHGKFP)(REFIID,DWORD,HANDLE,PWSTR*) = (HRESULT(WINAPI*)(REFIID,DWORD,HANDLE,PWSTR*)) myGetProcAddress(MGA_SHGetKnownFolderPath);
          TCHAR *buf2 = GetStringFromParm(0x22), succ = FALSE;
          if (SHGKFP && SUCCEEDED(ComIIDFromString(buf2, &kfid)))
          {
            PWSTR path;
            HRESULT hr = SHGKFP(&kfid, parm3, NULL, &path);
            if (SUCCEEDED(hr))
              strcpyWideToT(outstr, path), CoTaskMemFree(path), ++succ;
          }
          if (!succ)
            exec_error++, *outstr = _T('\0');
        }
        break;
#endif
        case GETOSINFO_READMEMORY:
        {
          TCHAR *outstr = var1;
          SIZE_T addr = GetIntPtrFromParm(2), spec = GetIntPtrFromParm(4), value = 0;
          UINT cb = LOBYTE(spec), offset = (UINT)(spec) >> 24;
          if (addr == ABI_OSINFOADDRESS) addr = (SIZE_T) (&g_execflags_and_osinfo);
          mini_memcpy(&value, ((char*) addr) + offset, cb);
          iptrtostr(outstr, value);
        }
        break;
      }
    }
    break;
#ifdef NSIS_LOCKWINDOW_SUPPORT
    case EW_LOCKWINDOW:
    {
      // ui_dlg_visible is 1 or 0, so is parm0. It is used because WM_SETREDRAW will toggle WS_VISIBLE!
      // BUGBUG: This has unfortunate consequences when used in
      // combination with BringToFront on the first page.
      // See http://forums.winamp.com/showthread.php?t=397781 for details.
      SendMessage(g_hwnd, WM_SETREDRAW, parm0 & ui_dlg_visible, 0);
      if (parm0) InvalidateRect(g_hwnd, NULL, FALSE);
      break;
    }
#endif //NSIS_LOCKWINDOW_SUPPORT
  }

  g_exec_flags.exec_error += exec_error;

  return 0;
}
