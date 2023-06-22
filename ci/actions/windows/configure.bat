@echo off
set exit_code=0

echo "BUILD TYPE %BUILD_TYPE%"
echo "RUN %RUN%"

cmake .. ^
  -Ax64 ^
  %NANO_TEST% ^
  %CI% ^
  %ROCKS_LIB% ^
  -DPORTABLE=1 ^
  -DQt5_DIR="c:\qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
  -DNANO_GUI=ON ^
  -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
  -DACTIVE_NETWORK=nano_%NETWORK_CFG%_network ^
  -DNANO_SIMD_OPTIMIZATIONS=TRUE ^
  -Dgtest_force_shared_crt=on

set exit_code=%errorlevel%
if %exit_code% neq 0 goto exit

:exit
exit /B %exit_code%
