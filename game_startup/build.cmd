@echo off
setlocal
set "root=%~dp0"
set "out=%root%build"
set "shared=%root%..\dlss_mfg_sm86"
set "mh=%shared%\vendor\minhook"
if not exist "%out%" mkdir "%out%"
set "vs=C:\Program Files\Microsoft Visual Studio\18\Community"
if defined VSINSTALLDIR set "vs=%VSINSTALLDIR%"
call "%vs%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "flags=/nologo /O2 /MT /Brepro /W4 /WX /wd4505 /EHsc"
for %%f in (buffer hook trampoline) do (
 cl /nologo /O2 /MT /Brepro /W3 /c /I"%mh%\include" /Fo"%out%\%%f.obj" "%mh%\src\%%f.c" >nul
 if errorlevel 1 exit /b 1
)
cl /nologo /O2 /MT /Brepro /W3 /c /I"%mh%\include" /Fo"%out%\hde64.obj" "%mh%\src\hde\hde64.c" >nul
if errorlevel 1 exit /b 1
set "hooks=%out%\buffer.obj %out%\hook.obj %out%\trampoline.obj %out%\hde64.obj"
cl %flags% /LD /I"%mh%\include" /Fo"%out%\game_startup.obj" "%root%src\game_startup.cpp" %hooks% /link /nologo /Brepro /OUT:"%out%\game_startup.dll" /IMPLIB:"%out%\game_startup.lib" kernel32.lib
if errorlevel 1 exit /b 1
cl %flags% /I"%mh%\include" /Fo"%out%\test_startup.obj" "%root%src\tests\test_startup.cpp" %hooks% /link /nologo /Brepro /OUT:"%out%\test_startup.exe" kernel32.lib
if errorlevel 1 exit /b 1
"%out%\test_startup.exe"
if errorlevel 1 exit /b 1
python "%root%src\generate_proxy.py"
if errorlevel 1 exit /b 1
set "proxy=%out%\proxies\winmm"
ml64 /nologo /c /Fo"%proxy%\proxy_stubs.obj" "%proxy%\proxy.asm" >nul
if errorlevel 1 exit /b 1
cl %flags% /DPROXY_RESOLVE_BY_NAME /c /I"%proxy%" /Fo"%proxy%\proxy_forward.obj" "%shared%\src\proxy_forward.cpp" >nul
if errorlevel 1 exit /b 1
cl %flags% /c /Fo"%proxy%\proxy_loader.obj" "%root%src\proxy_loader.cpp" >nul
if errorlevel 1 exit /b 1
link /nologo /Brepro /DLL /DEF:"%proxy%\proxy.def" /OUT:"%proxy%\winmm.dll" /IMPLIB:"%proxy%\winmm.lib" "%proxy%\proxy_loader.obj" "%proxy%\proxy_forward.obj" "%proxy%\proxy_stubs.obj" kernel32.lib
if errorlevel 1 exit /b 1
python "%root%src\tests\test_proxy.py"
if errorlevel 1 exit /b 1
if not exist "%out%\release" mkdir "%out%\release"
copy /y "%out%\game_startup.dll" "%out%\release\" >nul
copy /y "%proxy%\winmm.dll" "%out%\release\" >nul
copy /y "%root%config\game_startup.ini" "%out%\release\" >nul
copy /y "%root%README.md" "%out%\release\" >nul
if not exist "%root%games\immortals-of-aveum\mod\P3\Binaries\Win64" mkdir "%root%games\immortals-of-aveum\mod\P3\Binaries\Win64"
copy /y "%out%\game_startup.dll" "%root%games\immortals-of-aveum\mod\P3\Binaries\Win64\" >nul
copy /y "%proxy%\winmm.dll" "%root%games\immortals-of-aveum\mod\P3\Binaries\Win64\" >nul
copy /y "%root%games\immortals-of-aveum\game_startup.ini" "%root%games\immortals-of-aveum\mod\P3\Binaries\Win64\" >nul
echo Standalone startup DLL and winmm loader built; all startup tests passed.
exit /b 0
