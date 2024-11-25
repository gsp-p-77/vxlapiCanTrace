# CAN-Trace

## Prerequisites

- Install VS-code
- Install VS-code extensions:
  - C/C++ intelli sense, debugging
  - C/C++ extension pack
  - CMake
  - CMake tools
  - Compiler toolchain:MSVC (ref. to https://code.visualstudio.com/docs/cpp/config-msv)
 
## Build
- Add folder to Workspace in VS-code
- Select Visual Studio Build Tools 2019 Release - amd64
- open terminal here and execute the commands below
- Copy vxlapi64.dll to your folder, which contains your build output (vxlapiCanTrace.exe)
- Execute vxlapiCanTrace.exe

mkdir build

cd build

cmake ..

cmake --build .





