/*
 * util.c
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
 * Unicode support by Jim Park -- 08/11/2007
 */

#include "../Platform.h"
#include <shellapi.h>
#include "util.h"
#include "state.h"
#include "config.h"
#include "lang.h"
#include "fileform.h"
#include "exec.h"
#include "ui.h"
#include "resource.h"
#include "../tchar.h"

#ifdef NSIS_CONFIG_LOG
#if !defined(NSIS_CONFIG_LOG_ODS) && !defined(NSIS_CONFIG_LOG_STDOUT)
TCHAR g_log_file[1024];
#endif
#endif

// *** DO NOT DECLARE MORE VARIABLES INSIDE THESE PRAGMAS ***
// This will produce a special section called ".ndata" (stands for nsis data)
// this way makensis during build time, can search for this section by name
// and change the virtual size of this section
// which results in extra memory for extra variables without code to do allocation :)
// nsis then removes the "DISCARDABLE" style from section (for safe)
#ifdef _MSC_VER
#  pragma bss_seg(NSIS_VARS_SECTION)
NSIS_STRING g_usrvarssection[1];
#  pragma bss_seg()
#  pragma comment(linker, "/section:" NSIS_VARS_SECTION ",rwd")
#else
#  ifdef __GNUC__
// GCC does not treat g_usrvarssection as a bss section so we keep the size as small as possible.
// NSIS_STRING g_usrvarssection[31] is required to remove this hack but that really bloats the exehead.
TCHAR g_usrvarssection[1] __attribute__((section (NSIS_VARS_SECTION)));
const NSIS_STRING*const g_usrvarsstart = (const NSIS_STRING*const) g_usrvarssection;
#  else
#    error Unknown compiler. You must implement the separate PE section yourself.
#  endif
#endif

const UINT32 g_restrictedacl[] = {
  0x00340002, 0x00000002, // ACL (ACL_REVISION2, 2 ACEs)
  0x00180300, // ACCESS_ALLOWED_ACE:ACE_HEADER (ACCESS_ALLOWED_ACE_TYPE, CONTAINER_INHERIT_ACE|OBJECT_INHERIT_ACE)
  0x10000000, // ACCESS_ALLOWED_ACE:ACCESS_MASK: GENERIC_ALL
  0x00000201, 0x05000000, 0x00000020, 0x00000220, // ACCESS_ALLOWED_ACE:SID (BUILTIN\Administrators) NOTE: GetAdminGrpSid() relies on this being the first SID in the ACL
  0x00140300, // ACCESS_ALLOWED_ACE:ACE_HEADER (ACCESS_ALLOWED_ACE_TYPE, CONTAINER_INHERIT_ACE|OBJECT_INHERIT_ACE)
  0x001200c1, // ACCESS_ALLOWED_ACE:ACCESS_MASK: SYNCHRONIZE|READ_CONTROL|FILE_LIST_DIRECTORY|FILE_DELETE_CHILD|FILE_READ_ATTRIBUTES
  0x00000101, 0x01000000, 0x00000000 // ACCESS_ALLOWED_ACE:SID (WORLD\Everyone)
};

DWORD NSISCALL CreateRestrictedDirectory(LPCTSTR path)
{
  PSID admingrpsid = GetAdminGrpSid();
  SECURITY_DESCRIPTOR sd = { 1, 0, SE_DACL_PRESENT, admingrpsid, admingrpsid, NULL, GetAdminGrpAcl() };
  SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), &sd, FALSE };
  return CreateDirectory(path, &sa) ? ERROR_SUCCESS : GetLastError();
}
DWORD NSISCALL CreateNormalDirectory(LPCTSTR path)
{
  return CreateDirectory(path, NULL) ? ERROR_SUCCESS : GetLastError();
}

BOOL NSISCALL UserIsAdminGrpMember()
{
  FARPROC iuaa = myGetProcAddress(MGA_IsUserAnAdmin);
  return iuaa && ((BOOL(WINAPI*)())iuaa)();
}

HANDLE NSISCALL myCreateProcess(TCHAR *cmd)
{
  PROCESS_INFORMATION ProcInfo;
  static STARTUPINFO StartUp;
  StartUp.cb=sizeof(StartUp);
  if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &StartUp, &ProcInfo))
    return NULL;
  CloseHandle(ProcInfo.hThread);
  return ProcInfo.hProcess;
}

BOOL NSISCALL myShellExecuteEx(SHELLEXECUTEINFO*pSEI)
{
  pSEI->cbSize = sizeof(SHELLEXECUTEINFO);
  pSEI->lpIDList = NULL; // Must set this because SEE_MASK_INVOKEIDLIST might be set by ExecShell[Wait]
  return ShellExecuteEx(pSEI);
}

/*BOOL NSISCALL my_SetWindowText(HWND hWnd, const TCHAR *val)
{
  return SendMessage(hWnd,WM_SETTEXT,0,(LPARAM)val);
}*/

BOOL NSISCALL my_SetDialogItemText(HWND dlg, UINT idx, const TCHAR *val)
{
  return SetDlgItemText(dlg,idx,val);
//  return my_SetWindowText(GetDlgItem(dlg, idx), val);
}

int NSISCALL my_GetDialogItemText(UINT idx, TCHAR *val)
{
  extern HWND m_curwnd;
  return GetDlgItemText(m_curwnd, idx, val, NSIS_MAX_STRLEN);
//  return my_GetWindowText(GetDlgItem(m_curwnd, idx), val, NSIS_MAX_STRLEN);
}

int NSISCALL my_MessageBox(const TCHAR *text, UINT type) {
  int _type = type & 0x001FFFFF;
  static MSGBOXPARAMS mbp = {
    sizeof(MSGBOXPARAMS),
    0,
    0,
    0,
    0,
    0,
    MAKEINTRESOURCE(IDI_ICON2),
    0,
    0,
    0
  };
  
#ifdef NSIS_CONFIG_SILENT_SUPPORT
  // default for silent installers
  if (g_exec_flags.silent && type >> 21)
    return type >> 21;
#endif
  // no silent or no default, just show
  if (g_exec_flags.rtl)
    _type ^= MB_RIGHT | MB_RTLREADING;

  mbp.hwndOwner = g_hwnd;
  mbp.hInstance = g_hInstance;
  mbp.lpszText = text;
  mbp.lpszCaption = g_caption; // Should call update_caption() here?
  mbp.dwStyle = _type;
  
  return MessageBoxIndirect(&mbp);
}

