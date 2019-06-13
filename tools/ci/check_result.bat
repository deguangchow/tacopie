@echo off

echo Input Param : %1

IF "%1" == "cpplint" (
   GOTO CPPLINT_CHECK
) ELSE (
    echo Supported Param: cpplint
)

exit /b 0

:CPPLINT_CHECK
echo ======== Check CppLint Result ========
for /f "delims==" %%a in (cpplint.log) do (
    echo %%a| FIND /I "error" && (
        for /f "tokens=2 delims=:" %%i in ("%%a") do (
            if not "%%i"==" 0" GOTO CPPLINT_FAIL
        )
    ) || (
      rem else do nothing
    )
)
echo ======== CppLint Result :OK   ========
exit /b 0

:CPPLINT_FAIL
echo ======== CppLint Result :FAIL ========
exit /b 1
