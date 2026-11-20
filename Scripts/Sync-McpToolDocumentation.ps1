[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path $PSScriptRoot -Parent
$documentation = @{}
$declarations = [regex]::new('/\*\*(?<comment>(?:(?!\*/).)*)\*/\s*UFUNCTION\([^)]*\)\s*static\s+FString\s+(?<native>MCP_\w+)\s*\(', [System.Text.RegularExpressions.RegexOptions]::Singleline)
foreach ($headerPath in [System.IO.Directory]::EnumerateFiles((Join-Path $pluginRoot 'Source/MassBattleEditorMCP/Public'), '*.h', [System.IO.SearchOption]::AllDirectories)) {
    foreach ($declaration in $declarations.Matches([System.IO.File]::ReadAllText($headerPath))) {
        $comment = $declaration.Groups['comment'].Value
        if ($comment -notmatch '@ProductDocument') { continue }
        $lines = foreach ($line in ($comment -split '\r?\n')) {
            $text = $line -replace '^\s*\*\s?', ''
            if ($text -match '^\s*@ProductDocument\s*$') { continue }
            $text = $text -replace '@(?:brief|details)\s+', ''
            $text = $text -replace '@param\[in\]\s+(\w+)\s+', '${1}: '
            $text = $text -replace '@return\s+', 'Returns: '
            $text = $text -replace '@pre\s+', 'Before calling: '
            $text = $text -replace '@post\s+', 'After completion: '
            $text = $text -replace '@(?:see|note)\s+', ''
            $text -replace '@ref\s+', ''
        }
        $documentation[$declaration.Groups['native'].Value] = ($lines -join "`n").Trim()
    }
}
$serverPath = Join-Path $pluginRoot 'Resources/Python/MassBattleMcpServer.py'
$original = [System.IO.File]::ReadAllText($serverPath)
$updatedTools = [System.Collections.Generic.List[string]]::new()
$toolBlocks = [regex]::new('^@mcp\.tool\(\)\r?\nasync def (?<name>\w+)[\s\S]*?(?=^@mcp\.tool\(\)|^if __name__|\z)', [System.Text.RegularExpressions.RegexOptions]::Multiline)
$docstring = [regex]::new('"""[\s\S]*?"""')
$updated = $toolBlocks.Replace($original, [System.Text.RegularExpressions.MatchEvaluator]{ param($tool)
    $block = $tool.Value
    $commands = @([regex]::Matches($block, 'send_command\(\s*"(MCP_\w+)"') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    if ($commands.Count -ne 1 -or -not $documentation.ContainsKey($commands[0])) { return $block }
    $description = $documentation[$commands[0]]
    $body = '"""' + $description.Replace("`n", "`n    ") + '"""'
    $updatedTools.Add($tool.Groups['name'].Value)
    $docstring.Replace($block, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $body }, 1)
})
if ($Check) {
    if ($updated -cne $original) { throw 'MCP tool descriptions differ from the public API comments. Run Scripts/Sync-McpToolDocumentation.ps1.' }
} elseif ($updated -cne $original) {
    [System.IO.File]::WriteAllText($serverPath, $updated, [System.Text.UTF8Encoding]::new($false))
}
Write-Output "Synchronized $($updatedTools.Count) MCP tool descriptions from public API comments."
