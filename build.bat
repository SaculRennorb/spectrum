@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

REM gdi for patblt
SET LIBS=user32.lib Gdi32.lib Dsound.lib libfftw3-3.lib

mkdir build
pushd build
cl -nologo -Oi -GR- -EHa- -Zi -FC -diagnostics:column -I ..\deps\fftw\bin /std:c++20 -o ..\bin\spectrum.exe ..\src\main.cpp %LIBS% /link /LIBPATH:..\deps\fftw\bin
popd

copy deps\fftw\bin\libfftw3-3.dll bin\