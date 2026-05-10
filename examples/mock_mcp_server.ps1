param()

$stdin = [Console]::In
$stdout = [Console]::Out

function Send-Json($Object) {
    $json = $Object | ConvertTo-Json -Depth 20 -Compress
    $stdout.WriteLine($json)
    $stdout.Flush()
}

function New-ToolResultText($Text) {
    return @{
        content = @(
            @{
                type = "text"
                text = $Text
            }
        )
        isError = $false
    }
}

while ($true) {
    $line = $stdin.ReadLine()
    if ($null -eq $line) {
        break
    }

    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }

    try {
        $message = $line | ConvertFrom-Json -Depth 20
    } catch {
        continue
    }

    $method = $message.method
    $id = $message.id

    switch ($method) {
        "initialize" {
            Send-Json @{
                jsonrpc = "2.0"
                id = $id
                result = @{
                    protocolVersion = "2025-11-25"
                    capabilities = @{
                        tools = @{
                            listChanged = $false
                        }
                    }
                    serverInfo = @{
                        name = "mock-mcp-server"
                        title = "Mock MCP Server"
                        version = "1.0.0"
                    }
                }
            }
        }
        "tools/list" {
            Send-Json @{
                jsonrpc = "2.0"
                id = $id
                result = @{
                    tools = @(
                        @{
                            name = "echo_text"
                            title = "Echo Text"
                            description = "Returns the input text unchanged."
                            inputSchema = @{
                                type = "object"
                                properties = @{
                                    text = @{
                                        type = "string"
                                        description = "Text to echo back."
                                    }
                                }
                                required = @("text")
                            }
                        },
                        @{
                            name = "current_time"
                            title = "Current Time"
                            description = "Returns the current UTC time."
                            inputSchema = @{
                                type = "object"
                                properties = @{}
                            }
                        }
                    )
                }
            }
        }
        "tools/call" {
            $toolName = $message.params.name
            $arguments = $message.params.arguments

            switch ($toolName) {
                "echo_text" {
                    $text = ""
                    if ($arguments -and $arguments.text) {
                        $text = [string]$arguments.text
                    }
                    Send-Json @{
                        jsonrpc = "2.0"
                        id = $id
                        result = (New-ToolResultText "Echo: $text")
                    }
                }
                "current_time" {
                    Send-Json @{
                        jsonrpc = "2.0"
                        id = $id
                        result = (New-ToolResultText ("UTC time: " + [DateTime]::UtcNow.ToString("o")))
                    }
                }
                default {
                    Send-Json @{
                        jsonrpc = "2.0"
                        id = $id
                        result = @{
                            content = @(
                                @{
                                    type = "text"
                                    text = "Unknown tool: $toolName"
                                }
                            )
                            isError = $true
                        }
                    }
                }
            }
        }
        "ping" {
            Send-Json @{
                jsonrpc = "2.0"
                id = $id
                result = @{}
            }
        }
        default {
            if ($null -ne $id) {
                Send-Json @{
                    jsonrpc = "2.0"
                    id = $id
                    error = @{
                        code = -32601
                        message = "Method not found: $method"
                    }
                }
            }
        }
    }
}
