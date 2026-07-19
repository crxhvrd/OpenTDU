@echo off

pushd "third_party"
echo Building third-party dependencies...
call build_thirdparty.bat %1
popd

IF %ERRORLEVEL% NEQ 0 (
    echo "ThirdParty failed to build!"
    exit /b 1
)

echo Building Open Test Drive Unlimited...
IF "%~2"=="legacy" (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -A %1 -DOTDU_TARGET_WIN7=ON
) ELSE (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -A %1
)

pushd "build"
cmake --build . --config Release --parallel
popd

IF %ERRORLEVEL% NEQ 0 (
    echo "OpenTDU failed to build!"
    exit /b 1
)

exit /b 0
