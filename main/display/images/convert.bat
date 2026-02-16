@echo off
REM Convert image to RGB565 C headers for MimiClaw LCD
REM Generates both portrait (172x320) and landscape (320x172)
REM Usage: convert.bat your_image.png [name]
REM Example: convert.bat banner.png logo

if "%~1"=="" (
    echo Usage: convert.bat ^<image_file^> [name]
    echo Example: convert.bat banner.png logo
    exit /b 1
)

python "%~dp0convert.py" %*
