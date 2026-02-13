<#
PowerShell watcher: SFTP -> processing -> parse -> load

Assumptions:
- OpenSSH `scp` and `ssh` are available on PATH and key-based auth is configured for the SFTP/SSH host.
- `eniq_parser.exe` is built and available (set `$ParserExe`).
- A loader command (bulk loader) is configured in `$LoaderCmdTemplate`.

This script polls the remote folder, copies new .xml/.gz files to `ingest/incoming`, moves them to `ingest/processing` while parsing/loading,
and archives or moves failed files to `ingest/errors`.
#>

param(
    [int]$PollIntervalSeconds = 30
)

## Config - edit as needed
$SftpHost = "sftp.example.com"
$SftpUser = "user"
$RemotePath = "/remote/incoming"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$IngestDir = Join-Path $RepoRoot "ingest"
$IncomingDir = Join-Path $IngestDir "incoming"
$ProcessingDir = Join-Path $IngestDir "processing"
$ArchiveDir = Join-Path $IngestDir "archive"
$ErrorDir = Join-Path $IngestDir "errors"

# Path to the parser executable
$ParserExe = Join-Path $RepoRoot "build\Release\eniq_parser.exe"

# Template for loader command. Use {csv} and {db} placeholders.
$LoaderCmdTemplate = "echo 'LOADER: import {csv} into {db}'"

# Database path (for loader)
$DBPath = Join-Path $RepoRoot "eniq_data.db"

function Ensure-Dirs {
    foreach ($d in @($IngestDir, $IncomingDir, $ProcessingDir, $ArchiveDir, $ErrorDir)) {
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
    }
}

function Fetch-RemoteFiles {
    Write-Host "Fetching remote files from $SftpHost:$RemotePath..."
    # Try scp to copy xml and gz files. Requires key-based auth.
    $patterns = @("*.xml","*.gz")
    foreach ($p in $patterns) {
        $src = "$SftpUser@$SftpHost:`"$RemotePath/$p`""
        $cmd = "scp -q $src $IncomingDir 2>&1"
        Write-Host "-> $cmd"
        $out = cmd /c $cmd
        if ($LASTEXITCODE -ne 0) {
            # scp may fail if no matching files; ignore common errors
            Write-Verbose "scp exit=$LASTEXITCODE; output: $out"
        }
    }
    # After copying, move remote originals to processed subdir (best-effort)
    $remoteArchive = Join-Path $RemotePath "processed"
    $sshCmd = "ssh $SftpUser@$SftpHost \"mkdir -p '$remoteArchive' ; mv $RemotePath/*.xml $remoteArchive/ 2>/dev/null || true ; mv $RemotePath/*.gz $remoteArchive/ 2>/dev/null || true\""
    Write-Host "Remote rotate: $sshCmd"
    cmd /c $sshCmd | Out-Null
}

function Process-Files {
    Get-ChildItem -Path $IncomingDir -File | ForEach-Object {
        $src = $_.FullName
        $dest = Join-Path $ProcessingDir $_.Name
        try {
            Move-Item -Path $src -Destination $dest -Force
        } catch { Write-Warning "Failed move to processing: $_"; return }

        Write-Host "Processing $dest"
        # Determine out CSV path
        $csv = Join-Path $ProcessingDir ([IO.Path]::GetFileNameWithoutExtension($_.Name) + ".csv")
        # If gz, parser may accept .gz directly; otherwise pass the extracted file
        $parserCmd = "`"$ParserExe`" --in `"$dest`" --out-csv `"$csv`""
        Write-Host "Parser: $parserCmd"
        $pout = cmd /c $parserCmd
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Parser failed for $dest"
            Move-Item -Path $dest -Destination (Join-Path $ErrorDir $_.Name) -Force
            continue
        }

        # Run loader
        $loaderCmd = $LoaderCmdTemplate -replace '\{csv\}',$csv -replace '\{db\}',$DBPath
        Write-Host "Loader: $loaderCmd"
        $lout = cmd /c $loaderCmd
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Loader failed for $csv"
            Move-Item -Path $dest -Destination (Join-Path $ErrorDir $_.Name) -Force
            continue
        }

        # Success: archive original and csv
        Move-Item -Path $dest -Destination (Join-Path $ArchiveDir $_.Name) -Force
        Move-Item -Path $csv -Destination (Join-Path $ArchiveDir ([IO.Path]::GetFileName($csv))) -Force
        Write-Host "Archived $_.Name"
    }
}

Ensure-Dirs
Write-Host "Watcher started. Polling every $PollIntervalSeconds seconds. Press Ctrl-C to stop."
while ($true) {
    try {
        Fetch-RemoteFiles
        Process-Files
    } catch {
        Write-Error $_
    }
    Start-Sleep -Seconds $PollIntervalSeconds
}
