
#include "../../Source/Platform.h"
#undef _tcsrchr // The fix for bug #1085 causes a MSVC redefinition warning when <tchar.h> is included by zlib/unzip.h -> zlib/ioapi.h.

// Platform.h includes our custom tchar.h and
// VS2015 does not like this because we are about to pull in its tchar.h.
// As a temporary workaround we just undefine the things it disagrees with:
#undef _vstprintf
#undef _tcstok

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <ctype.h>
#include <commctrl.h>
#include <shlwapi.h>

#ifndef COUNTOF
#define COUNTOF(a) (sizeof(a)/sizeof(a[0]))
#endif

/*
version 0.40 (by Anders Kjersem)
* The compressor can be specified with the ZIP2EXE_COMPRESSOR define

version 0.39 (by Anders Kjersem)
* Accept .zip as command line parameter

version 0.38 (by Anders Kjersem)
* Aborts if the zip file is encrypted

version 0.37 (by Anders Kjersem)
* Unicode checkbox
* No output log length limit
* Fixed tab order

version 0.36
* Unicode support by Jim Park -- 08/27/2007
* This support allow Unicode *ZIP file* names but does NOT allow the archive
* to store Unicode files inside it.  That's a ZLIB limitation that I can't
* do much about.

version 0.35
* drag & drop support

version 0.34
* preserve zip timestamps

version 0.33
* Added solid compression checkbox

version 0.32
* Fixed codepage problems

version 0.31 (by Joost Verburg)
* LZMA compression support
* Fixed compression setting

version 0.31 (by Joost Verburg)
* Based on header files
* Improved interface
* Modern UI support
* New script code
* Immproved folder detection

portions Copyright © 1999-2001 Miguel Garrido (mgarrido01@hotmail.com)

*/

extern "C"
{
#include "zlib/unzip.h"
};
#include "resource.h"

#define WM_NOTIFYENDCOMPILE WM_APP

const TCHAR *g_errcaption=_T("Zip2Exe Error");
const TCHAR *g_options=_T("/V3 /OUTPUTCHARSET UTF8");

HINSTANCE g_hInstance;
HWND g_hwnd;
HANDLE g_hThread;
TCHAR g_cmdline[1024];
TCHAR tempzip_path[1024];
TCHAR nsifilename[MAX_PATH];
int g_extracting;
int g_compressor;
int g_compressor_solid;
int g_mui;
int g_zipfile_size;
bool g_made;


static INT_PTR CALLBACK DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

NSIS_ENTRYPOINT_SIMPLEGUI
int WINAPI _tWinMain(HINSTANCE hInst,HINSTANCE hOldInst,LPTSTR CmdLineParams,int ShowCmd)
{
  InitCommonControls();
  g_hInstance=hInst;
  return (int) DialogBox(g_hInstance,MAKEINTRESOURCE(IDD_DIALOG1),0,DlgProc);
}

enum { COMPRESSORID_FIRST = IDC_ZLIB, COMPRESSORID_LAST = IDC_ZSTD };

static LPCTSTR GetCompressorName()
{
  #ifdef C_ASSERT
  C_ASSERT(4 == COMPRESSORID_LAST - COMPRESSORID_FIRST + 1);
  #endif
  switch (g_compressor)
  {
  case IDC_ZLIB  + 1 - COMPRESSORID_FIRST: return _T("ZLIB");
  case IDC_BZIP2 + 1 - COMPRESSORID_FIRST: return _T("BZIP2");
  case IDC_ZSTD  + 1 - COMPRESSORID_FIRST: return _T("ZSTD");
  case IDC_LZMA  + 1 - COMPRESSORID_FIRST: return _T("LZMA");
  default: return _T("?");
  }
}

static int GetCheckedCompressor(HWND hwndDlg)
{
  for (UINT id = COMPRESSORID_FIRST; id <= COMPRESSORID_LAST; ++id)
    if (IsDlgButtonChecked(hwndDlg,id))
      return id + 1 - COMPRESSORID_FIRST;
  return 0;
}

