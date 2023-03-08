# Spectrum

A simple spectrum analyzer written in simple cpp.

I almost got myself to actually extract everything Windows related but couldn't be bothered to do it for DirectSound objects - maybe someday. 

## Compilation
You need to change the location of `vcvarsall.bat` in the `build.bat` files if you aren't running Visual Studio 2022.

1. `cd deps/fftw`
2. `call build.bat`
3. `cd ../..`
4. `call build.bat`
