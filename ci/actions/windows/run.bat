@echo off
setlocal

call %BUILD_TYPE%\core_test.exe
set core_code=%errorlevel%

call %BUILD_TYPE%\rpc_test.exe
set rpc_code=%errorlevel%

echo Core Test return code: %core_code%
echo RPC Test return code: %rpc_code%

if %core_code%==0 if %rpc_code%==0 (
    exit /b 0
) else (
    exit /b 1
)