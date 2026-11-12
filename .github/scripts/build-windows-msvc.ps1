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

$ScribusLibsDate = "20261026"
$ScribusLibsSha256 = "e870d0c7102180aa58400858d17f1d569e0de888b324c58c73f4fc6cbfe69a07"
$ScribusLibsUrl = "https://downloads.sourceforge.net/project/scribus/scribus-libs/scribus-1.7.x/scribus-1.7.x-libs-msvc-$ScribusLibsDate.7z"
$ScribusSourceUrl = "https://downloads.sourceforge.net/project/scribus/scribus-devel/$ScribusVersion/scribus-$ScribusVersion.tar.xz"
$QtRoot = $env:QT_ROOT_DIR

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$ToolArguments
    )

    Write-Host ">> $Executable $($ToolArguments -join ' ')"
    & $Executable @ToolArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE : $Executable"
    }
}

$RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
if ([string]::IsNullOrWhiteSpace($QtRoot) -or -not (Test-Path (Join-Path $QtRoot "bin\moc.exe"))) {
    throw "Qt MSVC tools were not found. QT_ROOT_DIR='$QtRoot'."
}
$requiredQtFiles = @(
    "lib\Qt6Core.lib",
    "lib\Qt6Concurrent.lib",
    "lib\Qt6Core5Compat.lib",
    "lib\Qt6Svg.lib"
)
foreach ($relativePath in $requiredQtFiles) {
    if (-not (Test-Path (Join-Path $QtRoot $relativePath))) {
        throw "Missing matching Qt library: $relativePath under $QtRoot"
    }
}

$msbuild = (Get-Command msbuild.exe -ErrorAction Stop).Source
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
$sevenZip = if ($sevenZipCommand) { $sevenZipCommand.Source } else { Join-Path $env:ProgramFiles "7-Zip\7z.exe" }
if (-not (Test-Path $sevenZip)) {
    throw "7-Zip is required to extract Scribus' MSVC dependency archive."
}

New-Item -ItemType Directory -Path $WorkspaceRoot -Force | Out-Null
$libsDirectory = Join-Path $WorkspaceRoot "libs"
New-Item -ItemType Directory -Path $libsDirectory -Force | Out-Null

$libPropsFiles = @(Get-ChildItem -Path $libsDirectory -Filter "scribus-lib-paths.props" -File -Recurse -ErrorAction SilentlyContinue)
if ($libPropsFiles.Count -eq 0) {
    $libsArchive = Join-Path $env:RUNNER_TEMP "scribus-1.7.x-libs-msvc-$ScribusLibsDate.7z"
    Invoke-External "curl.exe" @("--fail", "--location", "--retry", "4", $ScribusLibsUrl, "--output", $libsArchive)

    $actualHash = (Get-FileHash -Path $libsArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $ScribusLibsSha256) {
        throw "Scribus dependency archive SHA-256 mismatch. Expected $ScribusLibsSha256, got $actualHash."
    }

    Invoke-External $sevenZip @("x", "-y", "-o$libsDirectory", $libsArchive)
    $libPropsFiles = @(Get-ChildItem -Path $libsDirectory -Filter "scribus-lib-paths.props" -File -Recurse)
}
if ($libPropsFiles.Count -eq 0) {
    throw "The Scribus MSVC dependency archive did not contain scribus-lib-paths.props."
}
$libPropsFile = $libPropsFiles | Sort-Object { $_.FullName.Length } | Select-Object -First 1
$libRoot = $libPropsFile.Directory.FullName

