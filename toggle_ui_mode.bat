@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "PROPS=%ROOT%config\SynapticResynthesis-win.props"

if not exist "%PROPS%" (
  echo ERROR: Props file not found at:
  echo   "%PROPS%"
  exit /b 1
)

echo.
echo Select UI mode for SynapticResynthesis:
echo   [1] WebUI (WebView2)
echo   [2] IGraphics (NanoVG/GL2)
echo.
set "choice="
set /p "choice=Enter 1 or 2 then press Enter: "

if "%choice%"=="1" (
  set "TARGET=1"
  set "MODE=WebUI"
) else if "%choice%"=="2" (
  set "TARGET=0"
  set "MODE=IGraphics"
) else (
  echo Invalid choice: "%choice%". Aborting.
  exit /b 2
)

echo.
echo Updating SR_USE_WEB_UI to %TARGET% (%MODE%) in:
echo   "%PROPS%"
echo Creating backup...
copy /Y "%PROPS%" "%PROPS%.bak" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command "$path = '%PROPS%'; $val = '%TARGET%'; $content = Get-Content -LiteralPath $path -Raw; if ($content -notmatch '<SR_USE_WEB_UI>') { Write-Error 'SR_USE_WEB_UI tag not found'; exit 3 }; $pattern = '(?s)(<SR_USE_WEB_UI>\s*)[01](\s*</SR_USE_WEB_UI>)'; $new = [regex]::Replace($content, $pattern, { param($m) $m.Groups[1].Value + $val + $m.Groups[2].Value }); if ($new -ne $content) { Set-Content -LiteralPath $path -Value $new -Encoding UTF8; Write-Output ('Updated SR_USE_WEB_UI to ' + $val) } else { Write-Output 'No change made (already set).'}; if (-not (Select-String -InputObject $new -Pattern ('<SR_USE_WEB_UI>\s*' + [regex]::Escape($val) + '\s*</SR_USE_WEB_UI>'))) { exit 3 }"

if errorlevel 3 (
  echo ERROR: Failed to update the file. Check "%PROPS%.bak" to restore if needed.
  exit /b 3
)

echo Done.

exit /b 0


