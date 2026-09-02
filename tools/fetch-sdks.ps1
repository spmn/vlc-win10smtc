# Downloads the VLC SDKs this plugin builds against into sdk\vlc3 and sdk\vlc4.
#
# Every import library the plugin links is rebuilt from its own export list as an MSVC short-form
# library: /DELAYLOAD needs that form, and x86 /SAFESEH rejects the MinGW long-form ones VLC 3
# ships. The .def written next to each library records what was linked against.

param(
    [ValidateSet('x64', 'x86', 'arm64')] [string[]] $Platform = @('x64', 'x86', 'arm64'),
    [ValidateSet('vlc3', 'vlc4')]        [string[]] $Version = @('vlc3', 'vlc4'),

    [string] $Vlc3Version = '3.0.23',   # release tag under download.videolan.org
    [string] $Vlc4Date = '20260908'     # nightly date; each target stamps its own time
)

$ErrorActionPreference = 'Stop'

$targets = @(
    @{ Platform = 'x64';   Name = 'win64';    LibDir = 'lib64';    Machine = 'X64'
       Nightly = 'nightly-win64-llvm' }
    @{ Platform = 'x86';   Name = 'win32';    LibDir = 'lib';      Machine = 'X86'
       Nightly = 'nightly-win32-llvm' }
    @{ Platform = 'arm64'; Name = 'winarm64'; LibDir = 'libarm64'; Machine = 'ARM64'
       Nightly = 'nightly-win64-arm-llvm' }
) | Where-Object { $_.Platform -in $Platform }

$libraries = @('libvlc', 'libvlccore')

$root = Split-Path -Parent $PSScriptRoot
$temp = Join-Path $env:TEMP 'vlc-win10smtc-sdks'
New-Item -ItemType Directory -Force -Path $temp | Out-Null

function Find-Tool([string] $Name)
{
    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    if ($Name -eq '7z')
    {
        $candidates = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432) |
            Where-Object { $_ } | ForEach-Object { Join-Path $_ '7-Zip\7z.exe' }
        $hit = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $hit) { throw '7z not found. Install 7-Zip or put it on PATH.' }
        return $hit
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "$Name not found and vswhere is missing." }
    $vs = & $vswhere -latest -products * -property installationPath

    # Prefer tools built for this host, but take any if the native ones are absent.
    $native = switch ($env:PROCESSOR_ARCHITECTURE)
    {
        'ARM64' { 'Hostarm64' }
        'x86'   { 'Hostx86' }
        default { 'Hostx64' }
    }
    $found = Get-ChildItem "$vs\VC\Tools\MSVC" -Filter "$Name.exe" -Recurse -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName -notmatch $native } | Select-Object -First 1
    if (-not $found) { throw "$Name not found under $vs." }
    return $found.FullName
}

$sevenZip = Find-Tool '7z'
$dumpbin = Find-Tool 'dumpbin'
$libTool = Find-Tool 'lib'

function Get-Text([string] $Url)
{
    $file = Join-Path $temp 'listing.tmp'
    Invoke-WebRequest $Url -OutFile $file -UseBasicParsing
    return Get-Content $file -Raw
}

# Each returns the archive URL, where its checksum lives, and the folder inside the archive.
function Resolve-Vlc3($target)
{
    $url = "https://download.videolan.org/pub/videolan/vlc/$Vlc3Version/$($target.Name)/vlc-$Vlc3Version-$($target.Name).7z"
    return @{ Url = $url; Sums = "$url.sha256"; Algo = 'SHA256'; Inner = "vlc-$Vlc3Version" }
}

function Resolve-Vlc4($target)
{
    $base = "https://artifacts.videolan.org/vlc/$($target.Nightly)"
    $stamp = [regex]::Matches((Get-Text "$base/"), "$Vlc4Date-\d{4}") |
        ForEach-Object { $_.Value } | Sort-Object -Unique | Select-Object -Last 1
    if (-not $stamp) { throw "no $($target.Nightly) nightly for $Vlc4Date; pick another date" }

    $sums = "$base/$stamp/SHA512SUM"
    $name = [regex]::Match((Get-Text $sums), "vlc-4[\w.-]*-$($target.Name)-\w+\.7z").Value
    if (-not $name) { throw "no $($target.Name) archive listed in $sums" }

    return @{ Url = "$base/$stamp/$name"; Sums = $sums; Algo = 'SHA512'; Inner = 'vlc-4.0.0-dev' }
}

# Rebuilds an import library from the export names in the one VLC shipped.
function Rebuild-ImportLibrary([string] $Path, [string] $Dll, [string] $Machine)
{
    $prefix = if ($Machine -eq 'X86') { '__imp__' } else { '__imp_' }
    $exports = [regex]::Matches((& $dumpbin /linkermember:1 $Path) -join "`n", "\b$prefix(\w+)") |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    if (-not $exports) { throw "no exports found in $Path" }

    $def = [IO.Path]::ChangeExtension($Path, '.def')
    Set-Content $def (@("LIBRARY $Dll", 'EXPORTS') + ($exports -replace '^', '    ')) -Encoding ascii
    & $libTool /nologo "/def:$def" "/machine:$Machine" "/out:$Path" | Out-Null
    return $exports.Count
}

foreach ($version in $Version)
{
    foreach ($target in $targets)
    {
        $info = & "Resolve-$version" $target
        $name = Split-Path $info.Url -Leaf
        Write-Host "$version $($target.Name) $name"

        # Keyed by version and target too: different nightlies publish the same file name.
        $archive = Join-Path $temp "$version-$($target.Name)-$name"
        if (-not (Test-Path $archive)) { Invoke-WebRequest $info.Url -OutFile $archive -UseBasicParsing }

        $line = ((Get-Text $info.Sums) -split "`n" | Select-String -SimpleMatch $name | Select-Object -First 1).Line
        $want = ($line -split '\s+' | Select-Object -First 1)
        $got = (Get-FileHash $archive -Algorithm $info.Algo).Hash
        if ($got -ne $want.ToUpperInvariant())
        {
            Remove-Item -Force $archive
            throw "$($info.Algo) mismatch for $name; discarded the download, run again"
        }

        $stage = Join-Path $temp "stage-$version-$($target.Name)"
        Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
        & $sevenZip x -y -bso0 -bsp0 "-o$stage" $archive "$($info.Inner)/sdk/*" | Out-Null

        $sdk = Join-Path $stage "$($info.Inner)\sdk"
        $dest = Join-Path $root "sdk\$version"
        $libDir = Join-Path $dest $target.LibDir
        Remove-Item -Recurse -Force $libDir -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Recurse -Force (Join-Path $sdk 'include') $dest
        Copy-Item -Recurse -Force (Join-Path $sdk 'lib') $libDir

        foreach ($library in $libraries)
        {
            $count = Rebuild-ImportLibrary (Join-Path $libDir "$library.lib") "$library.dll" $target.Machine
            Write-Host "    $library.lib rebuilt with $count exports"
        }
    }
}
