[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [string]$ScribusVersion = "1.7.4",
    [string]$PluginVersion = "0.0.0-ci",
    [string]$WorkspaceRoot = "C:\s"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# These checksums are published by the official Scribus SourceForge downloads.
$ScribusInstallerSha256 = "53b9446319026c4575b53706d4adcf26f0aebb3bc74c41a0fdd5cf89fb535778"
$ScribusSourceSha256 = "1f9e0b62e38a2ba89cc71d6764b1ec3114f24125b717e45cce7ea7d99ab3fe6b"
$ScribusInstallerUrl = "https://downloads.sourceforge.net/project/scribus/scribus-devel/$ScribusVersion/scribus-$ScribusVersion-windows-x64.exe"
$ScribusSourceUrl = "https://downloads.sourceforge.net/project/scribus/scribus-devel/$ScribusVersion/scribus-$ScribusVersion.tar.xz"
$QtRoot = $env:QT_ROOT_DIR

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$ToolArguments
    )

    Write-Host ">> $Executable $($ToolArguments -join ' ')"
    & $Executable @ToolArguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed with exit code $exitCode : $Executable"
    }
}

function Assert-Sha256 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Expected
    )

    $actual = (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected; got $actual."
    }
}

$RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
if ([string]::IsNullOrWhiteSpace($QtRoot) -or -not (Test-Path (Join-Path $QtRoot "bin\moc.exe"))) {
    throw "Qt MSVC tools were not found. QT_ROOT_DIR='$QtRoot'."
}
foreach ($relativePath in @(
    "lib\Qt6Core.lib",
    "lib\Qt6Concurrent.lib",
    "lib\Qt6Core5Compat.lib",
    "lib\Qt6Gui.lib",
    "lib\Qt6Widgets.lib",
    "lib\Qt6Xml.lib"
)) {
    if (-not (Test-Path (Join-Path $QtRoot $relativePath))) {
        throw "Required Qt library is missing: $relativePath under $QtRoot"
    }
}

$msbuild = (Get-Command msbuild.exe -ErrorAction Stop).Source
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
$sevenZip = if ($sevenZipCommand) { $sevenZipCommand.Source } else { "C:\Program Files\7-Zip\7z.exe" }
if (-not (Test-Path $sevenZip)) {
    throw "7-Zip is required to extract the official Scribus installer."
}

$vswhere = Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Could not locate Visual Studio Installer's vswhere.exe."
}
$vsInstall = & $vswhere -latest -products "*" -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsInstall) {
    throw "Could not locate the installed Visual Studio toolchain."
}
$msvcBinRoot = Join-Path $vsInstall "VC\Tools\MSVC"
$dumpbinCandidates = @(Get-ChildItem $msvcBinRoot -Filter "dumpbin.exe" -File -Recurse |
    Where-Object { $_.FullName -match "\\bin\\Hostx64\\x64\\dumpbin\.exe$" } |
    Sort-Object FullName -Descending)
$libCandidates = @(Get-ChildItem $msvcBinRoot -Filter "lib.exe" -File -Recurse |
    Where-Object { $_.FullName -match "\\bin\\Hostx64\\x64\\lib\.exe$" } |
    Sort-Object FullName -Descending)
if ($dumpbinCandidates.Count -eq 0 -or $libCandidates.Count -eq 0) {
    throw "Could not locate x64 MSVC dumpbin.exe and lib.exe."
}
$dumpbin = $dumpbinCandidates[0].FullName
$libTool = $libCandidates[0].FullName

New-Item -ItemType Directory -Path $WorkspaceRoot -Force | Out-Null
$hostSdkDirectory = Join-Path $WorkspaceRoot "host-sdk"
New-Item -ItemType Directory -Path $hostSdkDirectory -Force | Out-Null

# Extract the exact official Windows application. Its export table is the source
# of truth for the import library; this avoids rebuilding the entire Scribus app.
$installerPath = Join-Path $env:RUNNER_TEMP "scribus-$ScribusVersion-windows-x64.exe"
$installerExtract = Join-Path $env:RUNNER_TEMP "scribus-windows-extracted"
if (-not (Test-Path $installerPath)) {
    Invoke-External "curl.exe" @(
        "--fail", "--location", "--retry", "4", "--retry-all-errors",
        "--connect-timeout", "30", "--max-time", "900",
        $ScribusInstallerUrl, "--output", $installerPath
    )
}
Assert-Sha256 -Path $installerPath -Expected $ScribusInstallerSha256

