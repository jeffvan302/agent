import re

with open('src/openai_client.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Fix 1: StreamChat
marker1 = '        result.success = true;\n        return result;\n    }\n\n    ChatExecutionResult result;\n    result.error = last_error.empty() ? \"The request failed after multiple attempts.\" : last_error;'
insert1 = '''        if (thinking_emitted && !thinking_closed) {
            const std::string close = "</think>\n\n";
            result.full_text += close;
        }

        result.success = true;'''

if marker1 in content:
    content = content.replace(marker1, insert1 + '\n        return result;\n    }\n\n    ChatExecutionResult result;\n    result.error = last_error.empty() ? "The request failed after multiple attempts." : last_error;', 1)
    print('Fixed StreamChat')
else:
    print('StreamChat marker not found')

# Fix 2: StreamToolAwareCompletion
marker2 = '            result.success = true;\n            result.tool_calls.clear();'
insert2 = '''            if (thinking_emitted && !thinking_closed) {
                const std::string close = "</think>\n\n";
                result.assistant_text += close;
            }

            result.success = true;'''

if marker2 in content:
    content = content.replace(marker2, insert2 + '\n            result.tool_calls.clear();', 1)
    print('Fixed StreamToolAwareCompletion')
else:
    print('StreamToolAwareCompletion marker not found')

with open('src/openai_client.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
