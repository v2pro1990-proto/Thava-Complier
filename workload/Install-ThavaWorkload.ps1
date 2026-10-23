param(
    [switch]$Uninstall,
    [switch]$PackageOnly
)

$ErrorActionPreference = "Stop"

$WorkloadRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot     = Split-Path -Parent $WorkloadRoot
$SdkDir       = Join-Path $env:LOCALAPPDATA "Programs\ThavaSDK"
$SdkBinDir    = Join-Path $SdkDir "bin"
$SdkMsBuild   = Join-Path $SdkDir "MSBuild"
$DistDir      = Join-Path $WorkloadRoot "dist"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Thava Programming Language - Visual Studio Workload & SDK " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if ($Uninstall) {
    Write-Host "[1/4] Removing user TextMate grammar & snippets..." -ForegroundColor Yellow
    $userTmDir = Join-Path $env:USERPROFILE ".vs\Extensions\thava"
    if (Test-Path $userTmDir) { Remove-Item $userTmDir -Recurse -Force }

    Write-Host "[2/4] Removing Visual Studio templates & snippets..." -ForegroundColor Yellow
    $docs = [Environment]::GetFolderPath('MyDocuments')
    foreach ($vsFolder in @("Visual Studio 18", "Visual Studio 2022")) {
        $base = Join-Path $docs $vsFolder
        if (Test-Path $base) {
            Get-ChildItem -Path $base -Filter "Thava*.zip" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force
            Get-ChildItem -Path $base -Filter "Thava.snippet" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force
        }
    }

    Write-Host "[3/4] Removing Thava SDK directory ($SdkDir)..." -ForegroundColor Yellow
    if (Test-Path $SdkDir) { Remove-Item $SdkDir -Recurse -Force }
    [Environment]::SetEnvironmentVariable("THAVA_SDK_DIR", $null, "User")

    Write-Host "[4/4] Uninstallation complete." -ForegroundColor Green
    exit 0
}

# ------------------------------------------------------------
# 1. Package ProjectTemplates, ItemTemplates, and VSIX
# ------------------------------------------------------------
Write-Host "[1/6] Packaging Visual Studio Templates and VSIX bundle..." -ForegroundColor Yellow

$distProj = Join-Path $DistDir "ProjectTemplates"
$distItem = Join-Path $DistDir "ItemTemplates"
if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
New-Item -ItemType Directory -Path $distProj -Force | Out-Null
New-Item -ItemType Directory -Path $distItem -Force | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem

function New-CleanZip([string]$sourceDir, [string]$zipPath) {
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($sourceDir, $zipPath)
}

New-CleanZip (Join-Path $WorkloadRoot "ProjectTemplates\ThavaConsoleApp") (Join-Path $distProj "ThavaConsoleApp.zip")
New-CleanZip (Join-Path $WorkloadRoot "ProjectTemplates\ThavaDynamicLibrary") (Join-Path $distProj "ThavaDynamicLibrary.zip")
New-CleanZip (Join-Path $WorkloadRoot "ItemTemplates\ThavaSourceFile") (Join-Path $distItem "ThavaSourceFile.zip")
New-CleanZip (Join-Path $WorkloadRoot "ItemTemplates\ThavaHeaderModule") (Join-Path $distItem "ThavaHeaderModule.zip")

# Stage VSIX contents
$vsixStage = Join-Path $DistDir "_vsix_stage"
New-Item -ItemType Directory -Path $vsixStage -Force | Out-Null
Copy-Item (Join-Path $WorkloadRoot "VSIX\*") $vsixStage -Recurse -Force

$vsixGrammars = Join-Path $vsixStage "Grammars\thava"
New-Item -ItemType Directory -Path $vsixGrammars -Force | Out-Null
Copy-Item (Join-Path $WorkloadRoot "TextMate\thava\*") $vsixGrammars -Recurse -Force

