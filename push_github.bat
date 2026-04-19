@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo [VerveOS] Pushing main to origin (GitHub)...
git push origin main
if errorlevel 1 (
  echo.
  echo Push failed. This PC may not reach github.com:443 (firewall, proxy, or need VPN).
  echo Your commit is still saved locally. Run this .bat again when the network works.
  pause
  exit /b 1
)
echo.
echo Done. GitHub is up to date.
pause
