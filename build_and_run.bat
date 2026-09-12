@echo off
chcp 936 >nul
REM =====================================================================
REM  Godot Engine  -  Windows 一键编译并启动
REM  源码版本: 4.7.x    工具链: MSVC (Visual Studio 2022)
REM
REM  用法:
REM    build_and_run.bat                 增量编译并启动编辑器
REM    build_and_run.bat <项目路径>      编译后打开指定项目
REM    build_and_run.bat rebuild        清理后全量重编译
REM    build_and_run.bat clean          只清理构建产物
REM    build_and_run.bat release        release 版编辑器
REM    build_and_run.bat template       构建导出模板
REM    build_and_run.bat editor-only    跳过编译,直接启动已存在的编辑器
REM    build_and_run.bat build-only     只编译,不启动(供编辑器/CI 调用)
REM
REM  以上参数可组合,例如: build_and_run.bat rebuild build-only
REM
REM  可用环境变量覆盖:
REM    set JOBS=16                      并行编译线程数(默认=CPU 逻辑核心数)
REM    set SCONS_OPTS=scu_build=yes     追加额外 scons 选项
REM    set SKIP_DEVCMD=1                跳过 VsDevCmd,直接手工构造 MSVC 环境
REM
REM  首次使用前的环境准备:
REM    1) 安装 Visual Studio 2022(Community 或 Build Tools 均可),
REM       必须勾选「使用 C++ 的桌面开发」工作负载;
REM    2) 安装 Python 3.9+ 并执行:  python -m pip install scons
REM    3) 首次编译前安装 Godot 的 Windows 平台依赖(仅需一次):
REM         python misc\scripts\install_d3d12_sdk_windows.py
REM         python misc\scripts\install_accesskit.py
REM         python misc\scripts\install_angle.py
REM
REM  输出物: bin\godot.windows.editor.dev.x86_64.exe
REM =====================================================================
setlocal EnableExtensions

title Godot Engine - 编译并启动

REM ---------- 0. 基本配置 ----------
set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

if "%JOBS%"=="" set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

set "ARCH=x86_64"
set "BUILD_MODE=dev"
set "PROJECT_ARG="
set "DO_CLEAN="
set "NO_LAUNCH="

REM ---------- 1. 解析命令行参数 ----------
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="rebuild"     set "DO_CLEAN=1" & shift & goto parse_args
if /i "%~1"=="clean"       set "BUILD_MODE=clean" & shift & goto parse_args
if /i "%~1"=="release"     set "BUILD_MODE=release" & shift & goto parse_args
if /i "%~1"=="template"    set "BUILD_MODE=template" & shift & goto parse_args
if /i "%~1"=="editor-only" set "BUILD_MODE=skip" & shift & goto parse_args
if /i "%~1"=="build-only"  set "NO_LAUNCH=1" & shift & goto parse_args
if /i "%~1"=="--help"      goto show_usage
set "PROJECT_ARG=%~1"
shift
goto parse_args
:args_done

echo ============================================================
echo  Godot Engine 编译脚本
echo  源码目录 : %SRC_DIR%
echo  并行线程 : %JOBS%
echo ============================================================
echo.

cd /d "%SRC_DIR%"
if errorlevel 1 goto fail_cd

REM ---------- 2. 定位 Visual Studio (带 MSVC) ----------
set "VS_HOME="
set "PF=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" goto try_known
for /f "usebackq tokens=* delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if not defined VS_HOME set "VS_HOME=%%i"
)

:try_known
if defined VS_HOME goto vs_found
for %%e in (BuildTools Community Professional Enterprise) do call :probe_vs "%%e"
if not defined VS_HOME goto fail_vs

:vs_found
echo [1/4] MSVC 环境已就绪
goto load_msvc

:probe_vs
REM 路径含 "(x86)",不能放进 for ... in ( ) 的括号列表里解析,改用逐条判断
set "CAND=%PF86%\Microsoft Visual Studio\2022\%~1"
if exist "%CAND%\Common7\Tools\VsDevCmd.bat" if not defined VS_HOME set "VS_HOME=%CAND%"
set "CAND=%PF%\Microsoft Visual Studio\2022\%~1"
if exist "%CAND%\Common7\Tools\VsDevCmd.bat" if not defined VS_HOME set "VS_HOME=%CAND%"
goto :eof