$libsReadyMarker = Join-Path $libRoot ".photobookbrowser-msvc-v143-x64-release-ready"
if (-not (Test-Path $libsReadyMarker)) {
    $libSolutions = @(Get-ChildItem -Path $libsDirectory -Filter "*.sln" -File -Recurse | Sort-Object { $_.FullName.Length })
    if ($libSolutions.Count -eq 0) {
        throw "No Visual Studio solution was found in the Scribus dependency kit."
    }
    $libSolution = $libSolutions[0]
    Write-Host "Building Scribus dependency solution: $($libSolution.FullName)"
    Invoke-External $msbuild @(
        $libSolution.FullName,
        "/nologo",
        "/m:2",
        "/p:Configuration=Release",
        "/p:Platform=x64",
        "/p:PlatformToolset=v143",
        "/v:minimal"
    )
    Set-Content -Path $libsReadyMarker -Value "Release|x64|v143|$ScribusLibsDate" -Encoding ASCII
}
else {
    Write-Host "Using cached Scribus MSVC dependencies: $libRoot"
}

$sourceRoot = Join-Path $WorkspaceRoot "src"
if (Test-Path $sourceRoot) {
    Remove-Item -Path $sourceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
$sourceArchive = Join-Path $env:RUNNER_TEMP "scribus-$ScribusVersion.tar.xz"
Invoke-External "curl.exe" @("--fail", "--location", "--retry", "4", $ScribusSourceUrl, "--output", $sourceArchive)
Invoke-External "tar.exe" @("-xJf", $sourceArchive, "-C", $sourceRoot, "--strip-components=1")

$scribusSolution = Join-Path $sourceRoot "win32\msvc2022\Scribus.sln"
if (-not (Test-Path $scribusSolution)) {
    throw "The Scribus $ScribusVersion source archive does not contain win32\msvc2022\Scribus.sln."
}
$buildRoot = Join-Path $WorkspaceRoot "Scribus-builds"
$applicationOutput = Join-Path $buildRoot "Scribus-Release-x64-v143"
$scribusApiLibrary = Join-Path $applicationOutput "scribus-api.lib"

if (-not (Test-Path $scribusApiLibrary)) {
    Write-Host "Building the matching Scribus $ScribusVersion MSVC application target to produce scribus-api.lib."
    Invoke-External $msbuild @(
        $scribusSolution,
        "/nologo",
        "/t:scribus-main",
        "/m:2",
        "/p:Configuration=Release",
        "/p:Platform=x64",
        "/p:PlatformToolset=v143",
        "/p:SCRIBUS_LIB_ROOT=$libRoot",
        "/p:QT6_DIR=$QtRoot",
        "/v:minimal"
    )
}
if (-not (Test-Path $scribusApiLibrary)) {
    throw "Scribus did not produce the MSVC plugin import library: $scribusApiLibrary"
}

$pluginSourceDirectory = Join-Path $RepositoryRoot "plugin"
$scribusPluginDirectory = Join-Path $sourceRoot "scribus\plugins\photobookbrowser"
if (Test-Path $scribusPluginDirectory) {
    Remove-Item -Path $scribusPluginDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $scribusPluginDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $pluginSourceDirectory "*") -Destination $scribusPluginDirectory -Recurse -Force

$projectDirectory = Join-Path $sourceRoot "win32\msvc2022\photobookbrowser"
New-Item -ItemType Directory -Path $projectDirectory -Force | Out-Null
$templateProject = Join-Path $sourceRoot "win32\msvc2022\picturebrowser\picturebrowser.vcxproj"
$pluginProject = Join-Path $projectDirectory "photobookbrowser.vcxproj"
if (-not (Test-Path $templateProject)) {
    throw "Scribus' built-in Picture Browser MSVC project was not found: $templateProject"
}

$document = New-Object System.Xml.XmlDocument
$document.PreserveWhitespace = $true
$document.Load($templateProject)
$namespaceUri = "http://schemas.microsoft.com/developer/msbuild/2003"
$namespaces = New-Object System.Xml.XmlNamespaceManager($document.NameTable)
$namespaces.AddNamespace("msb", $namespaceUri)

$guidNode = $document.SelectSingleNode("//msb:PropertyGroup[@Label='Globals']/msb:ProjectGuid", $namespaces)
if (-not $guidNode) { throw "Could not locate ProjectGuid in Scribus' plugin template." }
$guidNode.InnerText = ([guid]::NewGuid()).ToString("B").ToUpperInvariant()

$rootNamespaceNode = $document.SelectSingleNode("//msb:PropertyGroup[@Label='Globals']/msb:RootNamespace", $namespaces)
if ($rootNamespaceNode) { $rootNamespaceNode.InnerText = "photobookbrowser" }
foreach ($targetName in $document.SelectNodes("//msb:TargetName", $namespaces)) {
    $targetName.InnerText = "photobookbrowser"
}

$userMacros = $document.SelectSingleNode("//msb:PropertyGroup[@Label='UserMacros']", $namespaces)
if (-not $userMacros) { throw "Could not locate the UserMacros property group in Scribus' plugin template." }
foreach ($existing in @($userMacros.SelectNodes("msb:QT6CONCURRENT_LIB", $namespaces))) {
    [void]$userMacros.RemoveChild($existing)
}
$debugConcurrent = $document.CreateElement("QT6CONCURRENT_LIB", $namespaceUri)
$debugConcurrent.SetAttribute("Condition", "'`$(Configuration)' == 'Debug'")
$debugConcurrent.InnerText = "Qt6Concurrentd.lib"
[void]$userMacros.AppendChild($debugConcurrent)
$releaseConcurrent = $document.CreateElement("QT6CONCURRENT_LIB", $namespaceUri)
$releaseConcurrent.SetAttribute("Condition", "'`$(Configuration)' == 'Release'")
$releaseConcurrent.InnerText = "Qt6Concurrent.lib"
[void]$userMacros.AppendChild($releaseConcurrent)

$obsoleteNodes = @(
    $document.SelectNodes("//msb:ClCompile[contains(@Include, 'picbrowser')]", $namespaces)
    $document.SelectNodes("//msb:ClCompile[contains(@Include, 'plugins_pch.cpp')]", $namespaces)
    $document.SelectNodes("//msb:ClInclude[contains(@Include, 'picbrowser')]", $namespaces)
    $document.SelectNodes("//msb:moc[contains(@Include, 'picbrowser')]", $namespaces)
    $document.SelectNodes("//msb:uic", $namespaces)
    $document.SelectNodes("//msb:ResourceCompile", $namespaces)
)
foreach ($node in $obsoleteNodes) {
    if ($node -and $node.ParentNode) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}

$targetsImport = $document.SelectSingleNode('//msb:Import[@Project="$(VCTargetsPath)\Microsoft.Cpp.targets"]', $namespaces)
if (-not $targetsImport) { throw "Could not locate Microsoft.Cpp.targets import in plugin template." }

$compileGroup = $document.CreateElement("ItemGroup", $namespaceUri)
$pluginSources = @(
    "photobookbrowserplugin.cpp",
    "photobookbrowserpalette.cpp",
    "photobookbrowsermodel.cpp",
    "photobookbrowserdelegate.cpp",
    "photobookbrowserfilterproxy.cpp",
    "photobookbrowserthumbnailloader.cpp"
)
foreach ($source in $pluginSources) {
    $item = $document.CreateElement("ClCompile", $namespaceUri)
    $item.SetAttribute("Include", "..\..\..\scribus\plugins\photobookbrowser\$source")
    [void]$compileGroup.AppendChild($item)
}
$pchItem = $document.CreateElement("ClCompile", $namespaceUri)
$pchItem.SetAttribute("Include", "..\..\..\scribus\plugins\plugins_pch.cpp")
foreach ($configuration in @("Debug|Win32", "Debug|x64", "Release|Win32", "Release|x64")) {
    $pchSetting = $document.CreateElement("PrecompiledHeader", $namespaceUri)
    $pchSetting.SetAttribute("Condition", "'`$(Configuration)|`$(Platform)'=='$configuration'")
    $pchSetting.InnerText = "Create"
    [void]$pchItem.AppendChild($pchSetting)
}
[void]$compileGroup.AppendChild($pchItem)
[void]$document.DocumentElement.InsertBefore($compileGroup, $targetsImport)

$mocGroup = $document.CreateElement("ItemGroup", $namespaceUri)
$pluginHeaders = @(
    "photobookbrowserplugin.h",
    "photobookbrowserpalette.h",
    "photobookbrowsermodel.h",
    "photobookbrowserdelegate.h",
    "photobookbrowserfilterproxy.h",
    "photobookbrowserthumbnailloader.h"
)
foreach ($header in $pluginHeaders) {
    $item = $document.CreateElement("moc", $namespaceUri)
    $item.SetAttribute("Include", "..\..\..\scribus\plugins\photobookbrowser\$header")
    [void]$mocGroup.AppendChild($item)
}
[void]$document.DocumentElement.InsertBefore($mocGroup, $targetsImport)

foreach ($dependencies in $document.SelectNodes("//msb:ItemDefinitionGroup/msb:Link/msb:AdditionalDependencies", $namespaces)) {
    if ($dependencies.InnerText -notmatch "QT6CONCURRENT_LIB") {
        $dependencies.InnerText = $dependencies.InnerText.Replace("%(AdditionalDependencies)", '$(QT6CONCURRENT_LIB);%(AdditionalDependencies)')
    }
}

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

Write-Host "Generated MSVC plugin project: $pluginProject"
Invoke-External $msbuild @(
    $pluginProject,
    "/nologo",
    "/m:2",
    "/p:Configuration=Release",
    "/p:Platform=x64",
    "/p:PlatformToolset=v143",
    "/p:SCRIBUS_LIB_ROOT=$libRoot",
    "/p:QT6_DIR=$QtRoot",
    "/v:minimal"
)

$pluginDll = Join-Path $buildRoot "Scribus-Release-x64-v143\plugins\photobookbrowser.dll"
if (-not (Test-Path $pluginDll)) {
    throw "The MSVC build did not produce photobookbrowser.dll at $pluginDll"
}

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Could not locate vswhere.exe to inspect the MSVC plugin." }
$vsInstall = & $vswhere -latest -products "*" -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsInstall) { throw "Could not locate the installed Visual Studio toolchain." }
$dumpbinCandidates = @(Get-ChildItem (Join-Path $vsInstall "VC\Tools\MSVC") -Filter "dumpbin.exe" -File -Recurse |
    Where-Object { $_.FullName -match "\\bin\\Hostx64\\x64\\dumpbin\.exe$" } |
    Sort-Object FullName -Descending)