$vsixProjDir = Join-Path $vsixStage "ProjectTemplates\VC\Thava"
$vsixItemDir = Join-Path $vsixStage "ItemTemplates\VC\Thava"
New-Item -ItemType Directory -Path $vsixProjDir -Force | Out-Null
New-Item -ItemType Directory -Path $vsixItemDir -Force | Out-Null
Copy-Item (Join-Path $distProj "*.zip") $vsixProjDir -Force
Copy-Item (Join-Path $distItem "*.zip") $vsixItemDir -Force

$vsixFile = Join-Path $DistDir "Thava.VisualStudio.vsix"
New-CleanZip $vsixStage $vsixFile
Remove-Item $vsixStage -Recurse -Force

Write-Host "  -> Built VSIX package: $vsixFile" -ForegroundColor Green

if ($PackageOnly) {
    exit 0
}

# ------------------------------------------------------------
# 2. Locate or Build thavac.exe & Install Thava SDK
# ------------------------------------------------------------
Write-Host "[2/6] Installing Thava SDK to $SdkDir..." -ForegroundColor Yellow

New-Item -ItemType Directory -Path $SdkBinDir -Force | Out-Null
New-Item -ItemType Directory -Path $SdkMsBuild -Force | Out-Null

$compilerCandidates = @(
    (Join-Path $RepoRoot "thavac.exe"),
    (Join-Path $RepoRoot "bin\Release\thavac.exe"),
    (Join-Path $RepoRoot "bin\Debug\thavac.exe")
)

$foundCompiler = $null
foreach ($cand in $compilerCandidates) {
    if (Test-Path $cand) {
        $foundCompiler = $cand
        break
    }
}

if (-not $foundCompiler) {
    $srcCpp = Join-Path $RepoRoot "thavac.cpp"
    if (-not (Test-Path $srcCpp)) {
        $srcCpp = Join-Path $RepoRoot "src\thavac.cpp"
    }
    if ((Test-Path $srcCpp) -and (Get-Command g++ -ErrorAction SilentlyContinue)) {
        Write-Host "  -> Compiling thavac.exe from source using g++..." -ForegroundColor Cyan
        $incDir = Split-Path -Parent $srcCpp
        $outExe = Join-Path $SdkBinDir "thavac.exe"
        & g++ -O3 -std=c++17 "$srcCpp" "-I$incDir" -o "$outExe"
        if ($LASTEXITCODE -eq 0 -and (Test-Path $outExe)) {
            $foundCompiler = $outExe
        }
    }
}

if ($foundCompiler -and ($foundCompiler -ne (Join-Path $SdkBinDir "thavac.exe"))) {
    Copy-Item $foundCompiler (Join-Path $SdkBinDir "thavac.exe") -Force
}

if (Test-Path (Join-Path $SdkBinDir "thavac.exe")) {
    Write-Host "  -> Installed compiler: $(Join-Path $SdkBinDir 'thavac.exe')" -ForegroundColor Green
} else {
    Write-Host "  -> Warning: thavac.exe was not found in repo root. Place thavac.exe in $SdkBinDir." -ForegroundColor Yellow
}

# Build Thava.Build.Tasks.dll if needed
$taskCs  = Join-Path $WorkloadRoot "MSBuild\Thava.Build.Tasks.cs"
$taskDll = Join-Path $WorkloadRoot "MSBuild\Thava.Build.Tasks.dll"
if ((Test-Path $taskCs) -and (-not (Test-Path $taskDll) -or ((Get-Item $taskCs).LastWriteTime -gt (Get-Item $taskDll).LastWriteTime))) {
    $vswhereTmp = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhereTmp) {
        $vsDirTmp = (& $vswhereTmp -latest -property installationPath) | Select-Object -First 1
        if ($vsDirTmp) {
            $msbBin = Join-Path $vsDirTmp "MSBuild\Current\Bin"
            $refs = @(
                (Join-Path $msbBin "Microsoft.Build.Framework.dll"),
                (Join-Path $msbBin "Microsoft.Build.Utilities.Core.dll")
            )
            if ((Test-Path $refs[0]) -and (Test-Path $refs[1])) {
                if (Test-Path $taskDll) { Remove-Item $taskDll -Force }
                Add-Type -Path $taskCs -ReferencedAssemblies $refs -OutputAssembly $taskDll
            }
        }
    }
}

