[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$cacheDirectory = Join-Path $env:GITHUB_WORKSPACE "scribus-source-cache"
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

$sources = @(
    @{
        Name = "scribus-1.7.4-windows-x64.exe"
        Url = "https://downloads.sourceforge.net/project/scribus/scribus-devel/1.7.4/scribus-1.7.4-windows-x64.exe"
        Sha256 = "53b9446319026c4575b53706d4adcf26f0aebb3bc74c41a0fdd5cf89fb535778"
    },
    @{
        Name = "scribus-1.7.4.7z"
        Url = "https://downloads.sourceforge.net/project/scribus/scribus-devel/1.7.4/scribus-1.7.4.7z"
        Sha256 = "2fa0a33ad9c97974a5062ce8d28ad3bfd89e84cb68130055a5d10790b694ff31"
    },
    @{
        Name = "freetype-2.15.0.tar.xz"
        Url = "https://downloads.sourceforge.net/project/freetype/freetype2/2.15.0/freetype-2.15.0.tar.xz"
        Sha256 = "93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5"
    }
)

foreach ($source in $sources) {
    $destination = Join-Path $cacheDirectory $source.Name
    if (-not (Test-Path $destination)) {
        $temporary = "$destination.part"
        if (Test-Path $temporary) { Remove-Item -Path $temporary -Force }
        Write-Host "Downloading $($source.Name) into the persistent workflow cache."
        & curl.exe --fail --location --retry 10 --retry-all-errors --retry-delay 10 --connect-timeout 30 --max-time 1800 $source.Url --output $temporary
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($source.Url)" }
        Move-Item -Path $temporary -Destination $destination -Force
    }
    $actual = (Get-FileHash -Path $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $source.Sha256) {
        throw "SHA-256 mismatch for $destination. Expected $($source.Sha256); got $actual."
    }
    Write-Host "Verified $($source.Name)"
}
