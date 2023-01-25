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
goto exit

:exit
exit /B %exit_code%