static bool IsEncrypted(unz_file_info&zfi)
{
  const unsigned short gpf_encrypted = (1<< 0); // 2.0.0+
  //nst unsigned short gpf_encstrong = (1<< 6); // 5.0.0+ APPNOTE says that bit 0 MUST be set if bit 6 is set
  const unsigned short gpf_enccntdir = (1<<13); // 6.2.0+ Central Directory Encryption
  return (zfi.flag & (gpf_encrypted|gpf_enccntdir)) != 0;
}

static void doRMDir(TCHAR *buf)
{
  HANDLE h;
  WIN32_FIND_DATA fd;
  TCHAR *p=buf;
  while (*p) p++;
  lstrcpy(p,_T("\\*.*"));
  h = FindFirstFile(buf,&fd);
  if (h != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (fd.cFileName[0] != _T('.') ||
          (fd.cFileName[1] != _T('.') && fd.cFileName[1]))
      {
        lstrcpy(p+1,fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
          SetFileAttributes(buf,fd.dwFileAttributes^FILE_ATTRIBUTE_READONLY);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) doRMDir(buf);
        else
        {
          DeleteFile(buf);
        }
      }
    } while (FindNextFile(h,&fd));
    FindClose(h);
  }
  p[0]=0; // fix buffer
  RemoveDirectory(buf);
}

static void doMKDir(TCHAR *directory)
{
  TCHAR *p, *p2;
  TCHAR buf[MAX_PATH];
  if (!*directory) return;
  lstrcpy(buf,directory);
  p=buf; while (*p) p++;
  while (p >= buf && *p != _T('\\')) p--;
  p2 = buf;
  if (p2[1] == _T(':')) p2+=4;
  else if (p2[0] == _T('\\') && p2[1] == _T('\\'))
  {
    p2+=2;
    while (*p2 && *p2 != _T('\\')) p2++;
    if (*p2) p2++;
    while (*p2 && *p2 != _T('\\')) p2++;
    if (*p2) p2++;
  }
  if (p >= p2)
  {
    *p=0;
    doMKDir(buf);
  }
  CreateDirectory(directory,NULL);
}


void tempzip_cleanup(HWND hwndDlg, int err)
{
  if (tempzip_path[0]) doRMDir(tempzip_path);
  tempzip_path[0]=0;
  if (err)
  {
    SendDlgItemMessage(hwndDlg,IDC_ZIPINFO_FILES,LB_RESETCONTENT,0,0);
    EnableWindow(GetDlgItem(hwndDlg,IDOK),0);
    SetDlgItemText(hwndDlg,IDC_ZIPINFO_SUMMARY,_T(""));
    SetDlgItemText(hwndDlg,IDC_ZIPFILE,_T(""));
    SetDlgItemText(hwndDlg,IDC_OUTFILE,_T(""));
  }
}