:fail_vs
echo [错误] 没有检测到带 MSVC 的 Visual Studio 2022。
echo        请安装 Visual Studio 2022(Community 或 Build Tools),
echo        并在安装器中勾选「使用 C++ 的桌面开发」工作负载。
goto fail

:fail_cd
echo [错误] 无法进入源码目录
goto fail

REM ---------- 3. 载入 MSVC x64 环境 ----------
:load_msvc
set "DEVCMD=%VS_HOME%\Common7\Tools\VsDevCmd.bat"
if exist "%DEVCMD%" goto have_devcmd
echo [错误] 找不到 VsDevCmd.bat,Visual Studio 安装可能不完整。
goto fail
:have_devcmd

echo [2/4] 正在初始化 MSVC x64 编译环境...
if "%SKIP_DEVCMD%"=="1" goto skip_devcmd
call "%DEVCMD%" -arch=x64 -host_arch=x64 -no_logo
if defined INCLUDE goto env_ok
:skip_devcmd
echo       VsDevCmd 未生效,改用内置方式配置 MSVC 环境...
call :manual_env
if not defined INCLUDE goto fail_devcmd
:env_ok

REM ---------- 4. 定位 python / scons ----------
set "SCONS_CMD="
for /f "delims=" %%i in ('where scons 2^>nul') do if not defined SCONS_CMD set "SCONS_CMD=%%i"
if defined SCONS_CMD goto scons_ok

REM 注意: PATH 里的 "python" 可能是 WindowsApps 的 App Execution Alias 存根,
REM       直接调用会返回 9009。这里按真实安装目录逐个探测。
set "PY="
for %%v in (Python314 Python313 Python312 Python311) do call :probe_py "%%v"
if not defined PY if exist "C:\Python313\python.exe" set "PY=C:\Python313\python.exe"
if not defined PY goto fail_scons
for %%i in ("%PY%") do set "PYDIR=%%~dpi"
REM 优先用真正的 scons.exe 启动器,找不到再回退 "python -m SCons"。
if exist "%PYDIR%Scripts\scons.exe" set "SCONS_CMD="%PYDIR%Scripts\scons.exe""
if not defined SCONS_CMD set "SCONS_CMD="%PY%" -m SCons"

:scons_ok
echo [3/4] 构建工具已定位

REM ---------- 5. 组装 scons 命令 ----------
REM 关键点: Godot 的 SConstruct 使用 Environment(tools=[]),不会把外部
REM         INCLUDE / LIB 传给 cl.exe。VsDevCmd 设好的头文件与库路径必须
REM         通过 import_env_vars 显式导入,否则会报:
REM         "无法打开包括文件 ../um/windows.h" 或 "stddef.h: No such file"
set "IMPORT_VARS=INCLUDE,LIB,LIBPATH,PATH"
set "COMMON_OPTS=platform=windows arch=%ARCH% progress=no import_env_vars=%IMPORT_VARS% %SCONS_OPTS%"

if "%BUILD_MODE%"=="clean" goto do_clean
if "%BUILD_MODE%"=="skip" goto launch

if "%BUILD_MODE%"=="release" (
    set "TARGET_OPTS=target=editor dev_build=no optimize=speed debug_symbols=yes"
    set "BIN_NAME=godot.windows.editor.x86_64.exe"
) else if "%BUILD_MODE%"=="template" (
    set "TARGET_OPTS=target=template_debug"
    set "BIN_NAME=godot.windows.template_debug.dev.x86_64.exe"
) else (
    set "TARGET_OPTS=target=editor dev_build=yes debug_symbols=yes"
    set "BIN_NAME=godot.windows.editor.dev.x86_64.exe"
)

if not defined DO_CLEAN goto run_build
echo 正在清理旧产物以便全量重编译...
%SCONS_CMD% %COMMON_OPTS% --clean %TARGET_OPTS%

:run_build
echo.
echo [4/4] 开始编译, -j %JOBS% ...
echo 首次全量编译约需 10~20 分钟,之后为增量编译。
echo.
%SCONS_CMD% %COMMON_OPTS% -j %JOBS% %TARGET_OPTS%
if errorlevel 1 goto fail_build
echo 编译成功!
goto launch

:do_clean
echo 正在清理构建产物...
%SCONS_CMD% %COMMON_OPTS% --clean target=editor dev_build=yes debug_symbols=yes
echo 清理完成。
goto ok_exit

