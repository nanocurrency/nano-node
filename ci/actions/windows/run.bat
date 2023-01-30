@echo off
set exit_code=0
set count=0

goto %RUN%

:test
if %count% equ 10 goto rpc_test
call %BUILD_TYPE%\core_test.exe
set core_code=%errorlevel%
set /a count=count+1
if %core_code% neq 0 goto test

:rpc_test
call %BUILD_TYPE%\rpc_test.exe
set rpc_code=%errorlevel%
echo Core Test %core_code%
echo RPC Test %rpc_code%

exit /B %core_code%