BOOL NSISCALL delete_with_ro_attr_handling(LPCTSTR fileordir,int flags) 
{
  const DWORD attr=remove_ro_attr(fileordir);
  if (attr != INVALID_FILE_ATTRIBUTES) 
  {
    if (flags & DEL_DIR)
    {
      if (RemoveDirectory(fileordir)) return TRUE;
    }
    else
    {
      if (DeleteFile(fileordir)) return TRUE;
    }

    // Not sure if wininit.ini and MoveFileEx handle RO attr in the same 
    // way so we just play it safe
    if (!(flags & DEL_REBOOT)) SetFileAttributes(fileordir,attr);
  }
  return FALSE;
}

void NSISCALL myDelete(TCHAR *buf, int flags)
{
  static TCHAR lbuf[NSIS_MAX_STRLEN];
  const int rebootflag=(flags & DEL_REBOOT);

  HANDLE h;
  WIN32_FIND_DATA fd;
  TCHAR *fn;
  int valid_dir=is_valid_instpath(buf);

  if ((flags & DEL_SIMPLE))
  {
    g_exec_flags.exec_error += !DeleteFile(buf);
    return;
  }

#ifdef NSIS_SUPPORT_RMDIR
  if (!(flags & DEL_DIR) || (valid_dir && (flags & DEL_RECURSE)))
#endif//NSIS_SUPPORT_RMDIR
  {
    mystrcpy(lbuf,buf);
#ifdef NSIS_SUPPORT_RMDIR
    if (flags & DEL_DIR)
      mystrcat(lbuf,_T("\\*.*"));
    else
#endif//NSIS_SUPPORT_RMDIR
      trimslashtoend(buf);

    // only append backslash if the path isn't relative to the working directory [bug #1851273]
    if (*buf || *lbuf == _T('\\'))
      mystrcat(buf,_T("\\"));

    fn=buf+mystrlen(buf);

    h = FindFirstFile(lbuf,&fd);
    if (h != INVALID_HANDLE_VALUE)
    {
      do
      {
        TCHAR *fdfn = fd.cFileName;
#ifndef _UNICODE
        if (*findchar(fdfn, _T('?')) && *fd.cAlternateFileName)
          // name contains unicode, use short name
          fdfn = fd.cAlternateFileName;
#endif

#ifdef NSIS_SUPPORT_RMDIR
        if (fdfn[0] == _T('.') && !fdfn[1]) continue;
        if (fdfn[0] == _T('.') && fdfn[1] == _T('.') && !fdfn[2]) continue;
#endif//NSIS_SUPPORT_RMDIR
        {
          mystrcpy(fn,fdfn);
          if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
          {
#ifdef NSIS_SUPPORT_RMDIR
            if ((flags & (DEL_DIR | DEL_RECURSE)) == (DEL_DIR | DEL_RECURSE))
            {
              myDelete(buf,flags);
            }
#endif//NSIS_SUPPORT_RMDIR
          }
          else
          {
            log_printf2(_T("Delete: DeleteFile(\"%s\")"),buf);
            
            if (!delete_with_ro_attr_handling(buf,rebootflag))
            {
#ifdef NSIS_SUPPORT_MOVEONREBOOT
              if (rebootflag)
              {
                log_printf2(_T("Delete: DeleteFile on Reboot(\"%s\")"),buf);
                update_status_text(LANG_DELETEONREBOOT,buf);
                MoveFileOnReboot(buf,NULL);
              }
              else
#endif//NSIS_SUPPORT_MOVEONREBOOT
              {
                log_printf2(_T("Delete: DeleteFile failed(\"%s\")"),buf);
                g_exec_flags.exec_error++;
              }
            }
            else
              update_status_text(LANG_DELETEFILE,buf);
          }
        }
      } while (FindNextFile(h,&fd));
      FindClose(h);
    }

#ifdef NSIS_SUPPORT_RMDIR
    if (flags & DEL_DIR)
      fn[-1]=0;
#endif//NSIS_SUPPORT_RMDIR
  }

#ifdef NSIS_SUPPORT_RMDIR
  if ((flags & DEL_DIR))
  {
    if (!valid_dir)
    {
      log_printf2(_T("RMDir: RemoveDirectory invalid input(\"%s\")"),buf);
      g_exec_flags.exec_error++;
    }
    else if (file_exists(buf))
    {
      addtrailingslash(buf);
      log_printf2(_T("RMDir: RemoveDirectory(\"%s\")"),buf);
      if (!delete_with_ro_attr_handling(buf,DEL_DIR|rebootflag))
      {
#ifdef NSIS_SUPPORT_MOVEONREBOOT
        if (rebootflag)
        {
          log_printf2(_T("RMDir: RemoveDirectory on Reboot(\"%s\")"),buf);
          update_status_text(LANG_DELETEONREBOOT,buf);
          MoveFileOnReboot(buf,NULL);
        }
        else
#endif//NSIS_SUPPORT_MOVEONREBOOT
        {
          log_printf2(_T("RMDir: RemoveDirectory failed(\"%s\")"),buf);
          g_exec_flags.exec_error++;
        }
      }
      else
      {
        update_status_text(LANG_REMOVEDIR,buf);
      }
    }
  }
#endif//NSIS_SUPPORT_RMDIR
}

TCHAR *NSISCALL addtrailingslash(TCHAR *str)
{
  if (lastchar(str)!=_T('\\')) mystrcat(str,_T("\\"));
  return str;
}

/*char NSISCALL lastchar(const char *str)
{
  return *CharPrev(str,str+mystrlen(str));
}*/

TCHAR * NSISCALL findchar(TCHAR *str, TCHAR c)
{
  while (*str && *str != c)
  {
    str = CharNext(str);
  }
  return str;
}