int tempzip_make(HWND hwndDlg, TCHAR *fn)
{
  TCHAR buf[MAX_PATH];
  GetTempPath(MAX_PATH,buf);
  GetTempFileName(buf,_T("z2e"),GetTickCount(),tempzip_path);
  if (!CreateDirectory(tempzip_path,NULL))
  {
    GetTempPath(MAX_PATH,tempzip_path);
    _tcscat(tempzip_path,_T("\\nsi"));
    if (!CreateDirectory(tempzip_path,NULL))
    {
      tempzip_path[0]=0;
      MessageBox(hwndDlg,_T("Error creating temporary directory"),g_errcaption,MB_OK|MB_ICONSTOP);
      return 1;
    }
  }
  FILE *fp=_tfopen(fn,_T("rb"));
  if (fp)
  {
    fseek(fp,0,SEEK_END);
    g_zipfile_size=ftell(fp);
    fclose(fp);
  }
  else g_zipfile_size=0;
  unzFile f;
  f = unzOpen(fn);
  if (!f || unzGoToFirstFile(f) != UNZ_OK)
  {
    if (f) unzClose(f);
    MessageBox(hwndDlg,_T("Error opening ZIP file"),g_errcaption,MB_OK|MB_ICONSTOP);
    return 1;
  }

  int nf=0, nkb=0;
  g_extracting=1;
  do {
    char filenameA[MAX_PATH];
    unz_file_info info;

    // ZREAD uses byte size, not TCHAR length.
    unzGetCurrentFileInfo(f,&info,filenameA,sizeof(filenameA),NULL,0,NULL,0);
    if (IsEncrypted(info))
    {
      if (f) unzClose(f);
      g_extracting = 0;
      MessageBox(hwndDlg,_T("Encrypted ZIP files are not supported!"),g_errcaption,MB_OK|MB_ICONSTOP);
      return 1;
    }

    // was zip created on MS-DOS/Windows?
    if ((info.version & 0xFF00) == 0)
    {
      OemToCharBuffA(filenameA, filenameA, (DWORD)strlen(filenameA));
    }

#ifdef _UNICODE
    TCHAR filename[MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, filenameA, -1, filename, MAX_PATH) == 0)
    {
      if (f) unzClose(f);
      g_extracting = 0;
      MessageBox(hwndDlg,_T("Error converting filename to Unicode"), g_errcaption, MB_OK|MB_ICONSTOP);
      return 1;
    }
#else
    char* filename = filenameA;
#endif

    if (filename[0] &&
        filename[_tcslen(filename)-1] != _T('\\') &&
        filename[_tcslen(filename)-1] != _T('/'))
    {
      TCHAR *pfn=filename;
      while (*pfn)
      {
        if (*pfn == _T('/')) *pfn=_T('\\');
        pfn++;
      }
      pfn=filename;
      if (pfn[1] == _T(':') && pfn[2] == _T('\\')) pfn+=3;
      while (*pfn == _T('\\')) pfn++;

      TCHAR out_filename[1024];
      lstrcpy(out_filename,tempzip_path);
      lstrcat(out_filename,_T("\\"));
      lstrcat(out_filename,pfn);
      if (_tcsstr(pfn,_T("\\")))
      {
        TCHAR buf[1024];
        lstrcpy(buf,out_filename);
        TCHAR *p=buf+_tcslen(buf);
        while (p > buf && *p != _T('\\')) p--;
        *p=0;
        if (buf[0]) doMKDir(buf);
      }

      if (unzOpenCurrentFile(f) == UNZ_OK)
      {
        SendDlgItemMessage(hwndDlg,IDC_ZIPINFO_FILES,LB_ADDSTRING,0,(LPARAM)pfn);
        FILE *fp;
        int l;
        fp = _tfopen(out_filename,_T("wb"));
        if (fp)
        {
          do
          {
            // Jim Park: Local buf, no need to TCHAR
            char buf[1024];
            l=unzReadCurrentFile(f,buf,sizeof(buf));
            if (l > 0)
            {
              if (fwrite(buf,1,l,fp) != (unsigned int)l)
              {
                unzClose(f);
                fclose(fp);
                MessageBox(hwndDlg,_T("Error writing output file(s)"),g_errcaption,MB_OK|MB_ICONSTOP);
                g_extracting=0;
                return 1;
              }
              nkb++;
            }
          } while (l > 0);

          fclose(fp);

          {
            // set file time
            HANDLE hf = CreateFile(out_filename, GENERIC_WRITE, 0, 0, OPEN_ALWAYS, 0, 0);
            if (hf != INVALID_HANDLE_VALUE)
            {
              FILETIME ft, lft;
              DosDateTimeToFileTime(HIWORD(info.dosDate), LOWORD(info.dosDate), &ft);
              LocalFileTimeToFileTime(&ft, &lft);
              SetFileTime(hf, 0, 0, &lft);
              CloseHandle(hf);
            }
          }
        }
        else
        {
          unzClose(f);
          MessageBox(hwndDlg,_T("Error opening output file(s)"),g_errcaption,MB_OK|MB_ICONSTOP);
          g_extracting=0;
          return 1;
        }
        nf++;
        wsprintf(buf,_T("Extracting: %d files, %dKB"),nf,nkb);
        SetDlgItemText(hwndDlg,IDC_ZIPINFO_SUMMARY,buf);
        MSG msg;
        int quit=0;
        while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
        {
          if (msg.message == WM_DESTROY && msg.hwnd == g_hwnd)
          {
            quit++;
            break;
          }
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
        unzCloseCurrentFile(f);
        if (quit) break;
      }
      else
      {
        unzClose(f);
        MessageBox(hwndDlg,_T("Error extracting from ZIP file"),g_errcaption,MB_OK|MB_ICONSTOP);
        g_extracting=0;
        return 1;
      }
    }
  } while (unzGoToNextFile(f) == UNZ_OK);

  g_extracting=0;
  wsprintf(buf,_T("Extracted: %d files, %dKB"),nf,nkb);
  SetDlgItemText(hwndDlg,IDC_ZIPINFO_SUMMARY,buf);
  unzClose(f);
  return 0;
}

