REM vswhere is required, install it via `choco install vswhere`
@echo off
setlocal enabledelayedexpansion

for /f "usebackq tokens=*" %%i in (`vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
	"%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX12 || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX11 || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX10 || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX9 || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX8 || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX11-AVX || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX10-AVX || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX9-AVX || exit /b !errorlevel!
  "%%i" engine-vs2022.sln /m /p:Platform=x64 /p:Configuration=DX8-AVX || exit /b !errorlevel!
  pause
  exit /b !errorlevel!
)
