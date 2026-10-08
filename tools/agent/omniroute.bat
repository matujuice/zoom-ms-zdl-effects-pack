@echo off
rem Start Claude Code on the Windows PC through OmniRoute (local AI gateway with token compression).
rem First run: installs OmniRoute, starts it, opens the dashboard at http://localhost:20128
rem to add a provider (an Anthropic API key is the safe choice). Later runs just launch.
where omniroute >nul 2>&1 || npm install -g omniroute || exit /b 1
curl -s -o nul http://localhost:20128/api/health || start "OmniRoute" /min omniroute serve
timeout /t 5 /nobreak >nul
omniroute launch %*