const TCHAR *gp_winamp = _T("(WINAMP DIRECTORY)");
const TCHAR *gp_winamp_plugins = _T("(WINAMP PLUG-INS DIRECTORY)");
const TCHAR *gp_winamp_vis = _T("(WINAMP VIS PLUG-INS DIRECTORY)");
const TCHAR *gp_winamp_dsp = _T("(WINAMP DSP PLUG-INS DIRECTORY)");
const TCHAR *gp_winamp_skins = _T("(WINAMP SKINS DIRECTORY)");
const TCHAR *gp_poi = _T("(PATH OF INSTALLER)");


void wnd_printf(const TCHAR *str)
{
  if (!*str) return;
  HWND hLog=GetDlgItem(g_hwnd,IDC_OUTPUTTEXT);
  SendMessage(hLog,EM_SETSEL,0x7fffffff,-1);
  SendMessage(hLog,EM_REPLACESEL,false,(LPARAM)str);
}

void ErrorMessage(const TCHAR *str)  //display detailed error info
{
  LPVOID msg;
  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL,
    GetLastError(),
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
    (LPTSTR) &msg,
    0,
    NULL
    );
  wnd_printf(str);
  wnd_printf(_T(": "));
  wnd_printf((TCHAR*)msg);
  LocalFree(msg);
}

DWORD WINAPI ThreadProc(LPVOID p) // thread that will start & monitor makensis
{
  TCHAR buf[1024];
  char iobuf[1024];           //i/o buffer
  STARTUPINFO si={sizeof(si),};
  SECURITY_ATTRIBUTES sa={sizeof(sa),};
  SECURITY_DESCRIPTOR sd={0,};               //security information for pipes
  PROCESS_INFORMATION pi={0,};
  HANDLE newstdout=0,read_stdout=0;         //pipe handles

  OSVERSIONINFO osv={sizeof(osv)};
  GetVersionEx(&osv);
  if (osv.dwPlatformId == VER_PLATFORM_WIN32_NT)        //initialize security descriptor (Windows NT)
  {
    InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, true, NULL, false);
    sa.lpSecurityDescriptor = &sd;
  }
  else sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = true;         //allow inheritable handles

  if (!CreatePipe(&read_stdout,&newstdout,&sa,0))  //create stdout pipe
  {
    ErrorMessage(_T("CreatePipe"));
    PostMessage(g_hwnd,WM_NOTIFYENDCOMPILE,0,1);
    return 1;
  }

  GetStartupInfo(&si);      //set startupinfo for the spawned process
  /*
    The dwFlags member tells CreateProcess how to make the process.
    STARTF_USESTDHANDLES validates the hStd* members. STARTF_USESHOWWINDOW
    validates the wShowWindow member.
  */
  si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = newstdout;
  si.hStdError = newstdout;     //set the new handles for the child process

  // *******************************************************************
  // If there is a command line in the config file, use it for create process

  //spawn the child process
  if (!CreateProcess(NULL,g_cmdline,NULL,NULL,TRUE,CREATE_NEW_CONSOLE,
      NULL,tempzip_path,&si,&pi))
  {
    ErrorMessage(_T("CreateProcess"));
    wnd_printf(_T("\r\nPlease make sure the path to makensis.exe is correct."));
    CloseHandle(newstdout);
    CloseHandle(read_stdout);
    PostMessage(g_hwnd,WM_NOTIFYENDCOMPILE,0,1);
    return 1;
  }
  CloseHandle(newstdout); // close this handle (duplicated in subprocess) now so we get ERROR_BROKEN_PIPE
  DWORD dwLeft = 0, dwRead = 0;
  while (ReadFile(read_stdout, iobuf+dwLeft, sizeof(iobuf)-dwLeft-1, &dwRead, NULL)) //wait for buffer, or fails with ERROR_BROKEN_PIPE when subprocess exits
  {
    dwRead += dwLeft;
    iobuf[dwRead] = '\0';
#ifdef _UNICODE
    // this tweak is to prevent LogMessage from cutting in the middle of an UTF-8 sequence
    // we print only up to the latest \n of the buffer, and keep the remaining for the next loop
    char* lastLF = strrchr(iobuf,'\n');
    if (lastLF == NULL) lastLF = iobuf+dwRead-1;
    char ch = *++lastLF;
    *lastLF = '\0';
    MultiByteToWideChar(CP_UTF8,0,iobuf,(int)(lastLF+1-iobuf),buf,COUNTOF(buf));
    wnd_printf(buf);
    *lastLF = ch;
    dwLeft = (DWORD)(iobuf+dwRead-lastLF);
    memmove(iobuf, lastLF, dwLeft);
#else
    wnd_printf(iobuf);
#endif
  }
