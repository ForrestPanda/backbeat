@echo off
title BackBeat installer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-backbeat.ps1"
pause
