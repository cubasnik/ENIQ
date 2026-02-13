param(
    [string]$src,
    [string]$dst
)

if (-not (Test-Path $src)) {
    Write-Error "Source file not found: $src"
    exit 1
}

$in = [System.IO.File]::OpenRead($src)
$out = [System.IO.File]::Create($dst)
$gz = New-Object System.IO.Compression.GzipStream($out,[System.IO.Compression.CompressionMode]::Compress)
try {
    $in.CopyTo($gz)
} finally {
    $gz.Close()
    $in.Close()
    $out.Close()
}
Write-Output "Created $dst"
