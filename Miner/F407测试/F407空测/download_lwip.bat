 @echo off
 echo BTCMinerControl F407 — LwIP 下载脚本
 echo ========================================
 echo.
 echo 本脚本将下载 LwIP 2.2.1 源码并解压到 Library\LwIP\
 echo.
 
 set LWIP_URL=https://download.savannah.nongnu.org/releases/lwip/lwip-2.1.3.zip
 set LWIP_FILE=%TEMP%\lwip-2.1.3.zip
 set TARGET_DIR=%~dp0Library\LwIP
 
 echo 1. 下载 LwIP 2.1.3...
 echo 从: %LWIP_URL%
 echo.
 echo 备用下载地址:
 echo   GitHub:  https://github.com/lwip-tcpip/lwip/archive/refs/tags/v2.1.3.zip
 echo   Gitee:   https://gitee.com/mirrors/lwip/repository/archive/STABLE-2_2_1_RELEASE.zip
 echo   SourceForge: https://sourceforge.net/projects/lwip/files/lwip-2.1.3/
 echo.
 echo 如果自动下载失败，请手动下载并解压到 %TARGET_DIR%
 echo.
 echo 解压后应得到以下目录结构:
 echo   Library\LwIP\
 echo     ├── src\
 echo     │   ├── core\      (tcp.c, mem.c, ...)
 echo     │   ├── include\   (lwip/*.h, netif/*.h)
 echo     │   └── netif\     (ethernet.c)
 echo     └── ...
 echo.
 
 cd /d "%~dp0"
 
 rem 使用 curl 下载 (Windows 10/11 自带)
 where curl >nul 2>nul
 if %ERRORLEVEL%==0 (
     echo 正在下载，请稍候...
     curl -L -o "%LWIP_FILE%" "%LWIP_URL%"
     if !ERRORLEVEL!==0 goto :EXTRACT
 ) else (
     echo curl 不可用，尝试 PowerShell...
     powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%LWIP_URL%' -OutFile '%LWIP_FILE%' -TimeoutSec 120}"
     if !ERRORLEVEL!==0 goto :EXTRACT
 )
 
 echo 下载失败，请手动下载 LwIP 并解压到 Library\LwIP\
 pause
 goto :EOF
 
 :EXTRACT
 echo.
 echo 2. 解压到 Library\LwIP\...
 if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
 powershell -Command "& {Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory('%LWIP_FILE%', '%TARGET_DIR%')}"
 echo.
 echo 完成！请在 Keil 中添加以下源文件组:
 echo   1. 打开 Keil - Project - Manage - Run-Time Environment
 echo   2. 勾选 Network → Core 和 Network → Socket (TCP)
 echo   3. 或手动添加 src/core/*.c, src/core/ipv4/*.c, src/netif/ethernet.c
 echo.
 pause
