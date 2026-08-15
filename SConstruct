import platform; print("Using Python " + platform.python_version())
EnsureSConsVersion(1,2)
EnsurePythonVersion(2,7)

stubs = [
	'bzip2',
	'lzma',
	'zlib',
	'zstd'
]

plugin_libs = [
	'ExDLL'
]

plugins = [
	'AdvSplash',
	'Banner',
	'BgImage',
	'Dialer',
	'InstallOptions',
	'LangDLL',
	'Library/TypeLib',
	'Math',
	'nsDialogs',
	'nsExec',
	'NSISdl',
	'Splash',
	'StartMenu',
	'System',
	'UserInfo',
	'VPatch/Source/Plugin',
]

utils = [
	'Library/RegTool',
	'MakeLangId',
	'Makensisw',
	'NSIS Menu',
	'UIs',
	'SubStart',
	'VPatch/Source/GenPat',
	'zip2exe'
]

misc = [
	'Graphics',
	'Language files',
	'MultiUser',
	'Modern UI',
	'Modern UI 2',
	'VPatch'
]

doc = [
	'COPYING'
]

doctypes = [
	'chm',
	'html',
	'htmlsingle',
	'web',
]

######################################################################
#######  Build Environment                                         ###
######################################################################

import os
path = ARGUMENTS.get('PATH', '')
toolset = ARGUMENTS.get('TOOLSET', '')

defenv = {
	'TARGET_ARCH': ARGUMENTS.get('TARGET_ARCH', 'x86'),
	'ENV': {}
}

if path: defenv['ENV']['PATH'] = path
if toolset: defenv['TOOLS'] = toolset.split(',') + ['zip']

if len(defenv['ENV']) == 0: del defenv['ENV']
defenv = Environment(**defenv)
Export('defenv')

######################################################################
#######  Includes                                                  ###
######################################################################

SConscript('SCons/utils.py')
Import('GetOptionOrEnv')

######################################################################
#######  Options                                                   ###
######################################################################

default_doctype = 'html'
if defenv.WhereIs('hhc', os.environ['PATH']):
	default_doctype = 'chm'

import time
cvs_version = time.strftime('%d-%b-%Y.cvs', time.gmtime(int(os.environ.get('SOURCE_DATE_EPOCH', time.time()))))

opts = Variables()

# load configuration options
#  it's important this will be done here so NSIS_CONFIG_CONST_DATA_PATH
#  will be available for the next few lines and so `dirs` can be set
SConscript('SCons/config.py')

opts.Update(defenv)
Help(opts.GenerateHelpText(defenv))

#Fix ListVariable to generate comma separated (quoted if required) list of allowed names:
org_ListVariable = ListVariable
def ListVariable(key, help, default, names, map={}):
	r = list(org_ListVariable(key, help, default, names, map))
	if len(r) == 5 and isinstance(r[1], str):
		fmt = 'allowed names: %s'
		repl = fmt % ', '.join( ((' ' in s) and ('"%s"' % s) or s) for s in names )
		r[1] = r[1].replace(fmt % ' '.join(names), repl)
	return r


install_dirs = {
	'relocatable': {
		'dest': '',
		'prefix': '',
		'conf': '$PREFIX',
		'bin': '$PREFIX',
		'data': '$PREFIX',
		'doc': '$PREFIX'
	},
	'static': {
		'dest': '',
		'prefix': '/usr/local',
		'conf': '$PREFIX/etc',
		'bin': '$PREFIX/bin',
		'data': '$PREFIX/share/nsis',
		'doc': '$PREFIX/share/doc/nsis'
	}
}

if 'NSIS_CONFIG_CONST_DATA_PATH' in defenv['NSIS_CPPDEFINES']:
	dirs = install_dirs['static']
else:
	dirs = install_dirs['relocatable']

if defenv['PLATFORM'] == 'win32':
	ignore_tests = 'none'
else:
	ignore_tests = ','.join(Split("""
Examples/makensis.nsi"""))

