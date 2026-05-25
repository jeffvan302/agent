param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$cppPath = Join-Path $RepositoryRoot 'src\web_assets_default.cpp'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$utf8WithBom = [System.Text.UTF8Encoding]::new($true)

function Format-EmbeddedByteArray {
    param(
        [string]$Name,
        [string]$SourcePath
    )

    $sourceBytes = [System.IO.File]::ReadAllBytes($SourcePath)
    $bytes = [byte[]]::new($sourceBytes.Length + 1)
    [System.Array]::Copy($sourceBytes, $bytes, $sourceBytes.Length)

    $lines = [System.Collections.Generic.List[string]]::new()
    for ($offset = 0; $offset -lt $bytes.Length; $offset += 16) {
        $end = [Math]::Min($offset + 15, $bytes.Length - 1)
        $values = for ($index = $offset; $index -le $end; $index++) {
            '(char)0x{0:x2}' -f $bytes[$index]
        }
        $line = '  ' + ($values -join ', ')
        if ($end -lt $bytes.Length - 1) {
            $line += ','
        }
        $lines.Add($line)
    }

    return "const char $Name[] = {`n" +
        ($lines -join "`n") +
        "`n};"
}

$content = [System.IO.File]::ReadAllText($cppPath, $utf8NoBom)
$rawAssets = @(
    @{ Name = 'kIndexHtml'; Source = 'www\index.html' },
    @{ Name = 'kLoginHtml'; Source = 'www\login.html' },
    @{ Name = 'kChangePasswordHtml'; Source = 'www\change-password.html' }
)

foreach ($asset in $rawAssets) {
    $sourcePath = Join-Path $RepositoryRoot $asset.Source
    $sourceContent = [System.IO.File]::ReadAllText($sourcePath, $utf8NoBom)
    $replacement = "const char $($asset.Name)[] =`nR`"ASSET(" +
        $sourceContent +
        ")ASSET`";"
    $pattern = '(?s)const char ' + [Regex]::Escape($asset.Name) +
        '\[\] =\r?\nR"ASSET\(.*?\)ASSET";'
    if (-not [Regex]::IsMatch($content, $pattern)) {
        throw "Could not locate embedded raw asset $($asset.Name) in $cppPath."
    }
    $content = [Regex]::Replace(
        $content,
        $pattern,
        [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement },
        1)
}

$assets = @(
    @{ Name = 'kBaseCss'; Source = 'www\css\base.css'; Next = 'kAppJs' },
    @{ Name = 'kAppJs'; Source = 'www\js\app.js'; Next = 'kAutomationJs' },
    @{ Name = 'kAutomationJs'; Source = 'www\js\automation.js'; Next = 'kLoginJs' }
)

foreach ($asset in $assets) {
    $sourcePath = Join-Path $RepositoryRoot $asset.Source
    $replacement = Format-EmbeddedByteArray -Name $asset.Name -SourcePath $sourcePath
    $pattern = '(?s)const char ' + [Regex]::Escape($asset.Name) +
        '\[\] = \{\r?\n.*?\r?\n\};(?=\r?\nconst char ' +
        [Regex]::Escape($asset.Next) + '\[\])'
    if (-not [Regex]::IsMatch($content, $pattern)) {
        throw "Could not locate embedded byte array $($asset.Name) in $cppPath."
    }
    $content = [Regex]::Replace(
        $content,
        $pattern,
        [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement },
        1)
}

[System.IO.File]::WriteAllText($cppPath, $content, $utf8WithBom)
Write-Host 'Updated embedded byte-array web assets from www/.'