$deployedDll = Join-Path $SdkMsBuild "Thava.Build.Tasks.dll"
if (Test-Path $deployedDll) {
    Get-ChildItem $SdkMsBuild -Filter "Thava.Build.Tasks.dll.old*" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    try {
        Remove-Item $deployedDll -Force -ErrorAction Stop
    } catch {
        $oldName = "Thava.Build.Tasks.dll.old." + [Guid]::NewGuid().ToString("N")
        Rename-Item $deployedDll $oldName -Force -ErrorAction SilentlyContinue
    }
}
Copy-Item (Join-Path $WorkloadRoot "MSBuild\*") $SdkMsBuild -Force
Copy-Item (Join-Path $WorkloadRoot "VSIX\ThavaIcon.ico") (Join-Path $SdkDir "ThavaIcon.ico") -Force

# ------------------------------------------------------------
# 3. Configure Environment Variables (THAVA_SDK_DIR & PATH)
# ------------------------------------------------------------
Write-Host "[3/6] Configuring User Environment Variables (THAVA_SDK_DIR & PATH)..." -ForegroundColor Yellow

[Environment]::SetEnvironmentVariable("THAVA_SDK_DIR", $SdkDir, "User")
$env:THAVA_SDK_DIR = $SdkDir

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not $userPath) { $userPath = "" }
if (($userPath -split ';') -notcontains $SdkBinDir) {
    $newPath = if ($userPath.EndsWith(";") -or $userPath -eq "") { "$userPath$SdkBinDir" } else { "$userPath;$SdkBinDir" }
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    $env:Path = "$env:Path;$SdkBinDir"
    Write-Host "  -> Added $SdkBinDir to User PATH." -ForegroundColor Green
} else {
    Write-Host "  -> $SdkBinDir is already in User PATH." -ForegroundColor Green
}

# ------------------------------------------------------------
# 4. Install TextMate Grammar & Snippets for Visual Studio
# ------------------------------------------------------------
Write-Host "[4/6] Installing Thava TextMate Syntax Grammar & Code Snippets..." -ForegroundColor Yellow

$userTmDir = Join-Path $env:USERPROFILE ".vs\Extensions\thava"
if (Test-Path $userTmDir) { Remove-Item $userTmDir -Recurse -Force }
New-Item -ItemType Directory -Path $userTmDir -Force | Out-Null
Copy-Item (Join-Path $WorkloadRoot "TextMate\thava\*") $userTmDir -Recurse -Force
Write-Host "  -> Installed TextMate bundle to $userTmDir" -ForegroundColor Green