// Separates a full path to the directory portion and file name portion
// and returns the pointer to the filename portion.
TCHAR * NSISCALL trimslashtoend(TCHAR *buf)
{
  TCHAR *p = buf + mystrlen(buf);
  do
  {
    if (*p == _T('\\'))
      break;
    p = CharPrev(buf, p);
  } while (p > buf);

  *p = 0;

  return p + 1;
}

int NSISCALL validpathspec(TCHAR *ubuf)
{
  TCHAR dl = ubuf[0] | 0x20; // convert alleged drive letter to lower case
  return ((ubuf[0] == _T('\\') && ubuf[1] == _T('\\')) ||
          (dl >= _T('a') && dl <= _T('z') && ubuf[1] == _T(':')));
}

TCHAR * NSISCALL skip_root(TCHAR *path)
{
  TCHAR *p = CharNext(path);
  TCHAR *p2 = CharNext(p);

  if (*path && p[0] == _T(':') && p[1] == _T('\\'))
  {
    return CharNext(p2);
  }
  else if (path[0] == _T('\\') && path[1] == _T('\\'))
  {
    // skip host and share name
    int x = 2;
    while (x--)
    {
      p2 = findchar(p2, _T('\\'));
      if (!*p2)
        return NULL;
      p2++; // skip backslash
    }

    return p2;
  }
  else
    return NULL;
}

