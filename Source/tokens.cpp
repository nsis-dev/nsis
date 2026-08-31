/*
 * tokens.cpp
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
 * Unicode support by Jim Park -- 08/10/2007
 */

#include "Platform.h"
#include <stdlib.h>
#include <stdio.h>

#include "build.h"
#include "tokens.h"

typedef struct 
{
  int id;
  const TCHAR *name;
  int num_parms; // minimum number of parameters
  int opt_parms; // optional parameters, usually 0, can be -1 for unlimited.
  const TCHAR *usage_str;
  int placement; // where the token can be placed
} tokenType;


static tokenType tokenlist[TOK__LAST] =
{
{TOK_ABORT,_T("Abort"),0,1,_T("[message]"),TP_CODE},
{TOK_ADDBRANDINGIMAGE,_T("AddBrandingImage"),2,1,_T("(top|left|bottom|right) (height|width) [padding]"),TP_GLOBAL},
{TOK_ADDSIZE,_T("AddSize"),1,0,_T("size_to_add_to_section_in_kb"),TP_SEC},
{TOK_AUTOCLOSE,_T("AutoCloseWindow"),1,0,_T("(false|true)"),TP_GLOBAL},
{TOK_BGFONT,_T("BGFont"),0,6,_T("[font_face [height [weight] [/ITALIC] [/UNDERLINE] [/STRIKE]]]"),TP_GLOBAL},
{TOK_BGGRADIENT,_T("BGGradient"),0,3,_T("(off | [top_color [bottom_color [text_color]]])"),TP_GLOBAL},
{TOK_BRANDINGTEXT,_T("BrandingText"),1,1,_T("[/TRIM(LEFT|RIGHT|CENTER)] installer_text"),TP_GLOBAL},
{TOK_BRINGTOFRONT,_T("BringToFront"),0,0,_T(""),TP_CODE},
{TOK_CALL,_T("Call"),1,0,_T("function_name | [:label_name]"),TP_CODE},
{TOK_CALLINSTDLL,_T("CallInstDLL"),2,1,_T("dll_path_on_target.dll function"),TP_CODE},
{TOK_CAPTION,_T("Caption"),1,0,_T("installer_caption"),TP_GLOBAL|TP_PAGEEX},
{TOK_CHANGEUI,_T("ChangeUI"),2,0,_T("(all|dlg_id) ui_file.exe"),TP_GLOBAL},
{TOK_CLEARERRORS,_T("ClearErrors"),0,0,_T(""),TP_CODE},
{TOK_COMPTEXT,_T("ComponentText"),0,3,_T("[component_page_description] [component_subtext1] [component_subtext2]"),TP_PG},
{TOK_GETDLLVERSION,_T("GetDLLVersion"),3,1,_T("[/ProductVersion] filename $(user_var: high output) $(user_var: low output)"),TP_CODE},
{TOK_GETDLLVERSIONLOCAL,_T("GetDLLVersionLocal"),3,1,_T("localfilename $(user_var: high output) $(user_var: low output)"),TP_CODE},
{TOK_GETFILETIME,_T("GetFileTime"),3,0,_T("file $(user_var: high output) $(user_var: low output)"),TP_CODE},
{TOK_GETFILETIMELOCAL,_T("GetFileTimeLocal"),3,0,_T("localfile $(user_var: high output) $(user_var: low output)"),TP_CODE},
{TOK_COPYFILES,_T("CopyFiles"),2,3,_T("[/SILENT] [/FILESONLY] source_path destination_path [total_size_in_kb]"),TP_CODE},
{TOK_CRCCHECK,_T("CRCCheck"),1,0,_T("(on|force|off)"),TP_GLOBAL},
{TOK_CREATEDIR,_T("CreateDirectory"),1,0,_T("directory_name"),TP_CODE},
{TOK_CREATEFONT,_T("CreateFont"),2,5,_T("$(user_var: handle output) face_name [height weight /ITALIC /UNDERLINE /STRIKE]"),TP_CODE},
{TOK_CREATESHORTCUT,_T("CreateShortcut"),2,7,_T("[/NoWorkingDir] shortcut_name.lnk shortcut_target [parameters [icon_file [icon_index [showmode [hotkey [comment]]]]]]\n    showmode=(SW_SHOWNORMAL|SW_SHOWMAXIMIZED|SW_SHOWMINIMIZED)\n    hotkey=(ALT|CONTROL|EXT|SHIFT)|(F1-F24|A-Z)"),TP_CODE},
{TOK_DBOPTIMIZE,_T("SetDatablockOptimize"),1,0,_T("(off|on)"),TP_ALL},
{TOK_DELETEINISEC,_T("DeleteINISec"),2,0,_T("ini_file section_name"),TP_CODE},
{TOK_DELETEINISTR,_T("DeleteINIStr"),3,0,_T("ini_file section_name entry_name"),TP_CODE},
{TOK_DELETEREGKEY,_T("DeleteRegKey"),2,-1,_T("[/ifempty | /ifnosubkeys | /ifnovalues] root_key subkey\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_DELETEREGVALUE,_T("DeleteRegValue"),3,0,_T("root_key subkey entry_name\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_DELETE,_T("Delete"),1,1,_T("[/REBOOTOK] filespec"),TP_CODE},
{TOK_DETAILPRINT,_T("DetailPrint"),1,0,_T("message"),TP_CODE},
{TOK_DIRTEXT,_T("DirText"),0,4,_T("[directory_page_description] [directory_page_subtext] [browse_button_text] [browse_dlg_text]"),TP_PG},
//{TOK_DIRSHOW,_T("DirShow"),1,0,_T("(show|hide)"),TP_PG},
{TOK_DIRSHOW,_T("DirShow"),0,0,_T("doesn't currently work"),TP_ALL},
{TOK_DIRVAR,_T("DirVar"),1,0,_T("$(user_var: dir in/out)"),TP_PAGEEX},
{TOK_DIRVERIFY,_T("DirVerify"),1,0,_T("auto|leave"),TP_PAGEEX},
{TOK_GETINSTDIRERROR,_T("GetInstDirError"),1,0,_T("$(user_var: error output)"),TP_CODE},
{TOK_ROOTDIRINST,_T("AllowRootDirInstall"),1,0,_T("(true|false)"),TP_GLOBAL},
{TOK_CHECKBITMAP,_T("CheckBitmap"),1,0,_T("local_bitmap.bmp"),TP_GLOBAL},
{TOK_ENABLEWINDOW,_T("EnableWindow"),2,0,_T("hwnd state(1|0)"),TP_CODE},
{TOK_ENUMREGKEY,_T("EnumRegKey"),4,0,_T("$(user_var: output) rootkey subkey index\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_ENUMREGVAL,_T("EnumRegValue"),4,0,_T("$(user_var: output) rootkey subkey index\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_EXCH,_T("Exch"),0,1,_T("[$(user_var)] | [stack_item_index]"),TP_CODE},
{TOK_EXEC,_T("Exec"),1,0,_T("command_line"),TP_CODE},
{TOK_EXECWAIT,_T("ExecWait"),1,1,_T("command_line [$(user_var: return value)]"),TP_CODE},
{TOK_EXECSHELL,_T("ExecShell"),2,11,_T("[flags] verb file [parameters [showmode]]\n    verb=(open|print)\n    showmode=(SW_SHOWNORMAL|SW_SHOWMAXIMIZED|SW_SHOWMINIMIZED|SW_HIDE|SW_SHOW)"),TP_CODE},
{TOK_EXECSHELLWAIT,_T("ExecShellWait"),2,11,_T("[flags] verb file [parameters [showmode]]"),TP_CODE},
{TOK_EXPANDENVSTRS,_T("ExpandEnvStrings"),2,0,_T("$(user_var: output) string"),TP_CODE},
{TOK_FINDWINDOW,_T("FindWindow"),2,3,_T("$(user_var: handle output) WindowClass [WindowTitle] [Window_Parent] [Child_After]"),TP_CODE},
{TOK_FINDCLOSE,_T("FindClose"),1,0,_T("$(user_var: handle input)"),TP_CODE},
{TOK_FINDFIRST,_T("FindFirst"),3,0,_T("$(user_var: handle output) $(user_var: filename output) filespec"),TP_CODE},
{TOK_FINDNEXT,_T("FindNext"),2,0,_T("$(user_var: handle input) $(user_var: filename output)"),TP_CODE},
{TOK_FILE,_T("File"),1,-1,_T("[/nonfatal] [/a] ([/r] [/x filespec [...]] filespec [...] |\n   /oname=outfile one_file_only)"),TP_CODE},
{TOK_FILEBUFSIZE,_T("FileBufSize"),1,0,_T("buf_size_mb"),TP_ALL},
{TOK_FLUSHINI,_T("FlushINI"),1,0,_T("ini_file"),TP_CODE},
{TOK_RESERVEFILE,_T("ReserveFile"),1,-1,_T("[/nonfatal] [/r] [/x filespec [...]] file [file...] | [/nonfatal] /plugin file.dll"),TP_ALL},
{TOK_FILECLOSE,_T("FileClose"),1,0,_T("$(user_var: handle input)"),TP_CODE},
{TOK_FILEERRORTEXT,_T("FileErrorText"),0,2,_T("[text (can contain $0)] [text without ignore (can contain $0)]"),TP_GLOBAL},
{TOK_FILEOPEN,_T("FileOpen"),3,0,_T("$(user_var: handle output) filename openmode\n   openmode=r|w|a"),TP_CODE},
{TOK_FILEREAD,_T("FileRead"),2,1,_T("$(user_var: handle input) $(user_var: text output) [maxlen]"),TP_CODE},
{TOK_FILEWRITE,_T("FileWrite"),2,0,_T("$(user_var: handle input) text"),TP_CODE},
{TOK_FILEREADBYTE,_T("FileReadByte"),2,0,_T("$(user_var: handle input) $(user_var: bytevalue output)"),TP_CODE},
{TOK_FILEWRITEBYTE,_T("FileWriteByte"),2,0,_T("$(user_var: handle input) bytevalue"),TP_CODE},
#ifdef _UNICODE
{TOK_FILEREADUTF16LE,_T("FileReadUTF16LE"),2,1,_T("$(user_var: handle input) $(user_var: text output) [maxlen]"),TP_CODE},
{TOK_FILEWRITEUTF16LE,_T("FileWriteUTF16LE"),2,1,_T("[/BOM] $(user_var: handle input) text"),TP_CODE},
{TOK_FILEREADWORD,_T("FileReadWord"),2,0,_T("$(user_var: handle input) $(user_var: wordvalue output)"),TP_CODE},
{TOK_FILEWRITEWORD,_T("FileWriteWord"),2,0,_T("$(user_var: handle input) wordvalue"),TP_CODE},
#endif
{TOK_FILESEEK,_T("FileSeek"),2,2,_T("$(user_var: handle input) offset [mode] [$(user_var: new position output)]\n    mode=SET|CUR|END"),TP_CODE},
{TOK_FUNCTION,_T("Function"),1,0,_T("function_name"),TP_GLOBAL},
{TOK_FUNCTIONEND,_T("FunctionEnd"),0,0,_T(""),TP_FUNC},
{TOK_GETDLGITEM,_T("GetDlgItem"),3,0,_T("$(user_var: handle output) dialog item_id"),TP_CODE},
{TOK_GETFULLPATHNAME,_T("GetFullPathName"),2,1,_T("[/SHORT] $(user_var: result) path_or_file"),TP_CODE},
{TOK_GETTEMPFILENAME,_T("GetTempFileName"),1,1,_T("$(user_var: name output) [base_dir]"),TP_CODE},
{TOK_GETKNOWNFOLDERPATH,_T("GetKnownFolderPath"),2,0,_T("$(user_var: result) knownfolderid"),TP_CODE},
{TOK_GETWINVER,_T("GetWinVer"),2,0,_T("$(user_var: result) field\n    field=MAJOR|MINOR|BUILD|SERVICEPACK"),TP_CODE},
{TOK_READMEMORY,_T("ReadMemory"),3,0,_T("$(user_var: result) address size"),TP_CODE},
{TOK_HIDEWINDOW,_T("HideWindow"),0,0,_T(""),TP_CODE},
{TOK_ICON,_T("Icon"),1,0,_T("local_icon.ico"),TP_GLOBAL},
{TOK_IFABORT,_T("IfAbort"),1,1,_T("label_to_goto_if_abort [label_to_goto_if_no_abort]"),TP_CODE},
{TOK_IFERRORS,_T("IfErrors"),1,1,_T("label_to_goto_if_errors [label_to_goto_if_no_errors]"),TP_CODE},
{TOK_IFFILEEXISTS,_T("IfFileExists"),2,1,_T("filename label_to_goto_if_file_exists [label_to_goto_otherwise]"),TP_CODE},
{TOK_IFREBOOTFLAG,_T("IfRebootFlag"),1,1,_T("jump_if_set [jump_if_not_set]"),TP_CODE},
{TOK_IFSILENT,_T("IfSilent"),1,1,_T("jump_if_silent [jump_if_not_silent]"),TP_CODE},
{TOK_IFRTLLANG,_T("IfRtlLanguage"),1,1,_T("goto_true [goto_false]"),TP_CODE},
{TOK_INSTALLDIRREGKEY,_T("InstallDirRegKey"),3,0,_T("root_key subkey entry_name\n    root_key=(HKCR|HKLM|HKCU|HKU|HKCC|HKDD|HKPD)"),TP_GLOBAL},
{TOK_INSTCOLORS,_T("InstallColors"),1,1,_T("(/windows | (foreground_color background_color))"),TP_GLOBAL},
{TOK_INSTDIR,_T("InstallDir"),1,0,_T("default_install_directory"),TP_GLOBAL},
{TOK_INSTPROGRESSFLAGS,_T("InstProgressFlags"),0,-1,_T("[flag [...]]\n    flag={smooth|colored}"),TP_GLOBAL},
{TOK_INSTTYPE,_T("InstType"),1,1,_T("[un.]install_type_name [index_output] | /[UNINST]NOCUSTOM | /CUSTOMSTRING=str | /[UNINST]COMPONENTSONLYONCUSTOM"),TP_GLOBAL},
{TOK_INTOP,_T("IntOp"),3,1,_T("$(user_var: result) val1 OP [val2]\n    OP=(+ - * / % | & ^ ~ ! || && << >> >>>)"),TP_CODE},
{TOK_INTPTROP,_T("IntPtrOp"),3,1,_T("$(user_var: result) val1 OP [val2]"),TP_CODE},
{TOK_INTCMP,_T("IntCmp"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INTCMPU,_T("IntCmpU"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INT64CMP,_T("Int64Cmp"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INT64CMPU,_T("Int64CmpU"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INTPTRCMP,_T("IntPtrCmp"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INTPTRCMPU,_T("IntPtrCmpU"),3,2,_T("val1 val2 jump_if_equal [jump_if_val1_less] [jump_if_val1_more]"),TP_CODE},
{TOK_INTFMT,_T("IntFmt"),3,0,_T("$(user_var: output) format_string input"),TP_CODE},
{TOK_INT64FMT,_T("Int64Fmt"),3,0,_T("$(user_var: output) format_string input"),TP_CODE},
{TOK_ISWINDOW,_T("IsWindow"),2,1,_T("hwnd jump_if_window [jump_if_not_window]"),TP_CODE},
{TOK_GOTO,_T("Goto"),1,0,_T("label"),TP_CODE},
{TOK_LANGSTRING,_T("LangString"),3,0,_T("[un.]name lang_id|0 string"),TP_GLOBAL},
{TOK_LANGSTRINGUP,_T("LangStringUP"),0,0,_T("obsolete, use LangString."),TP_ALL},
{TOK_LICENSEDATA,_T("LicenseData"),1,0,_T("local_file_that_has_license_text | license_lang_string"),TP_PG},
{TOK_LICENSEFORCESELECTION,_T("LicenseForceSelection"),1,2,_T("(checkbox [accept_text] | radiobuttons [accept_text] [decline_text] | off)"),TP_PG},
{TOK_LICENSELANGSTRING,_T("LicenseLangString"),3,0,_T("name lang_id|0 license_path"),TP_GLOBAL},
{TOK_LICENSETEXT,_T("LicenseText"),1,1,_T("license_page_description [license_button_text]"),TP_PG},
{TOK_LICENSEBKCOLOR,_T("LicenseBkColor"),1,0,_T("background_color"),TP_GLOBAL},
{TOK_LOADNLF,_T("LoadLanguageFile"),1,0,_T("language.nlf"),TP_GLOBAL},
{TOK_LOGSET,_T("LogSet"),1,0,_T("on|off"),TP_CODE},
{TOK_LOGTEXT,_T("LogText"),1,0,_T("text"),TP_CODE},
{TOK_MESSAGEBOX,_T("MessageBox"),2,6,_T("mode messagebox_text [/SD return] [return_check label_to_goto_if_equal [return_check2 label2]]\n    mode=modeflag[|modeflag[|modeflag[...]]]\n    ")
                                _T("modeflag=(MB_ABORTRETRYIGNORE|MB_OK|MB_OKCANCEL|MB_RETRYCANCEL|MB_YESNO|MB_YESNOCANCEL|MB_ICONEXCLAMATION|MB_ICONINFORMATION|MB_ICONQUESTION|MB_ICONSTOP|MB_USERICON|MB_TOPMOST|MB_SETFOREGROUND|MB_RIGHT)"),TP_CODE},
{TOK_NOP,_T("Nop"),0,0,_T(""),TP_CODE},
{TOK_NAME,_T("Name"),1,1,_T("installer_name [installer_name_doubled_ampersands]"),TP_GLOBAL},
{TOK_OUTFILE,_T("OutFile"),1,0,_T("install_output.exe"),TP_GLOBAL},
#ifdef NSIS_SUPPORT_CODECALLBACKS
{TOK_PAGE,_T("Page"),1,4,_T("((custom [creator_function] [leave_function] [caption]) | ((license|components|directory|instfiles|uninstConfirm) [pre_function] [show_function] [leave_function])) [/ENABLECANCEL]"),TP_GLOBAL},
#else
{TOK_PAGE,_T("Page"),1,1,_T("license|components|directory|instfiles|uninstConfirm [/ENABLECANCEL]"),TP_GLOBAL},
#endif
{TOK_PAGECALLBACKS,_T("PageCallbacks"),0,3,_T("([creator_function] [leave_function]) | ([pre_function] [show_function] [leave_function])"),TP_PAGEEX},
{TOK_PAGEEX,_T("PageEx"),1,0,_T("[un.](custom|uninstConfirm|license|components|directory|instfiles)"),TP_GLOBAL},
{TOK_PAGEEXEND,_T("PageExEnd"),0,0,_T(""),TP_PAGEEX},
{TOK_POP,_T("Pop"),1,0,_T("$(user_var: output)"),TP_CODE},
{TOK_PUSH,_T("Push"),1,0,_T("string"),TP_CODE},
{TOK_QUIT,_T("Quit"),0,0,_T(""),TP_CODE},
{TOK_READINISTR,_T("ReadINIStr"),4,0,_T("$(user_var: output) ini_file section entry_name"),TP_CODE},
{TOK_READREGDWORD,_T("ReadRegDWORD"),4,0,_T("$(user_var: output) rootkey subkey entry\n   root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_READREGSTR,_T("ReadRegStr"),4,0,_T("$(user_var: output) rootkey subkey entry\n   root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_READENVSTR,_T("ReadEnvStr"),2,0,_T("$(user_var: output) name"),TP_CODE},
{TOK_REBOOT,_T("Reboot"),0,0,_T(""),TP_CODE},
{TOK_REGDLL,_T("RegDLL"),1,1,_T("dll_path_on_target.dll [entrypoint_symbol]"),TP_CODE},
{TOK_RENAME,_T("Rename"),2,1,_T("[/REBOOTOK] source_file destination_file"),TP_CODE},
{TOK_RET,_T("Return"),0,0,_T(""),TP_CODE},
{TOK_RMDIR,_T("RMDir"),1,2,_T("[/r] [/REBOOTOK] directory_name"),TP_CODE},
{TOK_SECTION,_T("Section"),0,3,_T("[/o] [-][un.][section_name] [section_index_output]"),TP_GLOBAL},
{TOK_SECTIONEND,_T("SectionEnd"),0,0,_T(""),TP_SEC},
{TOK_SECTIONINSTTYPE,_T("SectionInstType"),1,-1,_T("InstTypeIdx [InstTypeIdx [...]]"),TP_SEC},
{TOK_SECTIONIN,_T("SectionIn"),1,-1,_T("InstTypeIdx [InstTypeIdx [...]]"),TP_SEC},
{TOK_SUBSECTION,_T("SubSection"),1,2,_T("deprecated - use SectionGroup"),TP_GLOBAL},
{TOK_SECTIONGROUP,_T("SectionGroup"),1,2,_T("[/e] [un.]section_group_name [section_index_output]"),TP_GLOBAL},
{TOK_SUBSECTIONEND,_T("SubSectionEnd"),0,0,_T("deprecated - use SectionGroupEnd"),TP_GLOBAL},
{TOK_SECTIONGROUPEND,_T("SectionGroupEnd"),0,0,_T(""),TP_GLOBAL},
{TOK_SEARCHPATH,_T("SearchPath"),2,0,_T("$(user_var: result) filename"),TP_CODE},
{TOK_SECTIONSETFLAGS,_T("SectionSetFlags"),2,0,_T("section_index flags"),TP_CODE},
{TOK_SECTIONGETFLAGS,_T("SectionGetFlags"),2,0,_T("section_index $(user_var: output flags)"),TP_CODE},
{TOK_SECTIONSETINSTTYPES,_T("SectionSetInstTypes"),2,0,_T("section_index inst_types"),TP_CODE},
{TOK_SECTIONGETINSTTYPES,_T("SectionGetInstTypes"),2,0,_T("section_index $(user_var: output inst_types)"),TP_CODE},
{TOK_SECTIONGETTEXT,_T("SectionGetText"),2,0,_T("section_index $(user_var: output text)"),TP_CODE},
{TOK_SECTIONSETTEXT,_T("SectionSetText"),2,0,_T("section_index text_string"),TP_CODE},
{TOK_SECTIONGETSIZE,_T("SectionGetSize"),2,0,_T("section_index $(user_var: output size)"),TP_CODE},
{TOK_SECTIONSETSIZE,_T("SectionSetSize"),2,0,_T("section_index new_size"),TP_CODE},
{TOK_GETCURINSTTYPE,_T("GetCurInstType"),1,0,_T("$(user_var: output inst_type_idx)"),TP_CODE},
{TOK_SETCURINSTTYPE,_T("SetCurInstType"),1,0,_T("inst_type_idx"),TP_CODE},
{TOK_INSTTYPESETTEXT,_T("InstTypeSetText"),2,0,_T("insttype_index text"),TP_CODE},
{TOK_INSTTYPEGETTEXT,_T("InstTypeGetText"),2,0,_T("insttype_index $(user_var: output text)"),TP_CODE},
{TOK_SENDMESSAGE,_T("SendMessage"),4,2,_T("hwnd message [wparam|STR:wParam] [lparam|STR:lParam] [$(user_var: return value)] [/TIMEOUT=X]"),TP_CODE},
{TOK_SETAUTOCLOSE,_T("SetAutoClose"),1,0,_T("(false|true)"),TP_CODE},
{TOK_SETCTLCOLORS,_T("SetCtlColors"),2,2,_T("hwnd [/BRANDING] [text_color] [transparent|bg_color]"),TP_CODE},
{TOK_SETBRANDINGIMAGE,_T("SetBrandingImage"),1,2,_T("[/IMGID=image_item_id_in_dialog] [/RESIZETOFIT] bitmap.bmp"),TP_CODE},
{TOK_LOADANDSETIMAGE,_T("LoadAndSetImage"),4,6,_T("[/EXERESOURCE] [/STRINGID] [/RESIZETOFIT[WIDTH|HEIGHT]] ctrl imagetype lrflags imageid [$(user_var: imagehandle)]"),TP_CODE},
{TOK_SETCOMPRESS,_T("SetCompress"),1,0,_T("(off|auto|force)"),TP_ALL},
{TOK_SETCOMPRESSOR,_T("SetCompressor"),1,2,_T("[/FINAL] [/SOLID] (zlib|bzip2|lzma)"),TP_GLOBAL},
{TOK_SETCOMPRESSORDICTSIZE,_T("SetCompressorDictSize"),1,0,_T("dict_size_mb"),TP_ALL},
{TOK_SETCOMPRESSIONLEVEL,_T("SetCompressionLevel"),1,0,_T("level_0-9"),TP_ALL},
{TOK_SETDATESAVE,_T("SetDateSave"),1,0,_T("(off|on)"),TP_ALL},
{TOK_SETDETAILSVIEW,_T("SetDetailsView"),1,0,_T("(hide|show)"),TP_CODE},
{TOK_SETDETAILSPRINT,_T("SetDetailsPrint"),1,0,_T("(none|listonly|textonly|both|lastused)"),TP_CODE},
{TOK_SETERRORS,_T("SetErrors"),0,0,_T(""),TP_CODE},
{TOK_SETERRORLEVEL,_T("SetErrorLevel"),1,0,_T("error_level"),TP_CODE},
{TOK_GETERRORLEVEL,_T("GetErrorLevel"),1,0,_T("$(user_var: output)"),TP_CODE},
{TOK_SETFILEATTRIBUTES,_T("SetFileAttributes"),2,0,_T("file attribute[|attribute[...]]\n    attribute=(NORMAL|ARCHIVE|HIDDEN|OFFLINE|READONLY|SYSTEM|TEMPORARY|0)"),TP_CODE},
{TOK_SETFONT,_T("SetFont"),2,1,_T("[/LANG=lang_id] font_face_name font_size"),TP_GLOBAL},
{TOK_SETOUTPATH,_T("SetOutPath"),1,0,_T("output_path"),TP_CODE},
{TOK_SETOVERWRITE,_T("SetOverwrite"),1,0,_T("on|off|try|ifnewer|ifdiff"),TP_ALL},
{TOK_SETPLUGINUNLOAD,_T("SetPluginUnload"),1,0,_T("deprecated - plug-ins should handle this on their own"),TP_ALL},
{TOK_SETREBOOTFLAG,_T("SetRebootFlag"),1,0,_T("true|false"),TP_CODE},
{TOK_GETREGVIEW,_T("GetRegView"),1,0,_T("$(user_var: output)"),TP_CODE},
{TOK_SETREGVIEW,_T("SetRegView"),1,0,_T("32|64|default|lastused"),TP_CODE},
{TOK_IFALTREGVIEW,_T("IfAltRegView"),1,1,_T("goto_true [goto_false]"),TP_CODE},
{TOK_GETSHELLVARCONTEXT,_T("GetShellVarContext"),1,0,_T("$(user_var: output)"),TP_CODE},
{TOK_SETSHELLVARCONTEXT,_T("SetShellVarContext"),1,0,_T("all|current|lastused"),TP_CODE},
{TOK_IFSHELLVARCONTEXTALL,_T("IfShellVarContextAll"),1,1,_T("goto_true [goto_false]"),TP_CODE},
{TOK_SETSILENT,_T("SetSilent"),1,0,_T("silent|normal"),TP_CODE},
{TOK_SHOWDETAILS,_T("ShowInstDetails"),1,0,_T("(hide|show|nevershow)"),TP_GLOBAL},
{TOK_SHOWDETAILSUNINST,_T("ShowUninstDetails"),1,0,_T("(hide|show|nevershow)"),TP_GLOBAL},
{TOK_SHOWWINDOW,_T("ShowWindow"),2,0,_T("hwnd show_state"),TP_CODE},
{TOK_SILENTINST,_T("SilentInstall"),1,0,_T("(normal|silent|silentlog)"),TP_GLOBAL},
{TOK_SILENTUNINST,_T("SilentUnInstall"),1,0,_T("(normal|silent)"),TP_GLOBAL},
{TOK_SLEEP,_T("Sleep"),1,0,_T("sleep_time_in_ms"),TP_CODE},
{TOK_STRCMP,_T("StrCmp"),3,1,_T("str1 str2 label_to_goto_if_equal [label_to_goto_if_not]"),TP_CODE},
{TOK_STRCMPS,_T("StrCmpS"),3,1,_T("str1 str2 label_to_goto_if_equal [label_to_goto_if_not]"),TP_CODE},
{TOK_STRCPY,_T("StrCpy"),2,2,_T("$(user_var: output) str [maxlen] [startoffset]"),TP_CODE},
{TOK_UNSAFESTRCPY,_T("UnsafeStrCpy"),2,2,_T("$(var: output) str [maxlen] [startoffset]"),TP_CODE},
{TOK_STRLEN,_T("StrLen"),2,0,_T("$(user_var: length output) str"),TP_CODE},
{TOK_SUBCAPTION,_T("SubCaption"),2,0,_T("page_number(0-4) new_subcaption"),TP_GLOBAL},
#ifdef _UNICODE
{TOK_TARGET,_T("Target"),1,0,_T("cpu-charset"),TP_GLOBAL},
{TOK_TARGETCPU,_T("CPU"),1,0,_T("x86|amd64"),TP_GLOBAL},
{TOK_TARGETUNICODE,_T("Unicode"),1,0,_T("true|false"),TP_GLOBAL},
#endif
{TOK_UNINSTALLEXENAME,_T("UninstallExeName"),0,0,_T("no longer supported, use WriteUninstaller from section."),TP_ALL},
{TOK_UNINSTCAPTION,_T("UninstallCaption"),1,0,_T("uninstaller_caption"),TP_GLOBAL},
{TOK_UNINSTICON,_T("UninstallIcon"),1,0,_T("icon_on_local_system.ico"),TP_GLOBAL},
#ifdef NSIS_SUPPORT_CODECALLBACKS
{TOK_UNINSTPAGE,_T("UninstPage"),1,4,_T("((custom [creator_function] [leave_function] [caption]) | ((license|components|directory|instfiles|uninstConfirm) [pre_function] [show_function] [leave_function])) [/ENABLECANCEL]"),TP_GLOBAL},
#else
{TOK_UNINSTPAGE,_T("UninstPage"),1,1,_T("license|components|directory|instfiles|uninstConfirm [/ENABLECANCEL]"),TP_GLOBAL},
#endif
{TOK_UNINSTTEXT,_T("UninstallText"),1,1,_T("Text_to_go_on_uninstall_page [subtext]"),TP_PG},
{TOK_UNINSTSUBCAPTION,_T("UninstallSubCaption"),2,0,_T("page_number(0-2) new_subcaption"),TP_GLOBAL},
{TOK_UNREGDLL,_T("UnRegDLL"),1,0,_T("dll_path_on_target.dll"),TP_CODE},
{TOK_WINDOWICON,_T("WindowIcon"),1,0,_T("on|off"),TP_GLOBAL},
{TOK_WRITEINISTR,_T("WriteINIStr"),4,0,_T("ini_file section_name entry_name new_value"),TP_CODE},
{TOK_WRITEREGBIN,_T("WriteRegBin"),4,0,_T("rootkey subkey entry_name hex_string_like_12848412AB\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_WRITEREGMULTISZ,_T("WriteRegMultiStr"),5,0,_T("/REGEDIT5 rootkey subkey entry_name hex_string_like_660000000000\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_WRITEREGDWORD,_T("WriteRegDWORD"),4,0,_T("rootkey subkey entry_name new_value_dword\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_WRITEREGSTR,_T("WriteRegStr"),4,0,_T("rootkey subkey entry_name new_value_string\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_WRITEREGEXPANDSTR,_T("WriteRegExpandStr"),4,0,_T("rootkey subkey entry_name new_value_string\n    root_key=(HKCR[32|64]|HKLM[32|64]|HKCU[32|64]|HKU|HKCC|HKDD|HKPD|SHCTX)"),TP_CODE},
{TOK_WRITEREGNONE,_T("WriteRegNone"),3,1,_T("rootkey subkey entry_name [hex_data]"),TP_CODE},
{TOK_WRITEUNINSTALLER,_T("WriteUninstaller"),1,0,_T("uninstall_exe_name"),TP_CODE},
{TOK_PEADDRESOURCE,_T("PEAddResource"),3,2,_T("[/OVERWRITE|/REPLACE] file restype resname [reslang]"),TP_GLOBAL},
{TOK_PEREMOVERESOURCE,_T("PERemoveResource"),3,1,_T("[/NOERRORS] restype resname reslang|ALL"),TP_GLOBAL},
{TOK_PEDLLCHARACTERISTICS,_T("PEDllCharacteristics"),2,0,_T("addbits removebits"),TP_GLOBAL},
{TOK_PESUBSYSVER,_T("PESubsysVer"),1,0,_T("major.minor"),TP_GLOBAL},
{TOK_XPSTYLE,_T("XPStyle"),1,0,_T("(on|off)"),TP_GLOBAL},
{TOK_REQEXECLEVEL,_T("RequestExecutionLevel"),1,0,_T("none|user|highest|admin"),TP_GLOBAL},
{TOK_REQUNINSTEXECLEVEL,_T("RequestUninstallerExecutionLevel"),1,0,_T("none|user|highest|admin"),TP_GLOBAL},
{TOK_MANIFEST_APPENDCUSTOMSTRING,_T("ManifestAppendCustomString"),2,0,_T("path string"),TP_GLOBAL},
{TOK_MANIFEST_DPIAWARE,_T("ManifestDPIAware"),1,0,_T("notset|true|false"),TP_GLOBAL},
{TOK_MANIFEST_DPIAWARENESS,_T("ManifestDPIAwareness"),1,0,_T("comma_separated_string"),TP_GLOBAL},
{TOK_MANIFEST_LPAWARE,_T("ManifestLongPathAware"),1,0,_T("notset|true|false"),TP_GLOBAL},
{TOK_MANIFEST_SUPPORTEDOS,_T("ManifestSupportedOS"),1,-1,_T("none|all|WinVista|Win7|Win8|Win8.1|Win10|{GUID} [...]"),TP_GLOBAL},
{TOK_MANIFEST_MAXVERSIONTESTED,_T("ManifestMaxVersionTested"),1,0,_T("maj.min.bld.rev"),TP_GLOBAL},
{TOK_MANIFEST_DISABLEWINDOWFILTERING,_T("ManifestDisableWindowFiltering"),1,0,_T("notset|true"),TP_GLOBAL},
{TOK_MANIFEST_GDISCALING,_T("ManifestGdiScaling"),1,0,_T("notset|true"),TP_GLOBAL},
{TOK_P_PACKEXEHEADER,_T("!packhdr"),2,0,_T("temp_file_name command_line_to_compress_that_temp_file"),TP_ALL},
{TOK_P_FINALIZE,_T("!finalize"),1,2,_T("command_with_%1 [<OP retval>]"),TP_ALL},
{TOK_P_UNINSTFINALIZE,_T("!uninstfinalize"),1,2,_T("command_with_%1 [<OP retval>]"),TP_ALL},
{TOK_P_SYSTEMEXEC,_T("!system"),1,2,_T("command [<OP retval> | <retvalsymbol>]\n    OP=(< > <> =)"),TP_ALL},
{TOK_P_EXECUTE,_T("!execute"),1,2,_T("command [<OP retval> | <retvalsymbol>]\n    OP=(< > <> =)"),TP_ALL},
{TOK_P_MAKENSIS,_T("!makensis"),1,2,_T("parameters [<OP retval> | <retvalsymbol>]"),TP_ALL},
{TOK_P_ADDINCLUDEDIR,_T("!addincludedir"),1,0,_T("dir"),TP_ALL},
{TOK_P_INCLUDE,_T("!include"),1,2,_T("[/NONFATAL] [/CHARSET=<") TSTR_INPUTCHARSET _T(">] filename.nsh"),TP_ALL},
{TOK_P_CD,_T("!cd"),1,0,_T("absolute_or_relative_new_directory"),TP_ALL},
{TOK_P_IF,_T("!if"),1,3,_T("[!] (value [(==,!=,S==,S!=,=,<>,<=,<,>,>=,&,&&,||) value2] | /FILEEXISTS path)"),TP_ALL},
{TOK_P_IFDEF,_T("!ifdef"),1,-1,_T("symbol [| symbol2 [& symbol3 [...]]]"),TP_ALL},
{TOK_P_IFNDEF,_T("!ifndef"),1,-1,_T("symbol [| symbol2 [& symbol3 [...]]]"),TP_ALL},
{TOK_P_ENDIF,_T("!endif"),0,0,_T(""),TP_ALL},
{TOK_P_DEFINE,_T("!define"),1,5,_T("[/ifndef | /redef] ([/date|/utcdate] symbol [value]) | (/file symbol filename) | (/intfmt gflag fmtstr value) | (/math symbol val1 OP val2)\n    OP=(+ - * / % << >> >>> & | ^ ~ ! && ||)"),TP_ALL},
{TOK_P_UNDEF,_T("!undef"),1,-1,_T("[/noerrors] symbol [...]"),TP_ALL},
{TOK_P_ELSE,_T("!else"),0,-1,_T("[if[macro][n][def] ...]"),TP_ALL},
{TOK_P_ECHO,_T("!echo"),1,0,_T("message"),TP_ALL},
{TOK_P_WARNING,_T("!warning"),0,1,_T("[warning_message]"),TP_ALL},
{TOK_P_ERROR,_T("!error"),0,1,_T("[error_message]"),TP_ALL},
{TOK_P_ASSERT,_T("!assert"),2,2,_T("value [op value2] message"),TP_ALL},

{TOK_P_VERBOSE,_T("!verbose"),1,-1,_T("verbose_level | push | pop [...]"),TP_ALL},
{TOK_P_PRAGMA,_T("!pragma"),1,-1,_T("warning <enable|disable|default|error|warning> <code|all> | warning <push|pop>"),TP_ALL},

{TOK_P_MACRO,_T("!macro"),1,-1,_T("macroname [parms ...]"),TP_ALL},
{TOK_P_MACROEND,_T("!macroend"),0,0,_T(""),TP_ALL},
{TOK_P_MACROUNDEF,_T("!macroundef"),1,0,_T("macroname"),TP_ALL},
{TOK_P_INSERTMACRO,_T("!insertmacro"),1,-1,_T("macroname [parms ...]"),TP_ALL},
{TOK_P_IFMACRODEF,_T("!ifmacrodef"),1,-1,_T("macro [| macro2 [& macro3 [...]]]"),TP_ALL},
{TOK_P_IFMACRONDEF,_T("!ifmacrondef"),1,-1,_T("macro [| macro2 [& macro3 [...]]]"),TP_ALL},

{TOK_P_TEMPFILE,_T("!tempfile"),1,0,_T("symbol"),TP_ALL},
{TOK_P_DELFILE,_T("!delfile"),1,1,_T("[/nonfatal] file"),TP_ALL},
{TOK_P_APPENDFILE,_T("!appendfile"),2,2,_T("[/CHARSET=<") TSTR_OUTPUTCHARSET _T(">] [/RAWNL] file appended_line"),TP_ALL},
{TOK_P_APPENDMEMFILE,_T("!appendmemfile"),1,1,_T("memfile [append]"),TP_ALL},
{TOK_P_GETDLLVERSION,_T("!getdllversion"),2,3,_T("[/noerrors] [/packed] [/productversion] localfilename define_basename"),TP_ALL},
{TOK_P_GETTLBVERSION,_T("!gettlbversion"),2,2,_T("[/noerrors] [/packed] localfilename define_basename"),TP_ALL},

{TOK_P_SEARCHPARSESTRING,_T("!searchparse"),3,-1,_T("[/ignorecase] [/noerrors] [/file] source_string_or_file substring OUTPUTSYM1 [substring [OUTPUTSYM2 [substring ...]]]"),TP_ALL},
{TOK_P_SEARCHREPLACESTRING,_T("!searchreplace"),4,1,_T("[/ignorecase] output_name source_string substring replacestring"),TP_ALL},

{TOK_MISCBUTTONTEXT,_T("MiscButtonText"),0,4,_T("[back_button_text] [next_button_text] [cancel_button_text] [close_button_text]"),TP_GLOBAL},
{TOK_DETAILSBUTTONTEXT,_T("DetailsButtonText"),0,1,_T("[details_button_text]"),TP_PG},
{TOK_UNINSTBUTTONTEXT,_T("UninstallButtonText"),0,1,_T("[uninstall_button_text]"),TP_GLOBAL},
{TOK_INSTBUTTONTEXT,_T("InstallButtonText"),0,1,_T("[install_button_text]"),TP_GLOBAL},
{TOK_SPACETEXTS,_T("SpaceTexts"),0,2,_T("none | ([space_required_text] [space_available_text])"),TP_GLOBAL},
{TOK_COMPLETEDTEXT,_T("CompletedText"),0,1,_T("[completed_text]"),TP_PG},

{TOK_GETFUNCTIONADDR,_T("GetFunctionAddress"),2,0,_T("output function"),TP_CODE},
{TOK_GETLABELADDR,_T("GetLabelAddress"),2,0,_T("output label"),TP_CODE},
{TOK_GETCURRENTADDR,_T("GetCurrentAddress"),1,0,_T("output"),TP_CODE},

{TOK_PLUGINDIR,_T("!addplugindir"),1,1,_T("[/target] new_plugin_directory"),TP_ALL},
{TOK_INITPLUGINSDIR,_T("InitPluginsDir"),0,0,_T(""),TP_CODE},
// Added by ramon 23 May 2003
{TOK_ALLOWSKIPFILES,_T("AllowSkipFiles"),1,0,_T("(off|on)"),TP_ALL},
// Added by ramon 3 jun 2003
{TOK_DEFVAR,_T("Var"),1,1,_T("[/GLOBAL] var_name"),TP_ALL},
// Added by ramon 6 jun 2003
{TOK_VI_ADDKEY,_T("VIAddVersionKey"),2,1,_T("[/LANG=lang_id] keyname value"),TP_GLOBAL},
{TOK_VI_SETPRODUCTVERSION,_T("VIProductVersion"),1,0,_T("version_string_X.X.X.X"),TP_GLOBAL},
{TOK_VI_SETFILEVERSION,_T("VIFileVersion"),1,0,_T("version_string_X.X.X.X"),TP_GLOBAL},
{TOK_LOCKWINDOW,_T("LockWindow"),1,0,_T("(on|off)"),TP_CODE},
};

const TCHAR* CEXEBuild::get_commandtoken_name(int tok)
{
  for (UINT x = 0; x < TOK__LAST; ++x)
    if (tokenlist[x].id==tok) return tokenlist[x].name;
  return 0;
}

bool CEXEBuild::print_cmdhelp(const TCHAR *commandname, bool cmdhelp)
{
  // Print function chosen at run time because of bug #1203, -CMDHELP to stdout.
  void (CEXEBuild::*printer)(const TCHAR *s, ...) const = cmdhelp ? &CEXEBuild::INFO_MSG : &CEXEBuild::ERROR_MSG;
  UINT x;
  for (x = 0; x < TOK__LAST; ++x)
  {
    if (!commandname || !_tcsicmp(tokenlist[x].name,commandname))
    {
      (this->*printer)(_T("%") NPRIs _T("%") NPRIs _T(" %") NPRIs _T("\n"),commandname?_T("Usage: "):_T(""),tokenlist[x].name,tokenlist[x].usage_str);
      if (commandname) break;
    }
  }
  if (x == TOK__LAST && commandname)
  {
    ERROR_MSG(_T("Invalid command \"%") NPRIs _T("\"\n"),commandname);
    return false;
  }
  return true;
}

void CEXEBuild::print_help(const TCHAR *commandname)
{
  print_cmdhelp(commandname);
}

bool CEXEBuild::is_ppbranch_token(const TCHAR *s)
{
  int np, op, pos, tkid = get_commandtoken(s, &np, &op, &pos);
  switch(tkid)
  {
  case TOK_P_IF: case TOK_P_ELSE: case TOK_P_ENDIF:
  case TOK_P_IFDEF: case TOK_P_IFNDEF:
  case TOK_P_IFMACRODEF: case TOK_P_IFMACRONDEF:
    return true;
  default:
    return false;
  }
}

bool CEXEBuild::is_pp_token(int tkid)
{
  // NOTE: This assumes that all TOK_P_* in tokens.h are grouped together.
  return (tkid >= TOK_P_IF && tkid <= TOK_P_SEARCHREPLACESTRING);
}

bool CEXEBuild::is_unsafe_pp_token(int tkid)
{
  switch(tkid)
  {
  case TOK_P_TEMPFILE: case TOK_P_APPENDFILE: case TOK_P_DELFILE:
  case TOK_P_SYSTEMEXEC: case TOK_P_EXECUTE: case TOK_P_MAKENSIS:
  case TOK_P_PACKEXEHEADER: case TOK_P_FINALIZE:
    return true;
  }
  return false;
}

int CEXEBuild::get_commandtoken(const TCHAR *s, int *np, int *op, int *pos)
{
  for (UINT x = 0; x < TOK__LAST; ++x)
    if (!_tcsicmp(tokenlist[x].name,s)) 
    {
      *np=tokenlist[x].num_parms;
      *op=tokenlist[x].opt_parms;
      *pos=x;
      return tokenlist[x].id;
    }
  return -1;
}

int CEXEBuild::GetCurrentTokenPlace()
{
  if (build_cursection)
    return build_cursection_isfunc ? TP_FUNC : TP_SEC;

  if (cur_page)
    return TP_PAGEEX;

  return TP_GLOBAL;
}

int CEXEBuild::IsTokenPlacedRight(int pos, const TCHAR *tok)
{
  if (preprocessonly)
    return PS_OK;
  if ((unsigned int) pos > (sizeof(tokenlist) / sizeof(tokenType)))
    return PS_OK;

  int tp = tokenlist[pos].placement;
  int cp = GetCurrentTokenPlace();
  if (tp & cp) {
    return PS_OK;
  }
  else {
    TCHAR err[1024];
    if (cp == TP_SEC) {
      _tcscpy(err, _T("Error: command %") NPRIs _T(" not valid in Section\n"));
    }
    else if (cp == TP_FUNC) {
      _tcscpy(err, _T("Error: command %") NPRIs _T(" not valid in Function\n"));
    }
    else if (cp == TP_PAGEEX) {
      _tcscpy(err, _T("Error: command %") NPRIs _T(" not valid in PageEx\n"));
    }
    else
    {
      _tcscpy(err, _T("Error: command %") NPRIs _T(" not valid outside "));
      if (tp & TP_SEC)
        _tcscat(err, _T("Section"));
      if (tp & TP_FUNC)
      {
        if (tp & TP_SEC)
        {
          if (tp & TP_PAGEEX)
          {
            _tcscat(err, _T(", "));
          }
          else
          {
            _tcscat(err, _T(" or "));
          }
        }
        _tcscat(err, _T("Function"));
      }
      if (tp & TP_PAGEEX)
      {
        if (tp & TP_CODE)
        {
          _tcscat(err, _T(" or "));
        }
        _tcscat(err, _T("PageEx"));
      }
      _tcscat(err, _T("\n"));
    }
    ERROR_MSG(err, tok);
    return PS_ERROR;
  }
}
