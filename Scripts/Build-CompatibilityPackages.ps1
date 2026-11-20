[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'Saved/CompatibilityPackages'),
    [ValidateSet('5.6', '5.7', '5.8')]
    [string[]]$EngineVersions = @('5.6', '5.7', '5.8')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$pluginRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'Sync-McpToolDocumentation.ps1')
$descriptorPath = Join-Path $pluginRoot 'MassBattleEditorMCP.uplugin'
$descriptorText = [System.IO.File]::ReadAllText($descriptorPath)
$pluginVersion = ($descriptorText | ConvertFrom-Json).VersionName
$sourceFiles = @($descriptorPath)
foreach ($directoryName in @('Source', 'Config', 'Content', 'Resources', 'Scripts', 'skills', 'Docs')) {
    $directoryPath = Join-Path $pluginRoot $directoryName
    if ([System.IO.Directory]::Exists($directoryPath)) {
        $sourceFiles += [System.IO.Directory]::EnumerateFiles($directoryPath, '*', [System.IO.SearchOption]::AllDirectories) | Where-Object {
            $_ -notmatch '[\\/](Binaries|Intermediate|Saved|__pycache__|\.pytest_cache|\.venv)[\\/]|\.(pyc|pyo|log|bak)$'
        }
    }
}
foreach ($fileName in @('README.md', 'zh.md', 'LICENSE')) {
    $filePath = Join-Path $pluginRoot $fileName
    if ([System.IO.File]::Exists($filePath)) { $sourceFiles += $filePath }
}
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
foreach ($engineVersion in $EngineVersions) {
    $archivePath = Join-Path $OutputDirectory "MassBattleEditorMCP-$pluginVersion-UE$engineVersion.zip"
    $archiveStream = [System.IO.File]::Open($archivePath, [System.IO.FileMode]::Create)
    $archive = [System.IO.Compression.ZipArchive]::new($archiveStream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($sourceFile in $sourceFiles) {
            $entryName = $sourceFile.Substring($pluginRoot.Length + 1).Replace('\', '/')
            if ($sourceFile -eq $descriptorPath) {
                $versionedDescriptor = [regex]::Replace($descriptorText, '("EngineVersion"\s*:\s*")[^"]+(")', '${1}' + $engineVersion + '.0$2')
                $entry = $archive.CreateEntry($entryName)
                $writer = [System.IO.StreamWriter]::new($entry.Open(), [System.Text.UTF8Encoding]::new($false))
                try { $writer.Write($versionedDescriptor) } finally { $writer.Dispose() }
            } else {
                [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $sourceFile, $entryName, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        }
    } finally {
        $archive.Dispose()
        $archiveStream.Dispose()
    }
    Write-Output $archivePath
}