int NSISCALL is_valid_instpath(TCHAR *s)
{
  static TCHAR tmp[NSIS_MAX_STRLEN];
  TCHAR *root;

  mystrcpy(tmp, s);

  root = skip_root(tmp);

  if (!root)
    return 0;

  // must be called after skip_root or AllowRootDirInstall won't work.
  // validate_filename removes trailing backslashes and so converts
  // "C:\" to "C:" which is not a valid directory. skip_root returns
  // NULL for "C:" so the above test returns 0.
  // validate_filename is called so directories such as "C:\ " will
  // not pass as a valid non-root directory.
  validate_filename(root);

  if ((g_flags & CH_FLAGS_NO_ROOT_DIR) && (!*root || *root == _T('\\')))
    return 0;

  while (mystrlen(tmp) > root - tmp)
  {
    WIN32_FIND_DATA *fd = file_exists(tmp);
    // if the directory bit not set then it's a file, which is not a valid inst dir...
    // GetFileAttributes is not used because it doesn't work with certain files (error 32)
    // as for concerns of the user using * or ?, that's invalid anyway...
    if (fd && !(fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      return 0;
    trimslashtoend(tmp);
  }

  // if the root drive exists
  addtrailingslash(tmp); // don't check the current directory, check the root directory
  if (GetFileAttributes(tmp) == INVALID_FILE_ATTRIBUTES)
    return 0;

  return 1;
}

// Used strictly for the wininit.ini file which is an ASCII file.
char * NSISCALL mystrstriA(char *a, const char *b)
{
  int l = lstrlenA(b);
  while (lstrlenA(a) >= l)
  {
    char c = a[l];
    a[l] = 0;
    if (!lstrcmpiA(a, b))
    {
      a[l] = c;
      return a;
    }
    a[l] = c;
    a = CharNextA(a);
  }
  return NULL;
}


#ifndef _NSIS_NODEFLIB_CRTMEMCPY
// mini_memcpy takes the number of bytes to copy.
void NSISCALL mini_memcpy(void *out, const void *in, UINT_PTR cb)
{
  char *dst = (char*) out, *src = (char*) in;
  while (cb-- > 0) *dst++ = *src++;
}
#endif

DWORD NSISCALL remove_ro_attr(LPCTSTR file)
{
  const DWORD attr = GetFileAttributes(file);
  if (attr != INVALID_FILE_ATTRIBUTES)
    SetFileAttributes(file,attr&(~FILE_ATTRIBUTE_READONLY));
  return attr;
}

HANDLE NSISCALL myOpenFile(const TCHAR *fn, DWORD da, DWORD cd)
{
  int attr = GetFileAttributes(fn);
  return CreateFile(
    fn,
    da,
    FILE_SHARE_READ,
    NULL,
    cd,
    attr == INVALID_FILE_ATTRIBUTES ? 0 : attr,
    NULL
  );
}

TCHAR * NSISCALL my_GetTempFileName(TCHAR *buf, const TCHAR *dir)
{
  int n = 100;
  while (n--)
  {
    TCHAR prefix[4] = _T("nsa");
    prefix[2] += (TCHAR)(GetTickCount() % 26);
    if (GetTempFileName(dir, prefix, 0, buf))
      return buf;
  }
  *buf = 0;
  return 0;
}

BOOL NSISCALL myReadFile(HANDLE h, LPVOID buf, DWORD cb)
{
  DWORD cbio;
  return ReadFile(h, buf, cb, &cbio, NULL) && cb == cbio;
}

BOOL NSISCALL myWriteFile(HANDLE h, const void*buf, DWORD cb)
{
  DWORD cbio;
  return WriteFile(h, buf, cb, &cbio, NULL) && cb == cbio;
}

// Reading skips the BOM if present, writing writes it to a empty file
HRESULT NSISCALL UTF16LEBOM(HANDLE h, INT_PTR ForWrite)
{
  DWORD orgpos = SetFilePointer(h, 0, NULL, FILE_CURRENT);
  if (0 == orgpos)
  {
    BYTE bom[2];
    if (myReadFile(h, bom, 2) && (0xfeff == *(USHORT*) &bom[0]))
    {
      return S_OK;
    }
    else if (ForWrite)
    {
      if (0 == SetFilePointer(h, 0, NULL, FILE_CURRENT)) // Is the file empty?
      {
        static const BYTE bom16le[] = { 0xff, 0xfe };
        return myWriteFile(h, bom16le, 2) ? S_OK : E_FAIL;
      }
    }
    SetFilePointer(h, 0, NULL, FILE_BEGIN); // The file may have started with something that was not a BOM, undo the read
  }
  return S_FALSE;
}

#ifdef NSIS_SUPPORT_MOVEONREBOOT
#ifndef _WIN64
/** Modifies the wininit.ini file to rename / delete a file.
 *
 * @param prevName The previous / current name of the file.
 * @param newName The new name to move the file to.  If NULL, the current file
 * will be deleted.
 */
void RenameViaWininit(const TCHAR* prevName, const TCHAR* newName)
{
  static char szRenameLine[1024];
  static TCHAR wininit[1024];
  static TCHAR tmpbuf[1024];

  int cchRenameLine;
  LPCSTR szRenameSec = "[Rename]\r\n"; // rename section marker
  HANDLE hfile;
  DWORD dwFileSize, dwRenameLinePos;
  char *pszWinInit;   // Contains the file contents of wininit.ini

  int spn;   // length of the short path name in TCHARs.

  // Optimized mystrcpy(tmpbuf, _T("NUL")):
  if (sizeof(TCHAR) == 1)
    *(UINT32*)tmpbuf = ((UINT32)'N' <<  0) | ((UINT32)'U' <<  8) | ((UINT32)'L' << 16) | ((UINT32)'\0' << 24);
  else
    *(UINT64*)tmpbuf = ((UINT64)'N' <<  0) | ((UINT64)'U' << 16) | ((UINT64)'L' << 32) | ((UINT64)'\0' << 48);

  if (newName) {
    // create the file if it's not already there to prevent GetShortPathName from failing
    CloseHandle(myOpenFile(newName,0,CREATE_NEW));
    spn = GetShortPathName(newName,tmpbuf,1024);
    if (!spn || spn > 1024)
      return;
  }
  // wininit is used as a temporary here
  spn = GetShortPathName(prevName,wininit,1024);
  if (!spn || spn > 1024)
    return;
#ifdef _UNICODE
  cchRenameLine = wsprintfA(szRenameLine, "%ls=%ls\r\n", tmpbuf, wininit);
#else
  cchRenameLine = wsprintfA(szRenameLine, "%s=%s\r\n", tmpbuf, wininit);
#endif
  // Get the path to the wininit.ini file.
  GetNSISString(wininit, g_header->str_wininit);

  hfile = myOpenFile(wininit, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS);

  if (hfile != INVALID_HANDLE_VALUE)
  {
    // We are now working on the Windows wininit file
    dwFileSize = GetFileSize(hfile, NULL);
    pszWinInit = (char*) GlobalAlloc(GPTR, dwFileSize + cchRenameLine + 10);

    if (pszWinInit != NULL)
    {
      if (myReadFile(hfile, pszWinInit, dwFileSize))
      {
        // Look for the rename section in the current file.
        LPSTR pszRenameSecInFile = mystrstriA(pszWinInit, szRenameSec);
        if (pszRenameSecInFile == NULL)
        {
          // No rename section.  So we add it to the end of file.
          lstrcpyA(pszWinInit+dwFileSize, szRenameSec);
          dwFileSize += 10;
          dwRenameLinePos = dwFileSize;
        }
        else
        {
          // There is a rename section, but is there another section after it?
          char *pszFirstRenameLine = pszRenameSecInFile+10;
          char *pszNextSec = mystrstriA(pszFirstRenameLine,"\n[");
          if (pszNextSec)
          {
            char *p = pszWinInit + dwFileSize;
            char *pEnd = pszWinInit + dwFileSize + cchRenameLine;

            while (p > pszNextSec)
            {
              *pEnd-- = *p--;
            }

            dwRenameLinePos = BUGBUG64TRUNCATE(DWORD, pszNextSec - pszWinInit) + 1; // +1 for the \n
          }
          // rename section is last, stick item at end of file
          else dwRenameLinePos = dwFileSize;
        }

        mini_memcpy(&pszWinInit[dwRenameLinePos], szRenameLine, cchRenameLine);
        dwFileSize += cchRenameLine;

        SetFilePointer(hfile, 0, NULL, FILE_BEGIN);
        myWriteFile(hfile, pszWinInit, dwFileSize);

        GlobalFree(pszWinInit);
      }
    }
    
    CloseHandle(hfile);
  }
}
#endif

/**
 * MoveFileOnReboot tries to move a file by the name of pszExisting to the
 * name pszNew.
 *
 * @param pszExisting The old name of the file.
 * @param pszNew The new name of the file.
 */
void NSISCALL MoveFileOnReboot(LPCTSTR pszExisting, LPCTSTR pszNew)
{
#ifndef _WIN64 // Shut up GCC unused warning
  BOOL fOk =
#endif
    MoveFileEx(pszExisting, pszNew, MOVEFILE_DELAY_UNTIL_REBOOT|MOVEFILE_REPLACE_EXISTING);
#ifndef _WIN64
  if (!fOk)
  {
    RenameViaWininit(pszExisting, pszNew);
  }
#endif

#ifdef NSIS_SUPPORT_REBOOT
  g_exec_flags.exec_reboot++;
#endif
}
#endif

#define GetAltViewREGSAM() ( sizeof(void*) > 4 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY )
HKEY NSISCALL GetRegKeyAndSAM(HKEY hKey, REGSAM*pRS)
{
  const REGSAM samviewmask = (KEY_WOW64_32KEY|KEY_WOW64_64KEY);
  const REGSAM incompatsamview = SystemSupportsAltRegView() ? 0 : GetAltViewREGSAM();
  REGSAM sam = *pRS, incompatsam = incompatsamview;
#ifdef C_ASSERT
  {C_ASSERT(REGROOTVIEWTOSAMVIEW(REGROOTVIEW32|REGROOTVIEW64) == (KEY_WOW64_32KEY|KEY_WOW64_64KEY));}
#endif
  if ((sam & KEY_FORCEVIEW) && IsRegRootkeyForcedView(hKey))
  {
    REGSAM keysamview = REGROOTVIEWTOSAMVIEW(hKey);
    if (keysamview == samviewmask) keysamview = (g_exec_flags.alter_reg_view & ~incompatsamview); // HKxxANY tries to honor SetRegView
    sam &= ~samviewmask, sam |= (keysamview & ~(sizeof(void*) > 4 ? 0 : KEY_WOW64_32KEY)); // HKxx32 has the *_32KEY bit set but WinNT4&2000 cannot handle any KEY_WOW64_xxKEY flags.
    hKey = (HKEY) ( (UINT_PTR) hKey & ~(REGROOTVIEW32|REGROOTVIEW64) );
  }
  else if (sam & KEY_ALTERVIEW)
  {
    sam |= g_exec_flags.alter_reg_view; // We don't mask away the incompatsamview bits because the operation is supposed to fail if the view is not supported.
  }
  *pRS = sam & ~(NSIS_REGSAM_PRIVATEMASK); // Filter away internal flags
  return (incompatsam & sam) ? NULL : hKey; // Fail if the requested view is not supported
}
LONG NSISCALL RegKeyOpen(HKEY hBase, LPCTSTR SubKey, REGSAM RS, HKEY*phKey)
{
  if (!(hBase = GetRegKeyAndSAM(hBase, &RS))) return ERROR_INVALID_HANDLE; // ERROR_CANTOPEN?
  return RegOpenKeyEx(hBase, SubKey, 0, RS, phKey);
}
LONG NSISCALL RegKeyCreate(HKEY hBase, LPCTSTR SubKey, REGSAM RS, HKEY*phKey)
{
  if (!(hBase = GetRegKeyAndSAM(hBase, &RS))) return ERROR_INVALID_HANDLE; // ERROR_CANTOPEN?
  return RegCreateKeyEx(hBase, SubKey, 0, 0, 0, RS, 0, phKey, 0);
}

void NSISCALL myRegGetStr(HKEY root, const TCHAR *sub, const TCHAR *name, TCHAR *out, UINT altview)
{
  HKEY hKey;
  DWORD cb = NSIS_MAX_STRLEN*sizeof(TCHAR), rt, ec;
  REGSAM samview = altview ? GetAltViewREGSAM() : 0;
  if ((ec = RegKeyOpen(root, sub, KEY_READ|samview, &hKey)) == ERROR_SUCCESS)
  {
    ec = RegQueryValueEx(hKey, name, NULL, &rt, (LPBYTE)out, &cb);
    RegCloseKey(hKey);
    out[NSIS_MAX_STRLEN-1] = 0; // Make sure the string is terminated. This could potentially truncate a long string by 1 character!
  }
  if (ec != ERROR_SUCCESS || (rt != REG_SZ && rt != REG_EXPAND_SZ)) *out = 0; // Empty string on failure
}

void NSISCALL iptrtostr(TCHAR *s, INT_PTR d)
{
#ifdef _WIN64
  static const TCHAR c[] = _T("%Id"); 
#else
  static const TCHAR c[] = _T("%d");
#endif
  wsprintf(s,c,d);
}

INT_PTR NSISCALL strtoiptr(const TCHAR *s)
{
  UINT_PTR v=0;
  INT_PTR sign=1; // sign of positive
  TCHAR m=10; // base of 10
  TCHAR t=_T('9'); // cap top of numbers at 9

  if (*s == _T('-'))
  {
    s++;  //skip over -
    sign=-1; // sign flip
  }

  if (*s == _T('0'))
  {
    s++; // skip over 0
    if (s[0] >= _T('0') && s[0] <= _T('7'))
    {
      m=8; // base of 8
      t=_T('7'); // cap top at 7
    }
    if ((s[0] & ~0x20) == _T('X'))
    {
      m=16; // base of 16
      s++; // advance over 'x'
    }
  }

  for (;;)
  {
    int c=*s++;
    if (c >= _T('0') && c <= t) c-=_T('0');
    // clever little trick to do both upper and lowercase A-F.
    else if (m==16 && (c & ~0x20) >= _T('A') && (c & ~0x20) <= _T('F')) c = (c & 7) + 9;
    else break;
    v*=m;
    v+=c;
  }
  return ((INT_PTR)v)*sign;
}

// Straight copies of selected shell functions.  Calling local functions
// requires less code than DLL functions.  For the savings to outweigh the cost
// of a new function there should be about a couple of dozen or so calls.
TCHAR * NSISCALL mystrcpy(TCHAR *out, const TCHAR *in)
{
  return lstrcpyn(out, in, NSIS_MAX_STRLEN);
}

int NSISCALL mystrlen(const TCHAR *in)
{
  return lstrlen(in);
}

TCHAR * NSISCALL mystrcat(TCHAR *out, const TCHAR *concat)
{
  return lstrcat(out, concat);
}

int StrWideToACP(LPCWSTR Src, char* Dst, int DstCap)
{
  return WideCharToMultiByte(CP_ACP, 0, Src, -1, Dst, DstCap, NULL, NULL);
}

#ifndef UNICODE
void strcpyWideToT(TCHAR *out, LPCWSTR in)
{
  StrWideToACP(in, out, NSIS_MAX_STRLEN);
}
#endif

#if !defined(_WIN64) && !defined(UNICODE)
HRESULT ComIIDFromString(LPCTSTR str, IID*out)
{
  WCHAR buf[130];
  signed char i;
  for (i = 0; i >= 0; ++i)
    if (!(buf[i] = str[i]))
      return IIDFromString(buf, out);
  return E_FAIL;
}
#endif

TCHAR ps_tmpbuf[NSIS_MAX_STRLEN*2];

const TCHAR SYSREGKEY[]   = _T("Software\\Microsoft\\Windows\\CurrentVersion");
const TCHAR QUICKLAUNCH[] = _T("\\Microsoft\\Internet Explorer\\Quick Launch");

typedef HRESULT (WINAPI* PFNSHGETFOLDERPATH)(HWND, int, HANDLE, DWORD, LPTSTR);
extern void *g_SHGetFolderPath;
typedef LPITEMIDLIST (WINAPI* PFNSHCLONESPECIALIDLIST)(HWND, int, BOOL);

// Based on Dave Laundon's simplified process_string
// The string actually has a lot of different data encoded into it.  This
// function extracts the special data out and puts it into outbuf.
TCHAR * NSISCALL GetNSISString(TCHAR *outbuf, int strtab)
{
  // This looks at the g_block (copied from header->blocks) and
  // indexes into the language
  TCHAR *in = (TCHAR*)GetNSISStringNP(GetNSISTab(strtab));
  TCHAR *out = ps_tmpbuf;

  // Still working within ps_tmpbuf, so set out to the
  // current position that is passed in.
  if (outbuf >= ps_tmpbuf && 
     (size_t) (outbuf - ps_tmpbuf) < COUNTOF(ps_tmpbuf))
  {
    out = outbuf;
    outbuf = 0;
  }

  while (*in && out - ps_tmpbuf < NSIS_MAX_STRLEN)
  {
    _TUCHAR nVarIdx = (_TUCHAR)*in++;
    int nData;
    int fldrs[4];
    if (nVarIdx < NS_SKIP_CODE)
    {
      // The next 2 BYTEs in the string might be coding either a value 0..MAX_CODED (nData), or 2 CSIDL of Special folders (for NS_SHELL_CODE)
      nData = DECODE_SHORT(in);
      // There are 2 CSIDL parameters for each context and query must be used before create 
      // because of bug #820 (CSIDL_FLAG_CREATE failures on root paths are cached in Vista).
#ifdef _UNICODE
      fldrs[1] = LOBYTE(*in); // current user
      fldrs[0] = fldrs[1] | CSIDL_FLAG_CREATE;
      fldrs[3] = HIBYTE(*in); // all users
      fldrs[2] = fldrs[3] | CSIDL_FLAG_CREATE;
#else
      fldrs[0] = in[0] | CSIDL_FLAG_CREATE; // current user
      fldrs[1] = in[0];
      fldrs[2] = in[1] | CSIDL_FLAG_CREATE; // all users
      fldrs[3] = in[1];
#endif
      in += sizeof(SHORT)/sizeof(TCHAR);

      if (nVarIdx == NS_SHELL_CODE)
      {
        LPITEMIDLIST idl;

        int x = 2;
        BOOL isWin9598 = IsWin9598();
        /*
        SHGetFolderPath as provided by shfolder.dll is used to get special folders
        unless the installer is running on Windows 95/98. For 95/98 shfolder.dll is
        only used for the Application Data and Documents folder (if the DLL exists).
        Otherwise, the old SHGetSpecialFolderLocation API is called.

        The reason for not using shfolder.dll for all folders on 95/98 is that some
        unsupported folders (such as the Start Menu folder for all users) are
        simulated instead of returning an error so we can fall back on the current
        user folder.

        SHGetFolderPath in shell32.dll could be called directly for Windows versions
        later than 95/98 but there is no need to do so, because shfolder.dll is still
        provided and calls shell32.dll.
        */
        BOOL use_shfolder =
          // Use shfolder if not on 95/98
          !isWin9598 ||
          // Unless the Application Data or Documents folder is requested
          (
            (fldrs[3] == CSIDL_COMMON_APPDATA) ||
            (fldrs[3] == CSIDL_COMMON_DOCUMENTS)
          );

        /* Carry on... shfolder stuff is over. */

        if (g_exec_flags.all_user_var)
        {
          x = 4; // Get common folder > Create common folder > Get user folder > Create user folder
        }

        if (fldrs[1] & 0x80)
        {
          myRegGetStr(HKEY_LOCAL_MACHINE, SYSREGKEY, GetNSISStringNP(fldrs[1] & 0x3F), out, fldrs[1] & 0x40);
          if (!*out)
            GetNSISString(out, fldrs[3]);
          x = 0;
        }
        else if (fldrs[1] == CSIDL_SYSTEM) // Does not work on 95, 98 nor NT4. Works on ME and 2000+.
        {
          GetSystemDirectory(out, NSIS_MAX_STRLEN);
          x = 0;
        }
        else if (fldrs[1] == CSIDL_WINDOWS) // Does not work on 95, 98 nor NT4. Works on ME and 2000+.
        {
          GetWindowsDirectory(out, NSIS_MAX_STRLEN);
          x = 0;
        }

        while (x--)
        {
          PFNSHCLONESPECIALIDLIST SHCSILFunc;
          int idandflags = fldrs[x] & ~0x40, create = idandflags & CSIDL_FLAG_CREATE;

          if (g_SHGetFolderPath && use_shfolder)
          {
            PFNSHGETFOLDERPATH SHGetFolderPathFunc = (PFNSHGETFOLDERPATH) g_SHGetFolderPath;
            if (!SHGetFolderPathFunc(g_hwnd, idandflags, NULL, SHGFP_TYPE_CURRENT, out))
            {
              break;
            }
          }

          // This function exists in 95 & NT4 but was undocumented back then so we have to import it by ordinal.
          SHCSILFunc = (PFNSHCLONESPECIALIDLIST) myGetProcAddress(MGA_SHCloneSpecialIDList);
          idl = SHCSILFunc(g_hwnd, LOBYTE(idandflags), create);
          if (idl)
          {
            BOOL res = SHGetPathFromIDList(idl, out);
            CoTaskMemFree(idl);
            if (res) break;
          }

          *out=0;
        }

        if (*out)
        {
          // all users' version is CSIDL_APPDATA only for $QUICKLAUNCH
          // for normal $APPDATA, it'd be CSIDL_APPDATA_COMMON
          if (fldrs[3] == CSIDL_APPDATA)
          {
            mystrcat(out, QUICKLAUNCH); // append suffix path for $QUICKLAUNCH
          }
        }
        validate_filename(out);
      }
      else if (nVarIdx == NS_VAR_CODE)
      {
        if (nData == 29) // $HWNDPARENT
          iptrtostr(out, (INT_PTR) g_hwnd);
        else
          mystrcpy(out, g_usrvars[nData]);
        // validate the directory name
        if ((unsigned int)(nData - 21) < 7) {
          // validate paths for $INSTDIR, $OUTDIR, $EXEDIR, $LANGUAGE, $TEMP, $PLUGINSDIR and $EXEPATH
          // $LANGUAGE is just a number anyway...
          validate_filename(out);
        }
      } // == VAR_CODES_START
      else if (nVarIdx == NS_LANG_CODE)
      {
        GetNSISString(out, -nData-1);
      }
      out += mystrlen(out);
    }
    else if (nVarIdx == NS_SKIP_CODE)
    {
      *out++ = *in++;
    }
    else // Normal char
    {
      *out++ = nVarIdx;
    }
  } // while
  *out = 0;
  if (outbuf)
    return mystrcpy(outbuf, ps_tmpbuf);
  return ps_tmpbuf;
}

void NSISCALL validate_filename(TCHAR *in) {
  TCHAR *nono = _T("*?|<>/\":");
  TCHAR *out;
  TCHAR *out_save;

  // ignoring spaces is wrong, _T(" C:\blah") is invalid
  //while (*in == _T(' ')) in = CharNext(in);

  if (in[0] == _T('\\') && in[1] == _T('\\') && in[2] == _T('?') && in[3] == _T('\\'))
  {
    // at least four bytes
    in += 4;
  }
  if (*in)
  {
    // at least two bytes
    if (validpathspec(in)) in += 2;
  }
  out = out_save = in;
  while (*in)
  {
    if ((_TUCHAR)*in > 31 && !*findchar(nono, *in))
    {
      mini_memcpy(out, in, CharNext(in) - in);
      out = CharNext(out);
    }
    in = CharNext(in);
  }
  *out = 0;
  // now trim rightmost backslashes & spaces
  do
  {
    out = CharPrev(out_save, out);
    if (*out == _T(' ') || *out == _T('\\'))
      *out = 0;
    else
      break;
  } while (out_save < out);
}

#ifdef NSIS_CONFIG_LOG
int log_dolog;
TCHAR log_text[2048]; // 1024 for each wsprintf

#if !defined(NSIS_CONFIG_LOG_ODS) && !defined(NSIS_CONFIG_LOG_STDOUT)
void NSISCALL log_write(int close)
{
  static HANDLE fp=INVALID_HANDLE_VALUE;
  if (close)
  {
    if (fp!=INVALID_HANDLE_VALUE)
    {
      CloseHandle(fp);
    }
    fp=INVALID_HANDLE_VALUE;
    return;
  }
  if (log_dolog)
  {
    if (g_log_file[0] && fp==INVALID_HANDLE_VALUE)
    {
      fp = myOpenFile(g_log_file,GENERIC_WRITE,OPEN_ALWAYS);
      if (fp!=INVALID_HANDLE_VALUE)
        SetFilePointer(fp,0,NULL,FILE_END);
    }
    if (fp!=INVALID_HANDLE_VALUE)
    {
      mystrcat(log_text,_T("\r\n"));
      myWriteFile(fp,log_text,mystrlen(log_text)*sizeof(TCHAR));
    }
  }
}
#endif//!NSIS_CONFIG_LOG_ODS && !NSIS_CONFIG_LOG_STDOUT

const TCHAR * _RegKeyHandleToName(HKEY hKey)
{
  if (hKey == HKEY_CLASSES_ROOT) return _T("HKEY_CLASSES_ROOT");
  if (hKey == HKEY_CURRENT_USER) return _T("HKEY_CURRENT_USER");
  if (hKey == HKEY_LOCAL_MACHINE) return _T("HKEY_LOCAL_MACHINE");
  if (hKey == HKEY_USERS) return _T("HKEY_USERS");
  if (hKey == HKEY_PERFORMANCE_DATA) return _T("HKEY_PERFORMANCE_DATA");
  if (hKey == HKEY_CURRENT_CONFIG) return _T("HKEY_CURRENT_CONFIG");
  if (hKey == HKEY_DYN_DATA) return _T("HKEY_DYN_DATA");
  if (hKey == HKSHCTX) return _T("HKSHCTX");
  if (hKey == HKSHCTX32) return _T("HKSHCTX32");
  if (hKey == HKSHCTX64) return _T("HKSHCTX64");
  if (hKey == HKCR32) return _T("HKCR32");
  if (hKey == HKCR64) return _T("HKCR64");
  if (hKey == HKCU32) return _T("HKCU32");
  if (hKey == HKCU64) return _T("HKCU64");
  if (hKey == HKLM32) return _T("HKLM32");
  if (hKey == HKLM64) return _T("HKLM64");
  if (hKey == HKSHCTXANY) return _T("HKSHCTXANY");
  if (hKey == HKCRANY) return _T("HKCRANY");
  if (hKey == HKCUANY) return _T("HKCUANY");
  if (hKey == HKLMANY) return _T("HKLMANY");
  return _T("HK??");
}

void _LogData2Hex(TCHAR *buf, size_t cchbuf, BYTE *data, size_t cbdata)
{
  TCHAR *p = buf;
  int dots = 0;
  size_t i, bufbytes = cchbuf / 3; // 2 hex digits, one space/null

  if (cbdata > bufbytes)
    bufbytes--, dots++;
  else
    bufbytes = cbdata;

  for (i = 0; i < bufbytes; i++)
  {
    wsprintf(p, _T("%02x%c"), data[i], (i == bufbytes - 1) ? _T('\0') : _T(' '));
    p += 3;
  }

  if (dots) mystrcat(buf, _T("..."));
}

#ifdef NSIS_CONFIG_LOG_TIMESTAMP
void log_timestamp(TCHAR *buf)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  wsprintf(buf,_T("[%04hu/%02hu/%02hu %02hu:%02hu:%02hu] "), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}
#else
#  define log_timestamp(x)
#endif//NSIS_CONFIG_LOG_TIMESTAMP

void log_printf(TCHAR *format, ...)
{
#if defined(NSIS_CONFIG_LOG_STDOUT)
  HANDLE hStdOut;
#endif
  va_list val;
  va_start(val,format);

  log_text[0] = _T('\0');
  log_timestamp(log_text);
  wvsprintf(log_text+mystrlen(log_text),format,val);

  va_end(val);
#if defined(NSIS_CONFIG_LOG_ODS)
  if (log_dolog)
    OutputDebugString(log_text);
#elif defined(NSIS_CONFIG_LOG_STDOUT)
  if (log_dolog && (hStdOut = GetStdHandle(STD_OUTPUT_HANDLE)) != INVALID_HANDLE_VALUE)
  {
    const DWORD cch = lstrlen(log_text), cb = cch * sizeof(TCHAR);
#ifdef UNICODE
    DWORD conmode, cchio;
    if (GetConsoleMode(hStdOut, &conmode))
    {
      WriteConsoleW(hStdOut, log_text, cch, &cchio, 0);
      myWriteFile(hStdOut, "\n", 1);
    }
    else
#endif //~ UNICODE
    {
      myWriteFile(hStdOut, log_text, cb);
      myWriteFile(hStdOut, _T("\n"), 1 * sizeof(TCHAR));
    }
  }
#else
  log_write(0);
#endif
}
#endif//NSIS_CONFIG_LOG

// Jim Park: This function is non-reentrant because of the static.
WIN32_FIND_DATA * NSISCALL file_exists(TCHAR *buf)
{
  HANDLE h;
  static WIN32_FIND_DATA fd;
  h = FindFirstFile(buf,&fd);
  if (h != INVALID_HANDLE_VALUE)
  {
    FindClose(h);
    return &fd;
  }
  return NULL;
}

// Jim Park: Keep these as chars since there's only ANSI version of
// GetProcAddress.
struct MGA_FUNC
{
  const char *dll;
  const char *func;
};

struct MGA_FUNC MGA_FUNCS[] = {
#ifdef _UNICODE
  {"KERNEL32", "SetDefaultDllDirectories"},
#ifndef _WIN64
  {"KERNEL32", "GetDiskFreeSpaceExW"},
  {"KERNEL32", "GetUserDefaultUILanguage"},
#endif
#if !defined(_WIN64) || defined(_M_IA64)
  {"ADVAPI32", "RegDeleteKeyExW"},
#endif
  {"ADVAPI32", "InitiateShutdownW"},
  {"SHELL32", "SHGetKnownFolderPath"},
  {"SHELL32", (CHAR*) 680}, // IsUserAnAdmin
  {"SHELL32", (CHAR*) 89}, // SHCloneSpecialIDList
#ifndef _WIN64
  {"SHLWAPI", (CHAR*) 437}, // IsOS
#endif
  {"SHLWAPI",  "SHAutoComplete"},
  {"SHFOLDER", "SHGetFolderPathW"},
#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
  {"OLEAUT32", "OleLoadPictureFileEx"},
#endif
#ifdef NSIS_SUPPORT_GETDLLVERSION
  {"VERSION",  "GetFileVersionInfoSizeW"},
  {"VERSION",  "GetFileVersionInfoW"},
  {"VERSION",  "VerQueryValueW"}
#endif
};
#else
  {"KERNEL32", "SetDefaultDllDirectories"},
  {"KERNEL32", "GetDiskFreeSpaceExA"},
  {"KERNEL32", "GetUserDefaultUILanguage"},
  {"ADVAPI32", "RegDeleteKeyExA"},
  {"ADVAPI32", "InitiateShutdownA"},
  {"SHELL32", "SHGetKnownFolderPath"},
  {"SHELL32", (CHAR*) 680}, // IsUserAnAdmin
  {"SHELL32", (CHAR*) 89}, // SHCloneSpecialIDList
#ifndef _WIN64
  {"SHLWAPI", (CHAR*) 437}, // IsOS
#endif
  {"SHLWAPI",  "SHAutoComplete"},
  {"SHFOLDER", "SHGetFolderPathA"},
#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
  {"OLEAUT32", "OleLoadPictureFileEx"},
#endif
#ifdef NSIS_SUPPORT_GETDLLVERSION
  {"VERSION",  "GetFileVersionInfoSizeA"},
  {"VERSION",  "GetFileVersionInfoA"},
  {"VERSION",  "VerQueryValueA"}
#endif
};
#endif

HMODULE NSISCALL LoadSystemLibrary(LPCSTR name)
{
  LPCTSTR fmt = sizeof(*fmt) > 1 ? TEXT("%s%S.dll") : TEXT("%s%s.dll"); // The module name is always ANSI
  BYTE bytebuf[(MAX_PATH+1+20+1+3+!0) * sizeof(*fmt)]; // 20+4 is more than enough for 
  LPTSTR path = (LPTSTR) bytebuf;                      // the dllnames we are using.

  UINT cch = GetSystemDirectory(path, MAX_PATH);
  if (cch > MAX_PATH) // MAX_PATH was somehow not large enough and we don't support 
    cch = 0;          // \\?\ paths so we have to settle for just the name.
  wsprintf(path + cch, fmt, TEXT("\\") + (!cch || path[cch-1] == '\\'), name);

  return LoadLibraryEx(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

/**
 * Given a function enum, it will load the appropriate DLL and get the
 * process address of the function and return the pointer.  It's up to
 * the caller to know how to call that function, however.
 *
 * @param func Enum value that indexes the MGA_FUNCS array.
 * @return Pointer to the function identified by the enum value.
 */
void* NSISCALL myGetProcAddress(const enum myGetProcAddressFunctions func)
{
  const char *dllname = MGA_FUNCS[func].dll;
  HMODULE hModule;

  hModule = GetModuleHandleA(dllname);    // Avoid LoadLibrary if possible because 
  if (!hModule)                           // it can crash on 64-bit dlls if 
    hModule = LoadSystemLibrary(dllname); // WoW64 FS redirection is off.

  return hModule
    ? GetProcAddress(hModule, MGA_FUNCS[func].func)
    : (FARPROC) hModule; // Optimized "return NULL;"
}

void NSISCALL MessageLoop(UINT uCheckedMsg)
{
  MSG msg;
  while (PeekMessage(&msg, NULL, uCheckedMsg, uCheckedMsg, PM_REMOVE))
    DispatchMessage(&msg);
}

/**
 * This function is useful for Unicode support.  Since the Windows
 * GetProcAddress function always takes a char*, this function wraps
 * the windows call and does the appropriate translation when
 * appropriate.
 *
 * @param dllHandle Handle to the DLL loaded by LoadLibrary[Ex].
 * @param funcName The name of the function to get the address of.
 * @return The pointer to the function.  Null if failure.
 */
void * NSISCALL NSISGetProcAddress(HANDLE dllHandle, TCHAR* funcName)
{
#ifdef _UNICODE
  char ansiName[256];
  if (StrWideToACP(funcName, ansiName, 256) != 0)
    return GetProcAddress(dllHandle, ansiName);
  return NULL;
#else
  return GetProcAddress(dllHandle, funcName);
#endif
}

DWORD NSISCALL WaitForProcess(HANDLE hProcess)
{
  DWORD excod;
  while (WaitForSingleObject(hProcess, 100) == WAIT_TIMEOUT)
    MessageLoop(WM_PAINT);

  GetExitCodeProcess(hProcess, &excod);
  return excod;
}