if (Test-Path $installerExtract) {
    Remove-Item -Path $installerExtract -Recurse -Force
}
New-Item -ItemType Directory -Path $installerExtract -Force | Out-Null
Invoke-External $sevenZip @("x", "-y", "-o$installerExtract", $installerPath)

$scribusExeMatches = @(Get-ChildItem -Path $installerExtract -Filter "Scribus.exe" -File -Recurse)
if ($scribusExeMatches.Count -eq 0) {
    throw "Scribus.exe was not found in the extracted official Scribus $ScribusVersion installer."
}
$scribusExe = $scribusExeMatches[0].FullName
$hostHeaders = (& $dumpbin /headers $scribusExe | Out-String)
if ($LASTEXITCODE -ne 0 -or $hostHeaders -notmatch "8664 machine \(x64\)") {
    throw "The official Scribus executable is not an x64 PE binary."
}

Write-Host "Reading exported plugin API symbols from: $scribusExe"
$exportTable = (& $dumpbin /exports $scribusExe | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin could not read exports from the official Scribus executable."
}
$exportNameList = [System.Collections.Generic.List[string]]::new()
foreach ($line in ($exportTable -split "\r?\n")) {
    if ($line -match "^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(.+?)\s*$") {
        $candidate = $Matches[1].Trim()
        if ($candidate -and $candidate -notmatch "^\(") {
            [void]$exportNameList.Add($candidate)
        }
    }
}
$exportNames = @($exportNameList | Sort-Object -Unique)
if ($exportNames.Count -lt 10) {
    throw "The official Scribus executable export table contained only $($exportNames.Count) named exports; refusing to create an incomplete import library."
}
Write-Host "Found $($exportNames.Count) named Scribus API exports."

$definitionFile = Join-Path $hostSdkDirectory "Scribus.def"
$importLibrary = Join-Path $hostSdkDirectory "scribus-api.lib"
$definitionLines = @('LIBRARY "Scribus.exe"', "EXPORTS") + @($exportNames | ForEach-Object { "    $_" })
[System.IO.File]::WriteAllLines($definitionFile, $definitionLines, [System.Text.Encoding]::ASCII)
Invoke-External $libTool @(
    "/nologo",
    "/def:$definitionFile",
    "/machine:x64",
    "/out:$importLibrary"
)
if (-not (Test-Path $importLibrary)) {
    throw "MSVC did not create the Scribus executable import library."
}

# Fetch the matching release sources for Scribus headers and its native MSVC
# Picture Browser project template.
$sourceRoot = Join-Path $WorkspaceRoot "src"
if (Test-Path $sourceRoot) {
    Remove-Item -Path $sourceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
$sourceArchive = Join-Path $env:RUNNER_TEMP "scribus-$ScribusVersion.tar.xz"
Invoke-External "curl.exe" @(
    "--fail", "--location", "--retry", "4", "--retry-all-errors",
    "--connect-timeout", "30", "--max-time", "900",
    $ScribusSourceUrl, "--output", $sourceArchive
)
Assert-Sha256 -Path $sourceArchive -Expected $ScribusSourceSha256
Invoke-External "tar.exe" @("-xJf", $sourceArchive, "-C", $sourceRoot, "--strip-components=1")

$pluginSourceDirectory = Join-Path $RepositoryRoot "plugin"
$scribusPluginDirectory = Join-Path $sourceRoot "scribus\plugins\photobookbrowser"
New-Item -ItemType Directory -Path $scribusPluginDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $pluginSourceDirectory "*") -Destination $scribusPluginDirectory -Recurse -Force

$projectDirectory = Join-Path $sourceRoot "win32\msvc2022\photobookbrowser"
New-Item -ItemType Directory -Path $projectDirectory -Force | Out-Null
$templateProject = Join-Path $sourceRoot "win32\msvc2022\picturebrowser\picturebrowser.vcxproj"
$pluginProject = Join-Path $projectDirectory "photobookbrowser.vcxproj"
if (-not (Test-Path $templateProject)) {
    throw "Scribus' native Picture Browser MSVC project template was not found."
}

