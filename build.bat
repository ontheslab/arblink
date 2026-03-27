@echo off

set AMIGA_INC=C:\amiga-dev\targets\m68k-amigaos\include
set AMIGA_POSIX_INC=C:\amiga-dev\targets\m68k-amigaos\posix\include

echo.
echo ****************************************************************************
echo  ArbLink Build Script
echo ****************************************************************************

echo  Building arblink...
vc -I"%AMIGA_INC%" -I"%AMIGA_POSIX_INC%" -o arblink arblink.c doorlog.c door_config.c aedoor_bridge.c rlogin_client.c terminal_session.c
if errorlevel 1 goto fail

echo  Building rlogin...
vc -I"%AMIGA_INC%" -I"%AMIGA_POSIX_INC%" -o rlogin rlogin.c rlogin_client.c doorlog.c
if errorlevel 1 goto fail

echo.
echo  Output:
for %%F in (arblink rlogin) do echo    %%F  %%~zF bytes
echo.
echo ****************************************************************************
echo  Build OK
echo ****************************************************************************
exit /b 0

:fail
echo.
echo  *** BUILD FAILED ***
echo ****************************************************************************
exit /b 1
