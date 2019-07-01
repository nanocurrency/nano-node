# Build Instructions

## Windows
1. Install [Boost 1.67](https://sourceforge.net/projects/boost/files/boost-binaries/1.67.0/boost_1_67_0-msvc-14.1-64.exe/download)
2. Install [Qt 5.9.5 msvc2017 64-bit (open source version)](https://www.qt.io/download) 
3. Install [Git for Windows](https://git-scm.com/download/win)
4. Install [CMake](https://cmake.org/download/)
5. Install [Visual Studio 2017 Community](https://my.visualstudio.com/Downloads?q=visual%20studio%202017&wt.mc_id=o~msft~vscom~older-downloads) (or higher edition, if you have a valid license. eg. Professional or Enterprise) 
   * Select **Desktop development with C++**
   * Select the latest Windows 10 SDK
6. Download Source
   1. ```git clone https://github.com/nanocurrency/nano-node```
   2. ```cd nano-node```
   3. ```git submodule update --init --recursive```
7. Create a **build** folder inside nano-node (makes for easier cleaning of build)
   1. ```mkdir build``` 
   2. ```cd build``` (note, all subsequent commands should be run within the build folder)
8. Get redistributables
   1. From **Powershell** ```Invoke-WebRequest -Uri https://aka.ms/vs/15/release/vc_redist.x64.exe -OutFile .\vc_redist.x64.exe```
9. Generate the build configuration.   <BR/> <BR/>Replace **%CONFIGURATION%** with one of the following:  Release, RelWithDebInfo, Debug.   <BR/> <BR/>Replace **%NETWORK%** with one of the following: nano_beta_network, nano_live_network, nano_test_network.   <BR/> <BR/>Ensure the Qt, Boost, and Windows SDK paths to match your installation. <BR/> <BR/> ```cmake -DNANO_GUI=ON -DCMAKE_BUILD_TYPE=%CONFIGURATION% -DACTIVE_NETWORK=%NETWORK% -DQt5_DIR="C:\Qt\5.9.5\msvc2017_64\lib\cmake\Qt5" -DNANO_SIMD_OPTIMIZATIONS=TRUE -DBoost_COMPILER="-vc141" -DBOOST_ROOT="C:/local/boost_1_67_0" -DBOOST_LIBRARYDIR="C:/local/boost_1_67_0/lib64-msvc-14.1" -G "Visual Studio 15 2017 Win64" -DIPHLPAPI_LIBRARY="C:/Program Files (x86)/Windows Kits/10/Lib/10.0.17763.0/um/x64/iphlpapi.lib" -DWINSOCK2_LIBRARY="C:/Program Files (x86)/Windows Kits/10/Lib/10.0.17763.0/um/x64/WS2_32.lib" ..\.```
10. Build
    1. Open nano-node.sln in Visual Studio
    2. Build with Release x64 configuration (or whatever build configuration you specified in step 9)
11. Package up binaries ```cpack -G ZIP -C Release```