#ifdef _UNICODE
  // because of UTF-8 tweak, in rare case there can be some data remaining
  dwRead += dwLeft;
  iobuf[dwRead] = 0;
  MultiByteToWideChar(CP_UTF8,0,iobuf,dwRead+1,buf,COUNTOF(buf));
  wnd_printf(buf);
#endif
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(read_stdout);


  wsprintf(buf,_T("(source ZIP size was %d bytes)\r\n"),g_zipfile_size);
  wnd_printf(buf);

  PostMessage(g_hwnd,WM_NOTIFYENDCOMPILE,0,0);
  return 0;
}


void makeEXE(HWND hwndDlg)
{
  TCHAR buf[2048];
  GetTempPath(MAX_PATH,buf);
  GetTempFileName(buf,_T("zne"),0,nsifilename);
#ifdef _UNICODE
  FILE *fp=_tfopen(nsifilename,_T("w, ccs=UNICODE")); // generate a Unicode .NSI file BUGBUG: MSVCRT version specific
#else
  FILE *fp=_tfopen(nsifilename,_T("w"));
#endif
  if (!fp)
  {
    MessageBox(hwndDlg,_T("Error writing .NSI file"),g_errcaption,MB_OK|MB_ICONSTOP);
    PostMessage(g_hwnd,WM_NOTIFYENDCOMPILE,0,0);
    return;
  }
  _ftprintf(fp,_T("Unicode %s\n"),IsDlgButtonChecked(hwndDlg,IDC_UNICODE)?_T("true"):_T("false"));
  GetDlgItemText(hwndDlg,IDC_INSTNAME,buf,sizeof(buf));
  _ftprintf(fp,_T("!define ZIP2EXE_NAME `%s`\n"),buf);
  GetDlgItemText(hwndDlg,IDC_OUTFILE,buf,sizeof(buf));
  _ftprintf(fp,_T("!define ZIP2EXE_OUTFILE `%s`\n"),buf);
  LPCTSTR compname = GetCompressorName();
  _ftprintf(fp,_T("!define ZIP2EXE_COMPRESSOR %s\n"), compname);
  _ftprintf(fp,_T("!define ZIP2EXE_COMPRESSOR_%s\n"), compname); // Compatibility define
  if (g_compressor_solid == 1)
    _ftprintf(fp,_T("!define ZIP2EXE_COMPRESSOR_SOLID\n"));
  GetDlgItemText(hwndDlg,IDC_INSTPATH,buf,sizeof(buf));
  int iswinamp=0;
  LPCTSTR iswinampmode=NULL;
  if (!_tcscmp(buf,gp_poi)) lstrcpy(buf,_T("$EXEDIR"));

  if (!_tcscmp(buf,gp_winamp))
  {
    iswinamp=1;
  }
  if (!_tcscmp(buf,gp_winamp_plugins))
  {
    iswinamp=1;
    _ftprintf(fp,_T("!define ZIP2EXE_INSTALLDIR_PLUGINS\n"));
  }
  if (!_tcscmp(buf,gp_winamp_vis))
  {
    iswinamp=1;
    iswinampmode=_T("VisDir");
  }
  if (!_tcscmp(buf,gp_winamp_dsp))
  {
    iswinamp=1;
    iswinampmode=_T("DSPDir");
  }
  if (!_tcscmp(buf,gp_winamp_skins))
  {
    iswinamp=1;
    iswinampmode=_T("SkinDir");
    _ftprintf(fp,_T("!define ZIP2EXE_INSTALLDIR_SKINS\n"));
  }

  if (iswinamp)
  {
    _ftprintf(fp,_T("!define ZIP2EXE_INSTALLDIR_WINAMP\n"));

    if (iswinampmode)
    {
      _ftprintf(fp,_T("!define ZIP2EXE_INSTALLDIR_WINAMPMODE `%s`\n"),iswinampmode);
    }
  }
  else  // set out path to $INSTDIR
  {
    _ftprintf(fp,_T("!define ZIP2EXE_INSTALLDIR `%s`\n"),buf);
  }

  _ftprintf(fp,_T("!include `${NSISDIR}\\Contrib\\zip2exe\\Base.nsh`\n"));
  _ftprintf(fp,_T("!include `${NSISDIR}\\Contrib\\zip2exe\\%s.nsh`\n"),g_mui?_T("Modern"):_T("Classic"));

  _ftprintf(fp,_T("!insertmacro SECTION_BEGIN\n"));
  _ftprintf(fp,_T("File /r `%s\\*.*`\n"),tempzip_path);
  _ftprintf(fp,_T("!insertmacro SECTION_END\n"));

  fclose(fp);

  TCHAR g_makensis_path[MAX_PATH];
  TCHAR *p=g_makensis_path;
  GetModuleFileName(g_hInstance,g_makensis_path,sizeof(g_makensis_path));
  while (*p) p++;
  while (p >= g_makensis_path && *p != _T('\\')) p--;
  _tcscpy(p+1,_T("makensis.exe"));

  WIN32_FIND_DATA fd;
  HANDLE h=FindFirstFile(g_makensis_path,&fd);
  if (h==INVALID_HANDLE_VALUE)
  {
    if ((p-g_makensis_path>4)&&(_totlower(*(p-1))==_T('n'))&&(_totlower(*(p-2))==_T('i'))&&(_totlower(*(p-3))==_T('b'))&&(*(p-4)==_T('\\')))
    {
      p -= 4;
      _tcscpy(p+1,_T("makensis.exe"));
      h=FindFirstFile(g_makensis_path,&fd);
      if (h==INVALID_HANDLE_VALUE)
      {
        MessageBox(hwndDlg,_T("Error finding makensis.exe."),g_errcaption,MB_OK|MB_ICONSTOP);
        PostMessage(g_hwnd,WM_NOTIFYENDCOMPILE,0,0);
        return;
      }
    }
  }
  if (h!=INVALID_HANDLE_VALUE) FindClose(h);



  wsprintf(g_cmdline,_T("\"%s\" %s \"%s\""),g_makensis_path,g_options,nsifilename);
  DWORD id;
  g_hThread=CreateThread(NULL,0,ThreadProc,0,0,&id);

}