REM ---------- 6. 启动编辑器 ----------
:launch
if defined NO_LAUNCH goto ok_exit
echo.
echo 准备启动 Godot 编辑器...
if not defined BIN_NAME set "BIN_NAME=godot.windows.editor.dev.x86_64.exe"
set "GODOT_BIN=%SRC_DIR%\bin\%BIN_NAME%"

if exist "%GODOT_BIN%" goto have_bin
set "GODOT_BIN=%SRC_DIR%\bin\godot.windows.editor.x86_64.exe"
if not exist "%GODOT_BIN%" goto fail_bin

:have_bin
if "%BUILD_MODE%"=="template" goto ok_exit
echo 启动编辑器...
start "" "%GODOT_BIN%" --editor %PROJECT_ARG%
goto ok_exit

:ok_exit
echo.
echo 完成。
exit /b 0

REM ---------- 备用: 手动构造 MSVC x64 环境 ----------
REM 当 VsDevCmd.bat 因故未生效(例如受限环境拦截 reg.exe)时,用内置方式
REM 自动探测已安装的最新 MSVC 工具集与 Windows SDK 版本并拼出编译环境。
:manual_env
set "VCROOT=%VS_HOME%\VC\Tools\MSVC"
if not exist "%VCROOT%" goto :eof
set "VCVER="
for /f "delims=" %%d in ('dir /b /o-n "%VCROOT%" 2^>nul') do if not defined VCVER set "VCVER=%%d"
if not defined VCVER goto :eof
set "VCBIN=%VCROOT%\%VCVER%\bin\Hostx64\x64"
if not exist "%VCBIN%\cl.exe" set "VCBIN=%VCROOT%\%VCVER%\bin\HostX64\x64"
if not exist "%VCBIN%\cl.exe" goto :eof
set "KITS=%PF86%\Windows Kits\10"
if not exist "%KITS%" goto :eof
set "SDKVER="
for /f "delims=" %%d in ('dir /b /o-n "%KITS%\Include" 2^>nul') do if not defined SDKVER set "SDKVER=%%d"
if not defined SDKVER goto :eof
set "INCLUDE=%VCROOT%\%VCVER%\include;%KITS%\Include\%SDKVER%\ucrt;%KITS%\Include\%SDKVER%\um;%KITS%\Include\%SDKVER%\shared;%KITS%\Include\%SDKVER%\winrt;%KITS%\Include\%SDKVER%\cppwinrt"
set "LIB=%VCROOT%\%VCVER%\lib\x64;%KITS%\Lib\%SDKVER%\ucrt\x64;%KITS%\Lib\%SDKVER%\um\x64"
set "LIBPATH=%VCROOT%\%VCVER%\lib\x64"
set "PATH=%VCBIN%;%KITS%\bin\%SDKVER%\x64;%KITS%\bin\x64;%PATH%"
echo       已启用: MSVC %VCVER% + Windows SDK %SDKVER%
goto :eof

:show_usage
echo 用法: %~nx0 [rebuild^|clean^|release^|template^|editor-only^|build-only] [项目路径]
exit /b 0

:fail_devcmd
echo [错误] 初始化 MSVC 编译环境失败。
goto fail

:probe_py
if defined PY goto :eof
set "CAND=%LOCALAPPDATA%\Programs\Python\%~1\python.exe"
if exist "%CAND%" set "PY=%CAND%"
goto :eof

:fail_scons
echo [错误] 没有找到 Python / SCons。
echo        请安装 Python 3.9+ 后执行:  python -m pip install scons
goto fail

:fail_build
echo.
echo ============================================================
echo  [失败] 编译未通过
echo  排查建议:
echo    1. 向上滚动查看第一条 error 信息
echo    2. 确认 Windows SDK 已随 Visual Studio 一同安装
echo    3. 确认磁盘空间充足
echo    4. 若报 d3d12 / accesskit / angle 等依赖缺失,先运行:
echo         python misc\scripts\install_d3d12_sdk_windows.py
echo         python misc\scripts\install_accesskit.py
echo         python misc\scripts\install_angle.py
echo ============================================================
goto fail

:fail_bin
echo [错误] bin 目录下没有找到编辑器可执行文件,请先成功编译一次。
goto fail

:fail
echo.
echo ===== 执行未完成,按任意键关闭窗口 =====
pause >nul
exit /b 1
