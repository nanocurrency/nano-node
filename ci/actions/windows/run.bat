@echo off
setlocal
set exit_code=0

call %BUILD_TYPE%\core_test.exe
set core_code=%errorlevel%

call %BUILD_TYPE%\rpc_test.exe
set rpc_code=%errorlevel%

echo Core Test return code: %core_code%
echo RPC Test return code: %rpc_code%

if not %core_code%==0 (
    echo Core Test fail
    set exit_code=1
)

if not %rpc_code%==0 (
    echo RPC Test fail
    set exit_code=1
)

if %exit_code%==0 (
    echo Success
    exit /b 0
) else (
    echo Failed
    exit /b 1
)