# version
opts.Add(('VERSION', 'Version of NSIS', cvs_version))
opts.Add(('VER_MAJOR', 'Major version of NSIS (recommended for dist-installer)', None))
opts.Add(('VER_MINOR', 'Minor version of NSIS (recommended for dist-installer)', None))
opts.Add(('VER_REVISION', 'Revision of NSIS', None))
opts.Add(('VER_BUILD', 'Build version of NSIS', None))
opts.Add(('VER_PACKED', 'Packed version of NSIS in 0xMMmmmrrb format, used for feature detection in scripts (recommended if VER_MAJOR and VER_MINOR are not set)', None))
# installation
opts.Add(('PREFIX', 'Installation prefix', dirs['prefix']))
opts.Add(ListVariable('SKIPSTUBS', 'A list of stubs that will not be built', 'none', stubs))
opts.Add(ListVariable('SKIPPLUGINS', 'A list of plug-ins that will not be built', 'none', plugin_libs + plugins))
opts.Add(ListVariable('SKIPUTILS', 'A list of utilities that will not be built', 'NSIS Menu', utils))
opts.Add(ListVariable('SKIPMISC', 'A list of plug-ins that will not be built', 'none', misc))
opts.Add(ListVariable('SKIPDOC', 'A list of doc files that will not be built/installed', 'none', doc))
opts.Add(('SKIPTESTS', 'A comma-separated list of test files that will not be ran', 'none'))
opts.Add(('IGNORETESTS', 'A comma-separated list of test files that will be ran but ignored', ignore_tests))
# build tools
opts.Add(('PATH', 'A colon-separated list of system paths instead of the default - TEMPORARY AND MAY DEPRECATE', None))
opts.Add(('TOOLSET', 'A comma-separated list of specific tools used for building instead of the default', None))
opts.Add(BoolVariable('MSTOOLKIT', 'Use Microsoft Visual C++ Toolkit', 'no'))
opts.Add(EnumVariable('MSVS_VERSION', 'MS Visual C++ version', os.environ.get('MSVS_VERSION'), allowed_values=('6.0', '7.0', '7.1', '8.0', '8.0Exp', '9.0', '9.0Exp', '10.0', '10.0Exp')))
opts.Add(EnumVariable('TARGET_ARCH', 'Target processor architecture', 'x86', allowed_values=('x86', 'amd64', 'arm64')))
opts.Add(ListVariable('DOCTYPES', 'A list of document types that will be built', default_doctype, doctypes))
opts.Add(('CC', 'Override C compiler', None))
opts.Add(('CXX', 'Override C++ compiler', None))
opts.Add(PathVariable('APPEND_CPPPATH', 'Additional paths to search for include files', None))
opts.Add(PathVariable('APPEND_LIBPATH', 'Additional paths to search for libraries', None))
opts.Add(('APPEND_CCFLAGS', 'Additional C/C++ compiler flags'))
opts.Add(('APPEND_LINKFLAGS', 'Additional linker flags'))
opts.Add(PathVariable('WXWIN', 'Path to wxWindows library folder (e.g. C:\\Dev\\wxWidgets-2.8.10)', os.environ.get('WXWIN')))
opts.Add(PathVariable('ZLIB_W32', 'Path to Win32 zlib library folder (e.g. C:\\Dev\\zlib-1.2.3)', os.environ.get('ZLIB_W32')))
opts.Add(PathVariable('ZSTD_W32', 'Path to Win32 zstd library folder (e.g. C:\\Dev\\zstd-1.5.7)', os.environ.get('ZSTD_W32')))
# build options
opts.Add(BoolVariable('UNICODE', 'Build the Unicode version of the compiler and tools', 'yes'))
opts.Add(BoolVariable('DEBUG', 'Build executables with debugging information', 'no'))
opts.Add(PathVariable('CODESIGNER', 'A program used to sign executables', None))
opts.Add(BoolVariable('STRIP', 'Strips executables of any unrequired data such as symbols', 'yes'))
opts.Add(BoolVariable('STRIP_CP', 'Strips cross-platform executables of any unrequired data such as symbols', 'yes'))
opts.Add(BoolVariable('STRIP_W32', 'Strips Win32 executables of any unrequired data such as symbols', 'yes'))
# path related build options
opts.Add(('PREFIX_DEST', 'Intermediate installation prefix (extra install time prefix)', dirs['dest']))
opts.Add(('PREFIX_CONF', 'Path to install nsisconf.nsh to', dirs['conf']))
opts.Add(('PREFIX_BIN', 'Path to install native binaries to', dirs['bin']))
opts.Add(('PREFIX_DATA', 'Path to install nsis data to (plugins, includes, stubs, contrib, win32 binaries)', dirs['data']))
opts.Add(('PREFIX_DOC','Path to install nsis README / INSTALL / TODO files to.', dirs['doc']))
opts.Add(('PREFIX_PLUGINAPI_INC','Path to install plugin API headers to.', None))
opts.Add(('PREFIX_PLUGINAPI_LIB','Path to install plugin static library to.', None))
# reproducible builds
opts.Add(('SOURCE_DATE_EPOCH', 'UNIX timestamp (in seconds)', os.environ.get('SOURCE_DATE_EPOCH')))

opts.Update(defenv)
Help(opts.GenerateHelpText(defenv))

if defenv['TARGET_ARCH'] != 'x86':
	defenv['UNICODE'] = True

if defenv['DEBUG']:
	defenv.Append(CPPDEFINES = ['DEBUG'])

# add prefixes defines
if 'NSIS_CONFIG_CONST_DATA_PATH' in defenv['NSIS_CPPDEFINES']:
	defenv.Append(NSIS_CPPDEFINES = [('PREFIX_CONF', '"%s"' % defenv.subst('$PREFIX_CONF'))])
	defenv.Append(NSIS_CPPDEFINES = [('PREFIX_DATA', '"%s"' % defenv.subst('$PREFIX_DATA'))])
	defenv.Append(NSIS_CPPDEFINES = [('PREFIX_DOC', '"%s"' % defenv.subst('$PREFIX_DOC'))])

if defenv.get('SOURCE_DATE_EPOCH','') != '':
	if defenv.get('SOURCE_DATE_EPOCH','').lower() == 'now':
		import time
		defenv['SOURCE_DATE_EPOCH'] = str(int(time.time()))
	defenv['ENV']['SOURCE_DATE_EPOCH'] = defenv['SOURCE_DATE_EPOCH'] = int(defenv['SOURCE_DATE_EPOCH'], 0) # Normalize and apply to ENV for child processes
	defenv.Append(NSIS_CPPDEFINES = [('NSIS_SOURCE_DATE_EPOCH', '%s' % defenv['SOURCE_DATE_EPOCH'])]) # Display in /HDRINFO

