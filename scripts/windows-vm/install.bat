@echo off
rem Runs once at the end of the unattended Windows setup (dockur/windows copies
rem the OEM folder to C:\OEM and starts this file). The log ends up in
rem C:\OEM\setup.log.
powershell -NoProfile -ExecutionPolicy Bypass -File C:\OEM\setup.ps1 > C:\OEM\setup.log 2>&1