$document = New-Object System.Xml.XmlDocument
$document.PreserveWhitespace = $true
$document.Load($templateProject)
$namespaceUri = "http://schemas.microsoft.com/developer/msbuild/2003"
$namespaces = New-Object System.Xml.XmlNamespaceManager($document.NameTable)
$namespaces.AddNamespace("msb", $namespaceUri)

$guidNode = $document.SelectSingleNode("//msb:PropertyGroup[@Label='Globals']/msb:ProjectGuid", $namespaces)
if (-not $guidNode) { throw "The Visual Studio project template has no ProjectGuid." }
$guidNode.InnerText = ([guid]::NewGuid()).ToString("B").ToUpperInvariant()
$rootNamespaceNode = $document.SelectSingleNode("//msb:PropertyGroup[@Label='Globals']/msb:RootNamespace", $namespaces)
if ($rootNamespaceNode) { $rootNamespaceNode.InnerText = "photobookbrowser" }
foreach ($targetName in $document.SelectNodes("//msb:TargetName", $namespaces)) {
    $targetName.InnerText = "photobookbrowser"
}

# The upstream properties sheet points to a developer-specific dependency kit
# path. Remove it: this plugin only needs the official host import library and
# the exact Qt/MSVC libraries installed in this workflow.
$dependencyPropertyImports = @($document.SelectNodes("//msb:Import[contains(@Project, 'Scribus-build-props.props')]", $namespaces))
foreach ($node in $dependencyPropertyImports) {
    [void]$node.ParentNode.RemoveChild($node)
}

# The upstream Picture Browser uses a precompiled header and several unrelated
# dependencies. This plugin is built without PCH and links only what it uses.
foreach ($node in @(
    $document.SelectNodes("//msb:ClCompile[contains(@Include, 'picbrowser')]", $namespaces),
    $document.SelectNodes("//msb:ClCompile[contains(@Include, 'plugins_pch.cpp')]", $namespaces),
    $document.SelectNodes("//msb:ClInclude[contains(@Include, 'picbrowser')]", $namespaces),
    $document.SelectNodes("//msb:moc[contains(@Include, 'picbrowser')]", $namespaces),
    $document.SelectNodes("//msb:uic", $namespaces),
    $document.SelectNodes("//msb:ResourceCompile", $namespaces)
)) {
    if ($node -and $node.ParentNode) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}
foreach ($xpath in @(
    "//msb:ItemDefinitionGroup/msb:ClCompile/msb:PrecompiledHeader",
    "//msb:ItemDefinitionGroup/msb:ClCompile/msb:PrecompiledHeaderFile",
    "//msb:ItemDefinitionGroup/msb:ClCompile/msb:ForcedIncludeFiles"
)) {
    foreach ($node in @($document.SelectNodes($xpath, $namespaces))) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}

# Make QtConcurrent's headers visible and remove SDK-kit-specific link inputs.
foreach ($includeDirectory in $document.SelectNodes("//msb:ItemDefinitionGroup/msb:ClCompile/msb:AdditionalIncludeDirectories", $namespaces)) {
    if ($includeDirectory.InnerText -notmatch "QtConcurrent") {
        $includeDirectory.InnerText = '$(QT6_DIR)\include\QtConcurrent;' + $includeDirectory.InnerText
    }
}
foreach ($dependencies in $document.SelectNodes("//msb:ItemDefinitionGroup/msb:Link/msb:AdditionalDependencies", $namespaces)) {
    $dependencies.InnerText = 'Qt6Core.lib;Qt6Core5Compat.lib;Qt6Gui.lib;Qt6Widgets.lib;Qt6Xml.lib;Qt6Concurrent.lib;scribus-api.lib;%(AdditionalDependencies)'
}
foreach ($libraryDirectories in $document.SelectNodes("//msb:ItemDefinitionGroup/msb:Link/msb:AdditionalLibraryDirectories", $namespaces)) {
    $libraryDirectories.InnerText = '$(QT6_DIR)\lib;$(SCRIBUS_API_LIB_DIR);%(AdditionalLibraryDirectories)'
}

$targetsImport = $document.SelectSingleNode('//msb:Import[@Project="$(VCTargetsPath)\Microsoft.Cpp.targets"]', $namespaces)
if (-not $targetsImport) { throw "The Visual Studio template has no Microsoft.Cpp.targets import." }