# Clear Visual Studio TextMate user cache so new grammar loads immediately
$vsLocalAppData = Join-Path $env:LOCALAPPDATA "Microsoft\VisualStudio"
if (Test-Path $vsLocalAppData) {
    Get-ChildItem -Path $vsLocalAppData -Filter "user.cache" -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

# ------------------------------------------------------------
# 5. Install Visual Studio Project/Item Templates & Snippets
# ------------------------------------------------------------
Write-Host "[5/6] Installing Project Templates, Item Templates & IntelliSense Snippets..." -ForegroundColor Yellow

$docs = [Environment]::GetFolderPath('MyDocuments')
$vsVersions = @("Visual Studio 18", "Visual Studio 2022")

foreach ($vsVer in $vsVersions) {
    $vsDocRoot = Join-Path $docs $vsVer
    if (-not (Test-Path $vsDocRoot)) {
        New-Item -ItemType Directory -Path $vsDocRoot -Force | Out-Null
    }

    $projTplDirs = @(
        (Join-Path $vsDocRoot "Templates\ProjectTemplates"),
        (Join-Path $vsDocRoot "Templates\ProjectTemplates\Visual C++ Project")
    )
    foreach ($ptDir in $projTplDirs) {
        New-Item -ItemType Directory -Path $ptDir -Force | Out-Null
        Copy-Item (Join-Path $distProj "*.zip") $ptDir -Force
    }

    $itemTplDirs = @(
        (Join-Path $vsDocRoot "Templates\ItemTemplates"),
        (Join-Path $vsDocRoot "Templates\ItemTemplates\Visual C++ Project")
    )
    foreach ($itDir in $itemTplDirs) {
        New-Item -ItemType Directory -Path $itDir -Force | Out-Null
        Copy-Item (Join-Path $distItem "*.zip") $itDir -Force
    }

    $snippetDir = Join-Path $vsDocRoot "Code Snippets\Visual C++\My Code Snippets"
    New-Item -ItemType Directory -Path $snippetDir -Force | Out-Null
    Copy-Item (Join-Path $WorkloadRoot "Snippets\Thava\Thava.snippet") $snippetDir -Force

    Write-Host "  -> Installed Templates & Snippets for $vsVer" -ForegroundColor Green
}

# Optional elevated install into VS BuildCustomizations if running as Admin
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsInstallPaths = & $vswhere -property installationPath
    foreach ($vsPath in $vsInstallPaths) {
        if ($vsPath -and (Test-Path $vsPath)) {
            foreach ($vcVer in @("v170", "v180")) {
                $bcDir = Join-Path $vsPath "MSBuild\Microsoft\VC\$vcVer\BuildCustomizations"
                if (Test-Path $bcDir) {
                    try {
                        Copy-Item (Join-Path $WorkloadRoot "MSBuild\*") $bcDir -Force -ErrorAction Stop
                        Write-Host "  -> Installed MSBuild BuildCustomizations into $bcDir" -ForegroundColor Green
                    } catch {
                        # Non-admin fallback is already handled via $(THAVA_SDK_DIR)\MSBuild and $(LocalAppData)\Programs\ThavaSDK\MSBuild
                    }
                }
            }
        }
    }
}

# ------------------------------------------------------------
# 6. Register File Extensions (.thv, .thvh, .thdl) in HKCU
# ------------------------------------------------------------
Write-Host "[6/6] Registering .thv, .thvh, and .thdl file associations..." -ForegroundColor Yellow

$iconPath = Join-Path $SdkDir "ThavaIcon.ico"
$classesRoot = "HKCU:\Software\Classes"

$fileTypes = @{
    ".thv"  = @{ ProgId = "Thava.SourceFile";  Desc = "Thava Source File" }
    ".thvh" = @{ ProgId = "Thava.HeaderFile";  Desc = "Thava Header Module" }
    ".thdl" = @{ ProgId = "Thava.DynamicLib";  Desc = "Thava Dynamic Library" }
}

foreach ($ext in $fileTypes.Keys) {
    $info = $fileTypes[$ext]
    $extKey = Join-Path $classesRoot $ext
    $progKey = Join-Path $classesRoot $info.ProgId
    $iconKey = Join-Path $progKey "DefaultIcon"

    New-Item -Path $extKey -Force | Out-Null
    Set-ItemProperty -Path $extKey -Name "(Default)" -Value $info.ProgId

    New-Item -Path $progKey -Force | Out-Null
    Set-ItemProperty -Path $progKey -Name "(Default)" -Value $info.Desc

    New-Item -Path $iconKey -Force | Out-Null
    Set-ItemProperty -Path $iconKey -Name "(Default)" -Value "$iconPath,0"
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  Thava Visual Studio Workload & SDK Installed Successfully! " -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  SDK Path        : $SdkDir"
Write-Host "  VSIX Package    : $vsixFile"
Write-Host "  Templates       : Thava Console Application, Thava Dynamic Library (.thdl)"
Write-Host "  Editor Support  : .thv, .thvh Syntax Highlighting & Snippets"
Write-Host "  MSBuild Support : Ctrl+Shift+B to Build, F5 to Run, Error List navigation"
Write-Host "============================================================" -ForegroundColor Green
