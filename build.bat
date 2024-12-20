@echo off
vcvars64.bat
cmake --build build --config RelWithDebInfo
@echo on