$compileGroup = $document.CreateElement("ItemGroup", $namespaceUri)
$sources = @(
    "photobookbrowserplugin.cpp",
    "photobookbrowserpalette.cpp",
    "photobookbrowsermodel.cpp",
    "photobookbrowserdelegate.cpp",
    "photobookbrowserfilterproxy.cpp",
    "photobookbrowserthumbnailloader.cpp"
)
foreach ($source in $sources) {
    $item = $document.CreateElement("ClCompile", $namespaceUri)
    $item.SetAttribute("Include", "..\..\..\scribus\plugins\photobookbrowser\$source")
    [void]$compileGroup.AppendChild($item)
}
[void]$document.DocumentElement.InsertBefore($compileGroup, $targetsImport)

$mocGroup = $document.CreateElement("ItemGroup", $namespaceUri)
$headers = @(
    "photobookbrowserplugin.h",
    "photobookbrowserpalette.h",
    "photobookbrowsermodel.h",
    "photobookbrowserdelegate.h",
    "photobookbrowserfilterproxy.h",
    "photobookbrowserthumbnailloader.h"
)
foreach ($header in $headers) {
    $item = $document.CreateElement("moc", $namespaceUri)
    $item.SetAttribute("Include", "..\..\..\scribus\plugins\photobookbrowser\$header")
    [void]$mocGroup.AppendChild($item)
}
[void]$document.DocumentElement.InsertBefore($mocGroup, $targetsImport)

$writerSettings = New-Object System.Xml.XmlWriterSettings
$writerSettings.Encoding = New-Object System.Text.UTF8Encoding($true)
$writerSettings.Indent = $true
$writerSettings.IndentChars = "  "
$writer = [System.Xml.XmlWriter]::Create($pluginProject, $writerSettings)
try {
    $document.Save($writer)
}
finally {
    $writer.Dispose()
}

Write-Host "Generated x64 MSVC plugin project: $pluginProject"
$buildRoot = Join-Path $WorkspaceRoot "Scribus-builds"
Invoke-External $msbuild @(
    $pluginProject,
    "/nologo",
    "/m:2",
    "/p:Configuration=Release",
    "/p:Platform=x64",
    "/p:PlatformToolset=v143",
    "/p:SCRIBUS_CPP_STANDARD=202002",
    "/p:QT6_DIR=$QtRoot",
    "/p:SCRIBUS_API_LIB_DIR=$hostSdkDirectory",
    "/v:minimal"
)

$pluginDll = Join-Path $buildRoot "Scribus-Release-x64-v143\plugins\photobookbrowser.dll"
if (-not (Test-Path $pluginDll)) {
    throw "The MSVC build did not produce photobookbrowser.dll at $pluginDll"
}

$peHeaders = (& $dumpbin /headers $pluginDll | Out-String)
if ($LASTEXITCODE -ne 0 -or $peHeaders -notmatch "8664 machine \(x64\)") {
    throw "The generated PhotoBook Browser plugin is not a valid x64 PE DLL."
}
$imports = (& $dumpbin /dependents $pluginDll | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin could not inspect the plugin import table."
}
foreach ($requiredImport in @("Scribus.exe", "Qt6Core.dll", "Qt6Concurrent.dll", "Qt6Widgets.dll")) {
    if ($imports -notmatch [regex]::Escape($requiredImport)) {
        throw ("The plugin does not import expected host/runtime library '" + $requiredImport + "'. Imports:" + [Environment]::NewLine + $imports)
    }
}
$pluginExports = (& $dumpbin /exports $pluginDll | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin could not inspect the plugin's exported entry points."
}
foreach ($pluginEntryPoint in @("photobookbrowser_getPluginAPIVersion", "photobookbrowser_getPlugin", "photobookbrowser_freePlugin")) {
    if ($pluginExports -notmatch [regex]::Escape($pluginEntryPoint)) {
        throw "The generated DLL is missing Scribus plugin entry point '$pluginEntryPoint'."
    }
}

Write-Host "Verified x64 plugin imports:"
Write-Host $imports
Write-Host "Verified Scribus plugin entry points."
$distDirectory = Join-Path $RepositoryRoot "dist"
New-Item -ItemType Directory -Path $distDirectory -Force | Out-Null
$artifactName = "photobookbrowser-$PluginVersion-scribus-$ScribusVersion-windows-x64-msvc.dll"
$artifactPath = Join-Path $distDirectory $artifactName
Copy-Item -Path $pluginDll -Destination $artifactPath -Force
Write-Host "Windows MSVC plugin artifact: $artifactPath"