if ($dumpbinCandidates.Count -eq 0) { throw "Could not locate the x64 MSVC dumpbin.exe." }
$dumpbin = $dumpbinCandidates[0].FullName

$headers = (& $dumpbin /headers $pluginDll | Out-String)
if ($LASTEXITCODE -ne 0 -or $headers -notmatch "8664 machine \(x64\)") {
    throw "The generated PhotoBook Browser DLL is not a valid x64 PE binary."
}
$dependents = (& $dumpbin /dependents $pluginDll | Out-String)
if ($LASTEXITCODE -ne 0) { throw "dumpbin could not inspect the plugin's import table." }
foreach ($requiredImport in @("Scribus.exe", "Qt6Core.dll", "Qt6Concurrent.dll")) {
    if ($dependents -notmatch [regex]::Escape($requiredImport)) {
        throw ("The plugin does not import expected host/runtime library '" + $requiredImport + "'. Import table:" + [Environment]::NewLine + $dependents)
    }
}
Write-Host "Verified x64 plugin imports:"
Write-Host $dependents

$distDirectory = Join-Path $RepositoryRoot "dist"
New-Item -ItemType Directory -Path $distDirectory -Force | Out-Null
$artifactName = "photobookbrowser-$PluginVersion-scribus-$ScribusVersion-windows-x64-msvc.dll"
$artifactPath = Join-Path $distDirectory $artifactName
Copy-Item -Path $pluginDll -Destination $artifactPath -Force
Write-Host "Windows MSVC plugin artifact: $artifactPath"
