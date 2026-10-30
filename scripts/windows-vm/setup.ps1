# Prepares the Windows build VM of scripts/windows-vm.sh:
# OpenSSH server with the host's key, MSYS2 with the packages that
# scripts/package-windows.sh needs, and Inno Setup 6.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

function Step($text) { Write-Output "== $(Get-Date -Format s) $text" }

Step 'OpenSSH server'
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Set-Service -Name sshd -StartupType Automatic
Start-Service sshd
if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' `
        -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22
}
# The VM user is an administrator, so sshd reads this file instead of ~/.ssh.
$keys = 'C:\ProgramData\ssh\administrators_authorized_keys'
Copy-Item 'C:\OEM\authorized_keys' $keys -Force
icacls.exe $keys /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F'

Step 'MSYS2'
curl.exe -fsSL -o "$env:TEMP\msys2.sfx.exe" 'https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe'
& "$env:TEMP\msys2.sfx.exe" -y -oC:\
$bash = 'C:\msys64\usr\bin\bash.exe'
# The first login initializes the keyring; core updates need a second round.
& $bash -lc 'true'
& $bash -lc 'pacman -Syuu --noconfirm'
& $bash -lc 'pacman -Syuu --noconfirm'
& $bash -lc 'pacman -S --needed --noconfirm git make which diffutils autoconf automake libtool mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libtiff mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-pdf'

Step 'Inno Setup'
curl.exe -fsSL -o "$env:TEMP\innosetup.exe" 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe'
Start-Process "$env:TEMP\innosetup.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-' -Wait

Step 'done'
Set-Content -Path 'C:\OEM\ready' -Value (Get-Date -Format s)
