param(
    [Parameter(Mandatory)][ValidateSet('Prepare', 'Verify')][string]$Mode,
    [Parameter(Mandatory)][string]$Root,
    [string]$Catalog
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# A valid catalog signature on the runner is not necessarily available on a
# player's PC. Preserve only valid embedded Authenticode signatures.
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$files = @(Get-ChildItem -LiteralPath $rootPath -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @('.exe', '.dll', '.pyd') } |
    Sort-Object FullName)
if ($files.Count -eq 0) { throw "No Windows binaries found in $rootPath" }
$unsigned = [System.Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing linked binary: $($file.FullName)"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
    $embedded = $signature.SignatureType -eq 'Authenticode'
    if ($signature.Status -eq 'Valid' -and $embedded) { continue }
    if ($Mode -eq 'Verify') {
        throw "Missing valid embedded signature: $($file.FullName) ($($signature.Status), $($signature.SignatureType))"
    }
    if ($signature.Status -ne 'NotSigned' -and
        -not ($signature.Status -eq 'Valid' -and $signature.SignatureType -eq 'Catalog')) {
        throw "Invalid existing signature; investigate before signing: $($file.FullName) ($($signature.Status))"
    }
    $unsigned.Add($file.FullName)
}

if ($Mode -eq 'Prepare') {
    if (-not $Catalog) { throw 'Prepare requires -Catalog' }
    $catalogPath = [System.IO.Path]::GetFullPath($Catalog)
    $catalogDirectory = [System.IO.Path]::GetDirectoryName($catalogPath)
    [System.IO.Directory]::CreateDirectory($catalogDirectory) | Out-Null
    $relative = @($unsigned | ForEach-Object {
        [System.IO.Path]::GetRelativePath($catalogDirectory, $_)
    })
    [System.IO.File]::WriteAllLines($catalogPath, [string[]]$relative)
    if ($env:GITHUB_OUTPUT) {
        "unsigned-count=$($unsigned.Count)" | Out-File -FilePath $env:GITHUB_OUTPUT -Append -Encoding utf8
    }
    Write-Host "Found $($files.Count) binaries; $($unsigned.Count) need signing."
} else {
    Write-Host "Verified embedded signatures on $($files.Count) binaries."
}
