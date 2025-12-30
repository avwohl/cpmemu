# PowerShell script to build MSIX package for Windows Store
# Requires: Windows 10 SDK (for makeappx.exe and signtool.exe)

param(
    [string]$Version = "1.0.0.0",
    [string]$Publisher = "CN=YourPublisher",
    [string]$CertPath = "",
    [string]$CertPassword = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

# Configuration
$ProjectRoot = (Get-Item $PSScriptRoot).Parent.Parent.FullName
$SrcDir = Join-Path $ProjectRoot "src"
$PackagingDir = $PSScriptRoot
$OutputDir = Join-Path $ProjectRoot "dist"
$StagingDir = Join-Path $OutputDir "staging"

Write-Host "Building CP/M Emulator MSIX Package" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"

# Create output directories
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path $StagingDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StagingDir "Assets") | Out-Null

# Build the application if not skipping
if (-not $SkipBuild) {
    Write-Host "`nBuilding cpmemu.exe..." -ForegroundColor Yellow
    Push-Location $SrcDir

    # Try CMake first
    if (Test-Path "CMakeLists.txt") {
        $BuildDir = Join-Path $SrcDir "build"
        New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
        Push-Location $BuildDir
        cmake .. -G "Visual Studio 17 2022" -A x64
        cmake --build . --config Release
        Pop-Location
        $ExePath = Join-Path $BuildDir "Release\cpmemu.exe"
    }
    # Fall back to MinGW make
    elseif (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        mingw32-make -f Makefile.win clean
        mingw32-make -f Makefile.win
        $ExePath = Join-Path $SrcDir "cpmemu.exe"
    }
    else {
        Write-Error "No build system found. Install CMake or MinGW."
        exit 1
    }

    Pop-Location

    if (-not (Test-Path $ExePath)) {
        Write-Error "Build failed - cpmemu.exe not found at $ExePath"
        exit 1
    }

    Write-Host "Build successful!" -ForegroundColor Green
}
else {
    $ExePath = Join-Path $SrcDir "cpmemu.exe"
    if (-not (Test-Path $ExePath)) {
        $ExePath = Join-Path $SrcDir "build\Release\cpmemu.exe"
    }
}

# Copy executable to staging
Write-Host "`nStaging files..." -ForegroundColor Yellow
Copy-Item $ExePath $StagingDir

# Update and copy manifest
$ManifestPath = Join-Path $PackagingDir "AppxManifest.xml"
$ManifestContent = Get-Content $ManifestPath -Raw
# Only replace the Version in the Identity element (not the xml declaration)
$ManifestContent = $ManifestContent -replace '(<Identity[^>]*Version=)"[^"]*"', "`$1`"$Version`""
$ManifestContent = $ManifestContent -replace 'Publisher="[^"]*"', "Publisher=`"$Publisher`""
$ManifestContent | Set-Content (Join-Path $StagingDir "AppxManifest.xml")

# Check for asset files or create placeholders
$AssetsDir = Join-Path $PackagingDir "Assets"
if (Test-Path $AssetsDir) {
    Copy-Item (Join-Path $AssetsDir "*") (Join-Path $StagingDir "Assets") -Recurse
}
else {
    Write-Host "WARNING: No Assets directory found. You need to create icon files:" -ForegroundColor Yellow
    Write-Host "  - Assets/StoreLogo.png (50x50)"
    Write-Host "  - Assets/Square44x44Logo.png (44x44)"
    Write-Host "  - Assets/Square150x150Logo.png (150x150)"
    Write-Host "  - Assets/Wide310x150Logo.png (310x150)"
    Write-Host "  - Assets/SmallTile.png (71x71)"
    Write-Host "  - Assets/LargeTile.png (310x310)"
    Write-Host ""
    Write-Host "Creating placeholder files for testing..." -ForegroundColor Yellow

    # Create simple placeholder PNG files (1x1 transparent)
    $placeholderPng = [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82)

    @("StoreLogo.png", "Square44x44Logo.png", "Square150x150Logo.png", "Wide310x150Logo.png", "SmallTile.png", "LargeTile.png") | ForEach-Object {
        [System.IO.File]::WriteAllBytes((Join-Path $StagingDir "Assets\$_"), $placeholderPng)
    }
}

# Find Windows SDK tools
$SdkPath = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$SdkVersions = Get-ChildItem $SdkPath -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } | Sort-Object Name -Descending
if ($SdkVersions.Count -eq 0) {
    Write-Error "Windows 10 SDK not found. Please install it from Visual Studio Installer."
    exit 1
}
$SdkBin = Join-Path $SdkVersions[0].FullName "x64"
$MakeAppx = Join-Path $SdkBin "makeappx.exe"
$SignTool = Join-Path $SdkBin "signtool.exe"

if (-not (Test-Path $MakeAppx)) {
    Write-Error "makeappx.exe not found at $MakeAppx"
    exit 1
}

# Create MSIX package
Write-Host "`nCreating MSIX package..." -ForegroundColor Yellow
$MsixPath = Join-Path $OutputDir "cpmemu-$Version.msix"

& $MakeAppx pack /d $StagingDir /p $MsixPath /o

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to create MSIX package"
    exit 1
}

Write-Host "MSIX package created: $MsixPath" -ForegroundColor Green

# Sign the package if certificate provided
if ($CertPath -and (Test-Path $CertPath)) {
    Write-Host "`nSigning package..." -ForegroundColor Yellow

    $SignArgs = @("sign", "/fd", "SHA256", "/f", $CertPath)
    if ($CertPassword) {
        $SignArgs += @("/p", $CertPassword)
    }
    $SignArgs += $MsixPath

    & $SignTool @SignArgs

    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to sign package. Package created but unsigned."
    }
    else {
        Write-Host "Package signed successfully!" -ForegroundColor Green
    }
}
else {
    Write-Host "`nNOTE: Package is not signed. For Windows Store submission, you need to sign it." -ForegroundColor Yellow
    Write-Host "Use: .\build-msix.ps1 -CertPath path\to\cert.pfx -CertPassword yourpassword"
}

# Cleanup staging
Remove-Item $StagingDir -Recurse -Force

Write-Host "`nBuild complete!" -ForegroundColor Cyan
Write-Host "Output: $MsixPath"
Write-Host ""
Write-Host "Next steps for Windows Store submission:" -ForegroundColor Cyan
Write-Host "1. Create proper icon assets in packaging\windows\Assets\"
Write-Host "2. Register at https://partner.microsoft.com/ as a developer"
Write-Host "3. Get a code signing certificate"
Write-Host "4. Sign the package with your certificate"
Write-Host "5. Submit via Partner Center or use: msstore publish"
