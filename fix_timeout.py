import re

filepath = r'C:\Users\TheunisvanNiekerk\Code\agent\src\openai_client.cpp'
with open(filepath, 'r', encoding='utf-8') as f:
    text = f.read()

old_block = """            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, 0, 0);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);"""

new_block = """            // Apply per-project timeout: 0 = infinite (default), otherwise send+receive in ms.
            const DWORD timeout_ms = request.model_timeout_seconds > 0
                ? static_cast<DWORD>(request.model_timeout_seconds * 1000)
                : 0;
            WinHttpSetTimeouts(static_cast<HINTERNET>(request_handle.get()), 0, 0, timeout_ms, timeout_ms);
            ApplyCertificateFingerprintBypass(static_cast<HINTERNET>(request_handle.get()), request.provider.tls_certificate_fingerprint);"""

# Only replace in functions that have 'request' parameter available
# The remaining occurrences are in CreateToolAwareCompletion, CreateSimpleCompletion, StreamToolAwareCompletion
# We'll replace the exact block.
text = text.replace(old_block, new_block)

with open(filepath, 'w', encoding='utf-8') as f:
    f.write(text)
print('Done replacing timeouts in openai_client.cpp')
