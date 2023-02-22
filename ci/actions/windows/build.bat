@echo off
set exit_code=0

goto %RUN%

:test
cmake --build . ^
  --target core_test ^
  --config %BUILD_TYPE% ^
  -- /m:2
set exit_code=%errorlevel%
if %exit_code% neq 0 goto exit
cmake --build . ^
  --target rpc_test ^
  --config %BUILD_TYPE% ^
  -- /m:2
set exit_code=%errorlevel%
goto exit

:artifact
cmake --build . ^
  --target INSTALL ^
  --config %BUILD_TYPE% ^
  -- /m:2
set exit_code=%errorlevel%

echo "Packaging NSIS"
call "%cmake_path%\cpack.exe" -C %BUILD_TYPE%
echo "Packaging ZIP"
call "%cmake_path%\cpack.exe" -G ZIP -C %BUILD_TYPE%

goto exit

:exit
exit /B %exit_code%