int SetZip(HWND hwndDlg, TCHAR *path)
{
  TCHAR buf2[1024];
  lstrcpy(buf2,path);
  tempzip_cleanup(hwndDlg,1);
  SetDlgItemText(hwndDlg,IDC_ZIPFILE,path);
  TCHAR *t=path+lstrlen(path);
  while (t > path && *t != _T('\\') && *t != _T('.')) t--;
  {
    TCHAR *p=t;
    while (p >= path && *p != _T('\\')) p--;
    p++;
    *t=0;
    SetDlgItemText(hwndDlg,IDC_INSTNAME,p[0]?p:_T("Stuff"));
  }
  _tcscpy(t,_T(".exe"));
  SetDlgItemText(hwndDlg,IDC_OUTFILE,path);
  if (tempzip_make(hwndDlg,buf2))
  {
    tempzip_cleanup(hwndDlg,1);
    return 1;
  }
  else
  {
    EnableWindow(GetDlgItem(hwndDlg,IDOK),1);
    return 0;
  }
}

INT_PTR CALLBACK DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int ids[]={IDC_INFO,IDC_NSISICON,IDC_SZIPFRAME,IDC_BROWSE,IDC_ZIPFILE,IDC_ZIPINFO_SUMMARY,IDC_ZIPINFO_FILES,IDC_OFRAME,IDC_INAMEST,
                        IDC_INSTNAME,IDC_INSTPATH,IDC_OEFST,IDC_OUTFILE,IDC_BROWSE2,IDC_COMPRESSOR,IDC_ZLIB,IDC_BZIP2,IDC_ZSTD,IDC_LZMA,IDC_SOLID,IDC_INTERFACE,IDC_MODERNUI,IDC_CLASSICUI,IDC_UNICODE};
  static HICON hIcon=0;
  static HFONT hFont=0;
  switch (uMsg)
  {
    case WM_SIZE:
      if (wParam == SIZE_MAXIMIZED) ShowWindow(hwndDlg, SW_SHOWNOACTIVATE); // Disallow STARTF_USESHOWWINDOW+SW_MAXIMIZE
    break;
    case WM_INITDIALOG:
      g_hwnd=hwndDlg;
      CheckDlgButton(hwndDlg,IDC_LZMA,BST_CHECKED);
      CheckDlgButton(hwndDlg,IDC_MODERNUI,BST_CHECKED);
      CheckDlgButton(hwndDlg,IDC_UNICODE,BST_CHECKED);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_poi);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$TEMP"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$SYSDIR"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$WINDIR"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$DESKTOP"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$DESKTOP\\YourNameHere"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$PROGRAMFILES\\YourNameHere"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$STARTMENU"));
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)_T("$SMPROGRAMS"));

      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_winamp);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_winamp_plugins);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_winamp_vis);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_winamp_dsp);
      SendDlgItemMessage(hwndDlg,IDC_INSTPATH,CB_ADDSTRING,0,(LPARAM)gp_winamp_skins);

      SetDlgItemText(hwndDlg,IDC_INSTPATH,gp_poi);

      hIcon=LoadIcon(g_hInstance,MAKEINTRESOURCE(IDI_ICON1));
      SetClassLongPtr(hwndDlg,GCLP_HICON,(LONG_PTR)hIcon);

      hFont=CreateFont(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
              OUT_CHARACTER_PRECIS,
              CLIP_DEFAULT_PRECIS,
              DEFAULT_QUALITY,FIXED_PITCH|FF_DONTCARE,_T("Courier New"));
      SendDlgItemMessage(hwndDlg,IDC_OUTPUTTEXT,WM_SETFONT,(WPARAM)hFont,0);

      DragAcceptFiles(hwndDlg,TRUE);

      {
        LPTSTR args = PathGetArgs(GetCommandLine());
        TCHAR buf[100];
        lstrcpyn(buf, args, sizeof("/GENERATE"));
        BOOL generate = !_tcsicmp(buf, _T("/GENERATE"));
        args += generate ? sizeof("/GENERATE") : 0;
        while (*args <= ' ' && *args) ++args;
        PathUnquoteSpaces(args);
        if (*args && !SetZip(hwndDlg, args) && generate)
            PostMessage(hwndDlg, WM_COMMAND, IDOK, 0);
      }
    return 1;
    case WM_NCDESTROY:
      DeleteObject(hIcon); hIcon=0;
      DeleteObject(hFont); hFont=0;
    break;
    case WM_CLOSE:
      if (!g_hThread)
      {
        tempzip_cleanup(hwndDlg,0);
        EndDialog(hwndDlg,1);
      }
    break;
    case WM_NOTIFYENDCOMPILE:

      if (g_hThread)
      {
        if (!lParam) ShowWindow(GetDlgItem(hwndDlg,IDC_TEST),SW_SHOWNA);
        CloseHandle(g_hThread);
        g_hThread=0;
      }
      g_made=true;
      ShowWindow(GetDlgItem(hwndDlg,IDC_BACK),SW_SHOWNA);
      EnableWindow(GetDlgItem(hwndDlg,IDOK),1);
      if (nsifilename[0]) DeleteFile(nsifilename);
      nsifilename[0]=0;
    break;
    case WM_DROPFILES:
    {
      TCHAR dropped_file[MAX_PATH]=_T("");
      if (DragQueryFile((HDROP)wParam,(UINT)-1,NULL,0)==1)
      {
        DragQueryFile((HDROP)wParam,0,dropped_file,MAX_PATH);
        if (lstrlen(dropped_file)>0)
        {
          SetZip(hwndDlg,dropped_file);
        }
      }
      else
      {
        MessageBox(hwndDlg,_T("Dropping more than one zip file at a time is not supported"),g_errcaption,MB_OK|MB_ICONSTOP);
      }
      DragFinish((HDROP)wParam);
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_BROWSE:
          if (!g_extracting) {
            OPENFILENAME l={sizeof(l),};
            TCHAR buf[1024];
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = _T("ZIP Files\0*.zip\0All Files\0*.*\0");
            l.lpstrFile = buf;
            l.nMaxFile = 1023;
            l.lpstrTitle = _T("Open ZIP File");
            l.lpstrDefExt = _T("zip");
            l.lpstrInitialDir = NULL;
            l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER|OFN_PATHMUSTEXIST;
            buf[0]=0;
            if (GetOpenFileName(&l))
            {
              SetZip(hwndDlg,buf);
            }
          }
        break;
        case IDC_BROWSE2:
          {
            OPENFILENAME l={sizeof(l),};
            TCHAR buf[1024];
            l.hwndOwner = hwndDlg;
            l.lpstrFilter = _T("Executables\0*.exe\0All Files\0*.*\0");
            l.lpstrFile = buf;
            l.nMaxFile = 1023;
            l.lpstrTitle = _T("Select Output EXE File");
            l.lpstrDefExt = _T("exe");
            l.lpstrInitialDir = NULL;
            l.Flags = OFN_HIDEREADONLY|OFN_EXPLORER;
            GetDlgItemText(hwndDlg,IDC_OUTFILE,buf,sizeof(buf));
            if (GetSaveFileName(&l))
            {
              SetDlgItemText(hwndDlg,IDC_OUTFILE,buf);
            }
          }
        break;
        case IDC_BACK:
          if (!g_hThread)
          {
            g_made=false;
            ShowWindow(GetDlgItem(hwndDlg,IDC_BACK),SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg,IDC_TEST),SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg,IDC_OUTPUTTEXT),SW_HIDE);
            {
              for (size_t x = 0; x < COUNTOF(ids); x ++)
                ShowWindow(GetDlgItem(hwndDlg,ids[x]),SW_SHOWNA);
              SetDlgItemText(hwndDlg,IDOK,_T("&Generate"));
              EnableWindow(GetDlgItem(hwndDlg,IDOK),1);
            }
          }
        break;
        case IDC_TEST:
          if (!g_hThread) {
            TCHAR buf[1024];
            GetDlgItemText(hwndDlg,IDC_OUTFILE,buf,COUNTOF(buf));
            ShellExecute(hwndDlg,_T("open"),buf,_T(""),_T(""),SW_SHOW);
          }
        break;
        case IDOK:
          if (!g_hThread)
          {
            if (!g_made)
            {
              g_compressor = GetCheckedCompressor(hwndDlg);
              g_compressor_solid = IsDlgButtonChecked(hwndDlg,IDC_SOLID) != false;
              g_mui=!IsDlgButtonChecked(hwndDlg,IDC_CLASSICUI);
              SetDlgItemText(g_hwnd, IDC_OUTPUTTEXT, _T(""));
              for (size_t x = 0; x < COUNTOF(ids); x ++)
                ShowWindow(GetDlgItem(hwndDlg,ids[x]),SW_HIDE);
              ShowWindow(GetDlgItem(hwndDlg,IDC_OUTPUTTEXT),SW_SHOWNA);
              SetDlgItemText(hwndDlg,IDOK,_T("&Close"));
              EnableWindow(GetDlgItem(hwndDlg,IDOK),0);

              makeEXE(hwndDlg);
            }
            else
            {
              tempzip_cleanup(hwndDlg,0);
              EndDialog(hwndDlg,0);
            }
          }
        break;
      }
    break;
  }
  return FALSE;
}
