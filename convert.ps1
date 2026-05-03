$content = [System.IO.File]::ReadAllText('src/provider_manager.cpp', [System.Text.Encoding]::Unicode)
[System.IO.File]::WriteAllText('src/provider_manager.cpp.utf8', $content, [System.Text.Encoding]::UTF8)