# Need this early for the config header files to be placed in
if defenv['UNICODE']:
	if defenv['DEBUG']:
		defenv.Replace(BUILD_PREFIX = 'build/udebug')
	else:
		defenv.Replace(BUILD_PREFIX = 'build/urelease')
else:
	if defenv['DEBUG']:
		defenv.Replace(BUILD_PREFIX = 'build/debug')
	else:
		defenv.Replace(BUILD_PREFIX = 'build/release')

defenv.Replace(BUILD_CONFIG = defenv.subst('$BUILD_PREFIX/config'))

# ensure the config directory exists
if not Dir(defenv.Dir('#$BUILD_CONFIG')).exists():
	defenv.Execute(Mkdir(defenv.Dir('#$BUILD_CONFIG')))

# write configuration into sconf.h and defines.h
sconf_h = open(defenv.File('#$BUILD_CONFIG/nsis-sconf.h').abspath, 'w')
sconf_h.write('// This file is automatically generated by SCons\n// DO NOT EDIT THIS FILE\n')

defines_h = open(defenv.File('#$BUILD_CONFIG/nsis-defines.h').abspath, 'w')
defines_h.write('// This file is automatically generated by SCons\n// DO NOT EDIT THIS FILE\n')

for i in defenv['NSIS_CPPDEFINES']:
	if type(i) is not str:
		sconf_h.write('#define %s %s\n' % (i[0], i[1]))
		if str(i[1])[0] != '"':
			defines_h.write('definedlist.add(_T("%s"), _T("%s"));\n' % (i[0], i[1]))
		else:
			defines_h.write('definedlist.add(_T("%s"), _T(%s));\n' % (i[0], i[1]))
	else:
		sconf_h.write('#define %s\n' % (i))
		defines_h.write('definedlist.add(_T("%s"));\n' % (i))

sconf_h.write('#define NSIS_DEFAULT_TARGET_ARCH "%s"\n' % defenv['TARGET_ARCH'])
sconf_h.close()
defines_h.close()

# write version into version.h
f = open(defenv.File('#$BUILD_CONFIG/nsis-version.h').abspath, 'w')
f.write('// This file is automatically generated by SCons\n// DO NOT EDIT THIS FILE\n')
f.write('#include "%s"\n' % File('#/Source/tchar.h').abspath)

if (not 'VER_PACKED' in defenv) and 'VER_MAJOR' in defenv and 'VER_MINOR' in defenv:
	packed_r = int(defenv.get('VER_REVISION','0'))
	packed_b = int(defenv.get('VER_BUILD','0'))
	defenv['VER_PACKED'] = '0x%0.2i%0.3i%0.2i%0.1i' % (int(defenv['VER_MAJOR']), int(defenv['VER_MINOR']), packed_r, packed_b)
if not 'VER_PACKED' in defenv:
	import re
	found = None
	for v in re.compile(r'^\\H\{[v]?(\S+)\}', re.M).finditer(File('#/Docs/src/history.but').get_contents().decode()): # Try to parse the Halibut history file
		if v and not found:
			v = v.group(1).split('.')
			if len(v) >= 2:
				mi = int(re.search(r'\d+', v[1]).group())
				if mi < 1: mi = 1 # Make sure we can subtract 1 from the minor number so trunk stays below the next release
				defenv['VER_PACKED'] = '0x%0.2i%0.3i%0.2i%0.1i' % (int(re.search(r'\d+', v[0]).group()), mi - 1, 66, 6)
				if int(defenv['VER_PACKED'], 0) >= int('0x03000000', 0):
					found = v
	if not found:
		defenv['VER_PACKED'] = '0x%0.2i%0.3i%0.2i%0.1i' % (3, 3, 42, 0) # Default to a version number we never used
	print('WARNING: VER_PACKED not set, defaulting to %s!' % defenv['VER_PACKED'])
if int(defenv['VER_PACKED'], 0) < int('0x03000000', 0) or int(defenv['VER_PACKED'], 0) >= int('0x04000000', 0):
	print('ERROR: Invalid VER_PACKED value!')
	Exit(1)
f.write('#define NSIS_PACKEDVERSION _T("%s")\n' % defenv['VER_PACKED'])

if defenv.get('VERSION','') == '' and 'VER_MAJOR' in defenv:
	defenv['VERSION'] = defenv['VER_MAJOR']
	if 'VER_MINOR' in defenv:
		defenv['VERSION'] += '.' + defenv['VER_MINOR']
	if 'VER_REVISION' in defenv:
		defenv['VERSION'] += '.' + defenv['VER_REVISION']
if defenv.get('VERSION','') == '' and int(defenv['VER_PACKED'], 0) > int('0x02000000', 0):
	defenv['VERSION'] = '%i.%i.%i' % (int(defenv['VER_PACKED'][2:][:2]), int(defenv['VER_PACKED'][4:][:3]), int(defenv['VER_PACKED'][7:][:2]))
	print('WARNING: VERSION not set, defaulting to %s!' % defenv['VERSION'])
