@REM assumes we're being run from top-level directory as:
@REM win\build.bat

@ECHO OFF
SETLOCAL

@REM TODO: jump to Just32rel if %1 is 32rel, Just32dbg if %1 is 32dbg etc.

CALL win\vc32.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=rel
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=dbg
IF ERRORLEVEL 1 EXIT /B 1

CALL win\vc64.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=rel PLATFORM=X64
F ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=dbg PLATFORM=X64
IF ERRORLEVEL 1 EXIT /B 1
goto END

:Just32rel
CALL win\vc32.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=rel
IF ERRORLEVEL 1 EXIT /B 1
goto END

:Just32dbg
CALL win\vc32.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=dbg
IF ERRORLEVEL 1 EXIT /B 1
goto END

:Just64rel
CALL win\vc64.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=rel PLATFORM=X64
IF ERRORLEVEL 1 EXIT /B 1
goto END

:Just64dbg
CALL win\vc64.bat
IF ERRORLEVEL 1 EXIT /B 1

nmake -f win\makefile.msvc CFG=dbg PLATFORM=X64
IF ERRORLEVEL 1 EXIT /B 1
goto END

:END
