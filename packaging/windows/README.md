# Windows Build and Store Packaging

This directory contains files for building cpmemu on Windows and packaging it for the Microsoft Store.

## Prerequisites

### For Building
- **Option 1: Visual Studio 2022** with C++ Desktop Development workload
- **Option 2: MinGW-w64** (g++ with C++11 support)
- **CMake** 3.10+ (optional but recommended)

### For MSIX Packaging
- **Windows 10 SDK** (for makeappx.exe and signtool.exe)
- **Code signing certificate** (for Store submission)

## Building

### Using CMake (Recommended)
```powershell
cd src
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Using MinGW
```powershell
cd src
mingw32-make -f Makefile.win
```

### Using the original Makefile (Linux/WSL)
```bash
cd src
make
```

## Creating MSIX Package

Run the build script from this directory:

```powershell
.\build-msix.ps1
```

Options:
- `-Version "1.0.0.0"` - Set package version
- `-Publisher "CN=YourName"` - Set publisher identity
- `-CertPath "path\to\cert.pfx"` - Sign with certificate
- `-CertPassword "password"` - Certificate password
- `-SkipBuild` - Skip compilation (use existing exe)

## Required Assets

Before submitting to the Store, create these icon files in an `Assets` folder:

| File | Size | Description |
|------|------|-------------|
| StoreLogo.png | 50x50 | Store listing logo |
| Square44x44Logo.png | 44x44 | Taskbar icon |
| Square150x150Logo.png | 150x150 | Start menu tile |
| Wide310x150Logo.png | 310x150 | Wide tile |
| SmallTile.png | 71x71 | Small tile |
| LargeTile.png | 310x310 | Large tile |

## Windows Store Submission

### 1. Register as a Developer
- Go to [Partner Center](https://partner.microsoft.com/)
- Sign up as a Windows app developer (~$19 one-time fee)

### 2. Get a Code Signing Certificate
Options:
- Purchase from a CA (Comodo, DigiCert, etc.)
- Use Azure SignTool for cloud signing
- For testing only: create a self-signed certificate

### 3. Update Package Identity
Edit `AppxManifest.xml`:
- Set `Identity Name` to your reserved app name from Partner Center
- Set `Publisher` to match your certificate's subject
- Update `PublisherDisplayName` to your company/developer name

### 4. Submit
Using Microsoft Store Developer CLI:
```powershell
# Install CLI
winget install "Microsoft Store Developer CLI"

# Configure
msstore init

# Submit
msstore publish cpmemu-1.0.0.0.msix
```

Or upload manually through Partner Center.

## Testing Locally

To test the MSIX package locally before submission:

1. Create a self-signed certificate:
```powershell
New-SelfSignedCertificate -Type Custom -Subject "CN=TestPublisher" `
    -KeyUsage DigitalSignature -FriendlyName "Test Cert" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
```

2. Export to PFX and sign the package

3. Install the certificate to Trusted Root

4. Double-click the MSIX to install

## Troubleshooting

### "App manifest validation failed"
- Ensure all referenced asset files exist
- Check that Publisher matches your certificate exactly

### "Package unsigned"
- The Store requires signed packages
- For local testing, install your self-signed cert to Trusted Root

### Build errors on Windows
- Ensure you have C++11 support
- Check that Windows SDK is installed
- For MinGW: ensure g++ is in PATH