f.write('#define NSIS_VERSION _T("v%s")\n' % defenv['VERSION'])

f.close()

######################################################################
#######  Common Functions                                          ###
######################################################################

def GetArcCPU(env):
	if (not 'TARGET_ARCH' in env) or env['TARGET_ARCH'] == 'x86':
		return 'x86'
	return env['TARGET_ARCH']

def GetArcSuffix(env, unicode = None):
	if unicode is None:
		unicode = 'UNICODE' in env['CPPDEFINES']
	suff = '-unicode'
	if not unicode:
		suff = '-ansi'
	return GetArcCPU(env) + suff

def GetTargetMinOSVersion(env):
	if GetArcCPU(defenv) == 'arm64':
		return (6, 4)
	if not GetArcCPU(defenv) == 'x86':
		return (5, 1)
	return (4, 0)


def SafeFile(f):
	from types import StringType

	if isinstance(f, StringType):
		return File(f)

	return f

def MakeFileList(files):
	return Flatten(File(files))

def AddEnvStandardFlags(env, defines=None, flags=None, libs=None, entry=None, nodeflib=None):
	if defines:
		env.Append(CPPDEFINES = defines)
	if flags:
		env.Append(CCFLAGS = flags)
	if libs:
		env.Append(LIBS = libs)

	if entry:
		unicodestr = "None"
		if 'UNICODE' in env['CPPDEFINES']:
			unicodestr = "True"
		env.Append(LINKFLAGS = ['${ENTRY_FLAG("%s",%s)}' % (entry,unicodestr)])

	if nodeflib:
		env.Append(LINKFLAGS = ['$NODEFLIBS_FLAG']) # no default libraries

def AppendRES(env, source, res, resources):
	if res:
		target = MakeFileList(res)[0].name.replace('.rc', '-rc')
		target_res = env.RES(target, res)
		if resources:
			env.Depends(target_res, resources)
		source.append(target_res)

def CleanMap(env, target, target_name):
	env.Clean(target, File(target_name + '.map'))


######################################################################
#######  Functions                                                 ###
######################################################################

defenv['ZIPDISTDIR'] = defenv.Dir('#nsis-$VERSION')
defenv['INSTDISTDIR'] = defenv.Dir('#.instdist')
defenv['TESTDISTDIR'] = defenv.Dir('#.test')
defenv['DISTSUFFIX'] = ''

if GetArcCPU(defenv) != 'x86':
	defenv['DISTSUFFIX'] += GetArcCPU(defenv)
if 'CODESIGNER' in defenv:
	defenv['DISTSUFFIX'] += '-signed'

defenv.Execute(Delete('$ZIPDISTDIR'))
defenv.Execute(Delete('$INSTDISTDIR'))
defenv.Execute(Delete('$TESTDISTDIR'))

def Distribute(files, names, component, path, subpath, alias, install_alias=None):
	files = MakeFileList(files)

	names = names or list(map(lambda x: x.name, files))
	if isinstance(names, str):
		names = [names]

	for d in ('$ZIPDISTDIR', '$INSTDISTDIR', '$TESTDISTDIR'):
		paths = list(map(lambda file: os.path.join(d, path, subpath, file), names))
		defenv.InstallAs(paths, files)

	if ('PREFIX' in defenv and defenv['PREFIX']) or ('PREFIX_DEST' in defenv and defenv['PREFIX_DEST']) :
		prefix = '${PREFIX_DEST}${PREFIX_%s}' % component.upper()
		paths = list(map(lambda file: os.path.join(prefix, path, subpath, file), names))
		ins = defenv.InstallAs(paths, files)
	else:
		ins = []

	if ins:
		defenv.Alias('install', ins)
		defenv.Alias('install-%s' % component, ins)
		if alias:
			defenv.Alias(alias, ins)
		if install_alias:
			defenv.Alias('install-%s' % install_alias, ins)

		return ins

def DistributeBin(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'bin', '', path, alias)

def DistributeConf(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'conf', '', path, alias)

def DistributeW32Bin(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Bin', path, alias, 'w32bin')

def DistributeStubs(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Stubs', path, alias, 'stubs')

def DistributePlugin(files, names=[], arcsubpath='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Plugins', arcsubpath, alias, 'plugins')

def DistributeContrib(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Contrib', path, alias, 'contrib')

def DistributeMenu(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Menu', path, alias, 'menu')

def DistributeInclude(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'data', 'Include', path, alias, 'includes')

def DistributeDoc(files, names=[], path='', alias=None, basepath='', install_alias='docs'):
	return defenv.Distribute(files, names, 'doc', path=basepath, subpath=path, alias=alias, install_alias=install_alias)

def DistributeDocs(files, names=[], path='', alias=None, basepath='Docs', install_alias='docs'):
	return defenv.Distribute(files, names, 'doc', path=basepath, subpath=path, alias=alias, install_alias=install_alias)

def DistributeExamples(files, names=[], path='', alias=None):
	return defenv.Distribute(files, names, 'doc', 'Examples', path, alias, 'examples')

def FindMakeNSIS(env, path):
	exename = 'makensis_not_found'
	file = env.FindFile('makensis$PROGSUFFIX', 
		[os.path.join(path, '.'), os.path.join(path, 'Bin')])
	if file:
		exename = str(file)
	return exename

def Sign(targets):
	if 'CODESIGNER' in defenv:
		for t in targets:
			a = defenv.Action('$CODESIGNER "%s"' % t.path)
			defenv.AddPostAction(t, a)

Import('SilentActionEcho IsPEExecutable SetPESecurityFlagsWorker SetPEMinOS MakeReproducibleAction')
def SetPESecurityFlagsAction(target, source, env):
	for t in target:
		SetPESecurityFlagsWorker(t.path)
g_pesecflagsecho = {}
def SetPESecurityFlagsActionEcho(target, source, env):
	for t in target:
		if t.name[-4:].lower() == '.dll' and IsPEExecutable(t.path) and not t.path in g_pesecflagsecho:
			g_pesecflagsecho[t.path] = 1
			print('Setting PE flags on %s' % t.name)
def SetPESecurityFlags(targets):
	for t in targets:
		a = defenv.Action(SetPESecurityFlagsAction, strfunction=SetPESecurityFlagsActionEcho)
		defenv.AddPostAction(t, a)

def SetTargetPEMinOS(target, source=None, env=None):
	for t in target:
		if source is None:
			defenv.AddPostAction(t, defenv.Action(SetTargetPEMinOS, strfunction=SilentActionEcho))
		else:
			ver = GetTargetMinOSVersion(env)
			SetPEMinOS(t.path, ver[0], ver[1], ver[0], ver[1])

def MakeReproducible(targets):
	for t in targets:
		defenv.AddPostAction(t, defenv.Action(MakeReproducibleAction, strfunction=SilentActionEcho))

def TestScript(scripts):
	defenv.Install('$TESTDISTDIR/Tests', scripts)

defenv.Distribute = Distribute
defenv.DistributeBin = DistributeBin
defenv.DistributeConf = DistributeConf
defenv.DistributeW32Bin = DistributeW32Bin
defenv.DistributeStubs = DistributeStubs
defenv.DistributePlugin = DistributePlugin
defenv.DistributeContrib = DistributeContrib
defenv.DistributeMenu = DistributeMenu
defenv.DistributeInclude = DistributeInclude
defenv.DistributeDoc = DistributeDoc
defenv.DistributeDocs = DistributeDocs
defenv.DistributeExamples = DistributeExamples
defenv.Sign = Sign
defenv.SetPESecurityFlags = SetPESecurityFlags
defenv.SetTargetPEMinOS = SetTargetPEMinOS
defenv.MakeReproducible = MakeReproducible
defenv.TestScript = TestScript

def DistributeExtras(env, target, examples, docs):
	if examples:
		env.DistributeExamples(examples, path=target)
	if docs:
		env.DistributeDocs(docs, path=target)

######################################################################
#######  Environments                                              ###
######################################################################

if defenv['MSTOOLKIT']:
	if GetOptionOrEnv('MSVC_USE_SCRIPT', '!') != '!':
		defenv['MSVC_USE_SCRIPT'] = GetOptionOrEnv('MSVC_USE_SCRIPT')
	defenv.Tool('mstoolkit', toolpath = [Dir('SCons/Tools').rdir()])

defenv.Append(CCFLAGS = Split('$APPEND_CCFLAGS'))
defenv.Append(LINKFLAGS = Split('$APPEND_LINKFLAGS'))
defenv.Append(CPPPATH = Split('$APPEND_CPPPATH'))
defenv.Append(LIBPATH = Split('$APPEND_LIBPATH'))

defenv.Default('$BUILD_PREFIX')

if 'ZLIB_W32' in defenv:
	defenv['ZLIB_W32_INC'] = os.path.dirname(str(
		defenv.FindFile('zlib.h', 
			[
				defenv['ZLIB_W32'], 
				os.path.join(defenv['ZLIB_W32'], 'include')
			]
		)
	))
	# Search for import library of zlib for mingw or VisualC
	for importlib in ['libzdll.a', 'libz.dll.a', 'zdll.lib']:
		defenv['ZLIB_W32_LIB'] = os.path.dirname(str(
			defenv.FindFile(importlib, 
				[
					defenv['ZLIB_W32'], 
					os.path.join(defenv['ZLIB_W32'], 'lib')
				]
			)
		))
		if defenv['ZLIB_W32_LIB']:
			break
	defenv['ZLIB_W32_DLL'] = defenv.FindFile('zlib1.dll', 
		[defenv['ZLIB_W32'], defenv['ZLIB_W32_LIB']])
	defenv['ZLIB_W32_NEW_DLL'] = defenv.FindFile('zlib.dll',
		[defenv['ZLIB_W32'], defenv['ZLIB_W32_LIB']])

if 'ZSTD_W32' in defenv:
	defenv['ZSTD_W32_INC'] = os.path.dirname(str(
		defenv.FindFile('zstd.h',
			[
				defenv['ZSTD_W32'],
				os.path.join(defenv['ZSTD_W32'], 'include')
			]
		)
	))
	for importlib in ['libzstd.dll.a', 'libzstd.a', 'zstd.lib']:
		defenv['ZSTD_W32_LIB'] = os.path.dirname(str(
			defenv.FindFile(importlib,
				[
					defenv['ZSTD_W32'],
					os.path.join(defenv['ZSTD_W32'], 'lib')
				]
			)
		))
		if defenv['ZSTD_W32_LIB']:
			break
	defenv['ZSTD_W32_DLL'] = defenv.FindFile('libzstd.dll',
		[defenv['ZSTD_W32'], defenv['ZSTD_W32_LIB']])

tools = defenv['TOOLS']

envs = []

if 'msvc' in tools or 'mstoolkit' in tools:
	envs = SConscript('SCons/Config/ms')
elif 'gcc' in tools:
	envs = SConscript('SCons/Config/gnu')
elif 'hpc++' in tools:
	envs = SConscript('SCons/Config/hpc++')
else:
	envs = SConscript('SCons/Config/default')

stub_env = envs[0]
makensis_env = envs[1]
plugin_env = envs[2]
util_env = envs[3]
cp_util_env = envs[4]
test_env = envs[5]
stub_uenv = envs[6]
plugin_uenv = envs[7]

Export('plugin_env plugin_uenv')

######################################################################
#######  Distribution                                              ###
######################################################################

if defenv['PLATFORM'] == 'win32':
	def build_nsis_menu_for_zip(target, source, env):
		cmdline = FindMakeNSIS(env, str(env['ZIPDISTDIR']))
		if Execute('''"{}" "{}" /X"OutFile '{}'" '''.format(cmdline, source[0].abspath, target[0].abspath)):
			Exit(1)

	nsis_menu_target = defenv.Command(
		os.path.join('$ZIPDISTDIR', 'NSIS.exe'),
		os.path.join('$ZIPDISTDIR', 'Examples', 'NSISMenu.nsi'),
		build_nsis_menu_for_zip
	)
	defenv.Depends(nsis_menu_target, r'$ZIPDISTDIR\makensis.exe')
	defenv.Depends(nsis_menu_target, r'$ZIPDISTDIR\Stubs')
	defenv.Depends(nsis_menu_target, r'$ZIPDISTDIR\Plugins')
	defenv.MakeReproducible(nsis_menu_target)
	defenv.Sign(nsis_menu_target)

dist_zip = 'nsis-${VERSION}${DISTSUFFIX}.zip'
zip_target = defenv.Zip(dist_zip, '$ZIPDISTDIR')
defenv.Alias('dist-zip', zip_target)

AlwaysBuild(defenv.AddPostAction(zip_target, Delete('$ZIPDISTDIR')))

if defenv['PLATFORM'] == 'win32':
	optchar = '/'
else:
	optchar = '-'

defenv['INSTVER'] = '%sDVERSION=$VERSION' % optchar
if 'VER_MAJOR' in defenv and 'VER_MINOR' in defenv \
    and 'VER_REVISION' in defenv and 'VER_BUILD' in defenv:
	defenv['INSTVER'] += ' %sDVER_MAJOR=$VER_MAJOR' % optchar
	defenv['INSTVER'] += ' %sDVER_MINOR=$VER_MINOR' % optchar
	defenv['INSTVER'] += ' %sDVER_REVISION=$VER_REVISION' % optchar
	defenv['INSTVER'] += ' %sDVER_BUILD=$VER_BUILD' % optchar

inst_env = {}
inst_env['NSISDIR'] = os.path.abspath(str(defenv['INSTDISTDIR']))
inst_env['NSISCONFDIR'] = os.path.abspath(str(defenv['INSTDISTDIR']))

def build_installer(target, source, env):
	cmdline = FindMakeNSIS(env, str(env['INSTDISTDIR'])) + ' %sDOUTFILE=%s %s' % (optchar, target[0].abspath, env['INSTVER'])
	if 'ZLIB_W32_NEW_DLL' in env and env['ZLIB_W32_NEW_DLL']:
		cmdline += ' %sDUSE_NEW_ZLIB' % optchar
	cmd = env.Command(None, source, cmdline + ' $SOURCE')
	AlwaysBuild(cmd)
	# Comment out the following if you want to see the installation directory
	# after the build is finished.
	#AlwaysBuild(env.AddPostAction(cmd, Delete('$INSTDISTDIR')))
	env.Alias('dist-installer', cmd)

installer_target = defenv.Command('nsis-${VERSION}-setup${DISTSUFFIX}.exe',
                                  os.path.join('$INSTDISTDIR', 'Examples', 'makensis.nsi'),
                                  build_installer,
                                  ENV = inst_env)
defenv.Depends(installer_target, '$INSTDISTDIR')
defenv.Sign(installer_target)
defenv.Alias('dist-installer', installer_target)

defenv.Alias('dist', ['dist-zip', 'dist-installer'])

######################################################################
#######  Distribute Basics                                         ###
######################################################################

for d in doc:
	if d in defenv['SKIPDOC']:
		continue
	defenv.DistributeDoc(d)

defenv.DistributeConf('nsisconf.nsh')

######################################################################
#######  Stubs                                                     ###
######################################################################

def BuildStub(compression, solid, unicode):

	suffix = ''
	if solid:
		suffix = '_solid'
	if unicode:
		env = stub_uenv.Clone()
	else:
		env = stub_env.Clone()

	suffix = suffix + '-' + GetArcSuffix(env, unicode)

	AddEnvStandardFlags(env, entry='NSISWinMainNOCRT')

	build_dir = '$BUILD_PREFIX/stub_%s%s' % (compression, suffix)

	exports = { 'env' : env, 'compression' : compression, 'solid_compression' : solid }

	target = defenv.SConscript(dirs = 'Source/exehead', variant_dir = build_dir, duplicate = False, exports = exports)
	env.SideEffect('%s/stub_%s.map' % (build_dir, stub), target)

	env.SetTargetPEMinOS(target)
	env.MakeReproducible(target)
	env.DistributeStubs(target, names=compression+suffix)

	defenv.Alias(compression, target)
	defenv.Alias('stubs', target)

for stub in stubs:
	if stub in defenv['SKIPSTUBS']:
		continue

	if defenv['UNICODE']:
		BuildStub(stub, False, True)
		BuildStub(stub, True, True)
	
	if GetArcCPU(defenv)=='x86':
		BuildStub(stub, False, False)
		BuildStub(stub, True, False)
	# BUGBUG64: Should build x86 stubs on x64?

defenv.DistributeStubs('Source/exehead/uninst.ico',names='uninst')

######################################################################
#######  makensis                                                  ###
######################################################################

build_dir = '$BUILD_PREFIX/makensis'
exports = { 'env' : makensis_env }

makensis = defenv.SConscript(dirs = 'Source', variant_dir = build_dir, duplicate = False, exports = exports)

makensis_env.SideEffect('%s/makensis.map' % build_dir, makensis)

defenv.MakeReproducible(makensis)
defenv.Alias('makensis', makensis)

if defenv['PLATFORM'] == 'win32': 
	defenv.DistributeW32Bin(makensis, alias='install-compiler')
else:
	defenv.DistributeBin(makensis, alias='install-compiler')

######################################################################
#######  Plug-ins                                                  ###
######################################################################

def PerformPluginExtrasDistOperationOnce(env, unicode):
	#SCons does not like it if you install the same file multiple times
	return GetArcCPU(defenv)==GetArcCPU(env) and (defenv['UNICODE']==unicode)

def BuildPluginWorker(target, source, libs, examples = None, docs = None,
                entry = 'DllMain', res = None, resources = None,
                defines = None, flags = None, nodeflib = True,
                cppused = False, unicode = False):
	basename = target
	if unicode:
		env = plugin_uenv.Clone()
	else:
		env = plugin_env.Clone()

	if cppused and env['CPP_REQUIRES_STDLIB']:
		nodeflib = False

	AddEnvStandardFlags(env, defines, flags, libs, entry, nodeflib)

	AppendRES(env, source, res, resources)

	plugin = env.SharedLibrary(target, source)
	defenv.Alias(target, plugin)
	defenv.Alias('plugins', plugin)

	defenv.SetTargetPEMinOS(plugin)
	defenv.SetPESecurityFlags(plugin)
	defenv.MakeReproducible(plugin)
	defenv.Sign(plugin)

	CleanMap(env, plugin, target)

	for i in plugin:
		if str(i)[-4:].lower() == '.dll':
			plugin = i
			break
	env.DistributePlugin(plugin, arcsubpath = GetArcSuffix(env, unicode))
	
	if PerformPluginExtrasDistOperationOnce(env, unicode):	# only distribute extras once
		DistributeExtras(env, basename, examples, docs)

def BuildPlugin(target, source, libs, examples = None, docs = None,
                entry = 'DllMain', res = None, resources = None,
                defines = None, flags = None, nodeflib = True,
                cppused = False):
	unicodetarget = 'UNICODE' in exports['env']['CPPDEFINES']
	BuildPluginWorker(target, source, libs, examples, docs, entry, res, resources, defines, flags, nodeflib, cppused, unicodetarget)


for plugin in plugin_libs + plugins:
	if plugin in defenv['SKIPPLUGINS']:
		continue
	
	srcpath = 'Contrib/' + plugin
	build_dir = '$BUILD_PREFIX/' + plugin
	pvariants = []
	if GetArcCPU(defenv) == 'x86':
		pvariants += [{'e':plugin_env.Clone()}]
	if defenv['UNICODE']:
		pvariants += [{'e':plugin_uenv.Clone()}]
	for pvariant in pvariants:
		exports = {
		  'env' : pvariant['e'],
		  'BuildPlugin' : BuildPlugin, 'GetArcSuffix' : GetArcSuffix, 
		  'PerformPluginExtrasDistOperationOnce' : PerformPluginExtrasDistOperationOnce 
		}
		vdir = build_dir + '/' + GetArcSuffix(pvariant['e'])
		defenv.SConscript(dirs = srcpath, variant_dir = vdir, duplicate = False, exports = exports)



######################################################################
#######  Utilities                                                 ###
######################################################################

Import('AddZLib')

def BuildUtilEnv(defines = None, flags = None, libs = None,
                 entry = None, nodeflib = None,
                 cross_platform = False, cli = False):
	if not cross_platform:
		env = util_env.Clone()
		platform = 'win32'
	else:
		env = cp_util_env.Clone()
		platform = env['PLATFORM']

	if libs and 'z' in libs:
		libs.remove('z')
		AddZLib(env, platform)

	if platform == 'win32':
		if cli:
			env.Append(LINKFLAGS = env['SUBSYS_CON'])
		else:
			env.Append(LINKFLAGS = env['SUBSYS_WIN'])

	AddEnvStandardFlags(env, defines, flags, libs, entry, nodeflib)

	return env

def BuildUtil(target, source, libs, entry = None, res = None, 
              resources = None, defines = None, flags = None,
              nodeflib = False, file_name = '', path='', contrib = False,
              examples = None, docs = None, cross_platform = False,
              root_util = False, cli = False, noinstall = False):
	env = BuildUtilEnv(defines, flags, libs, entry, nodeflib, cross_platform, cli)

	AppendRES(env, source, res, resources)

	if file_name != '':
		target = "%s/%s" % (target, file_name)

	# make sure the environment suffix fits
	if env['PROGSUFFIX'] not in target:
		if '.' in target:
			env['PROGSUFFIX'] = target[target.rindex('.'):]

	util = env.Program(target, source)
	defenv.Alias(target, util)
	defenv.Alias('utils', util)

	if (str(target)[:7]).lower() == 'RegTool'.lower():
		defenv.SetTargetPEMinOS(util)
	defenv.MakeReproducible(util)
	defenv.Sign(util)

	CleanMap(env, util, target)

	if not noinstall:
		if contrib:
			ins = env.DistributeContrib(util, path=path, alias='install-utils')
		elif cross_platform and not env['PLATFORM'] == 'win32' or root_util and env['PLATFORM'] == 'win32':
			ins = env.DistributeBin(util, path=path, alias='install-utils')
		else:
			ins = env.DistributeW32Bin(util, path=path, alias='install-utils')
		
		DistributeExtras(env, target, examples, docs)

	return util

for util in utils:
	if util in defenv['SKIPUTILS']:
		continue

	path = 'Contrib/' + util
	build_dir = '$BUILD_PREFIX/' + util
	exports = {'BuildUtil' : BuildUtil, 'BuildUtilEnv' : BuildUtilEnv, 'env' : util_env, 'GetArcCPU' : GetArcCPU}

	defenv.SConscript(dirs = path, variant_dir = build_dir, duplicate = False, exports = exports)

######################################################################
#######  Documentation                                             ###
######################################################################

halibut = defenv.SConscript(
	dirs = 'Docs/src/bin/halibut',
	variant_dir = '$BUILD_PREFIX/halibut',
	duplicate = False,
	exports = {'env' : defenv.Clone()}
)

for doctype in defenv['DOCTYPES']:
	
	defenv.SConscript(
		dirs = 'Docs/src',
		variant_dir = '$BUILD_PREFIX/Docs/' + doctype,
		duplicate = False,
		exports = {'halibut' : halibut, 'env' : defenv.Clone(), 'build_doctype' : doctype}
	)

######################################################################
#######  Examples                                                  ###
######################################################################

defenv.SConscript(
	dirs = 'Examples',
	exports = {'env': defenv.Clone()}
)

######################################################################
#######  Includes                                                  ###
######################################################################

defenv.SConscript(
	dirs = 'Include',
	exports = {'env': defenv.Clone()}
)

######################################################################
#######  Miscellaneous                                             ###
######################################################################

for i in misc:
	if i in defenv['SKIPMISC']:
		continue

	defenv.SConscript(dirs = 'Contrib/%s' % i)

######################################################################
#######  Tests                                                     ###
######################################################################

# test code

build_dir = '$BUILD_PREFIX/tests'
exports = {'env' : test_env.Clone()}

defenv.SConscript(
	dirs = 'Source/Tests',
	duplicate = False,
	exports = exports,
	variant_dir = build_dir
)

defenv.Ignore('$BUILD_PREFIX', '$BUILD_PREFIX/tests')

# test scripts

test_scripts_env = defenv.Clone(ENV = os.environ) # env needed for some scripts
test_scripts_env['ENV']['NSISDIR'] = os.path.abspath(str(defenv['TESTDISTDIR']))
test_scripts_env['ENV']['NSISCONFDIR'] = os.path.abspath(str(defenv['TESTDISTDIR']))
test_scripts_env.PrependENVPath('PATH', os.path.abspath(str(defenv['TESTDISTDIR'])))

def test_scripts(target, source, env):
	from os import walk, sep

	instdir = source[0].path

	tdlen = len(env.subst('$TESTDISTDIR'))
	skipped_tests = env['SKIPTESTS'].split(',')
	ignored_tests = env['IGNORETESTS'].split(',')
	skipped_tests += ['Examples' + sep + 'AppGen.nsi'] # Never test this, not a real installer

	compiler = FindMakeNSIS(env, env.subst('$TESTDISTDIR'))

	for root, dirs, files in walk(instdir):
		for file in files:
			if file[-4:] == '.nsi':
				nsi = root + sep + file
				nsif = nsi[tdlen + 1:]

				if nsif in skipped_tests:
					continue

				if nsif in ignored_tests:
					cmd = env.Command(None, nsi, '-' + compiler + ' $SOURCE')
				else:
					cmd = env.Command(None, nsi, compiler + ' $SOURCE')
				AlwaysBuild(cmd)
				env.Alias('test-scripts', cmd)

	return None

test = test_scripts_env.Command('test-scripts.log', '$TESTDISTDIR', test_scripts)
test_scripts_env.Alias('test-scripts', test)

# test all

defenv.Alias('test', ['test-code', 'test-scripts'])
