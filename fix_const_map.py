import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        text = raw.decode('utf-16-le')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = True
    else:
        text = raw.decode('utf-8')
        eol = '\r\n' if '\r\n' in text else '\n'
        bom = False
    return text, eol, bom

def write_file(path, text, eol, bom):
    if bom:
        b = text.encode('utf-16-le')
    else:
        b = text.encode('utf-8')
    with open(path, 'wb') as f:
        f.write(b)

text, eol, bom = read_file('src/openai_client.cpp')

# Fix const auto& -> auto& in EnumerateGates and GetGateState
old1 = (
    "std::vector\u003cGateStateSnapshot\u003e ProviderRequestGate::EnumerateGates() {" + eol +
    "    std::vector\u003cGateStateSnapshot\u003e result;" + eol +
    "    std::lock_guard\u003cstd::mutex\u003e map_lock(GateMapMutex());" + eol +
    "    const auto\u0026 map = GateMap();"
)
new1 = (
    "std::vector\u003cGateStateSnapshot\u003e ProviderRequestGate::EnumerateGates() {" + eol +
    "    std::vector\u003cGateStateSnapshot\u003e result;" + eol +
    "    std::lock_guard\u003cstd::mutex\u003e map_lock(GateMapMutex());" + eol +
    "    auto\u0026 map = GateMap();"
)
if old1 in text:
    text = text.replace(old1, new1)
    print('Fixed EnumerateGates const')
else:
    print('WARN: EnumerateGates old not found')

old2 = (
    "std::optional\u003cGateStateSnapshot\u003e ProviderRequestGate::GetGateState(const GateKey\u0026 key) {" + eol +
    "    std::lock_guard\u003cstd::mutex\u003e map_lock(GateMapMutex());" + eol +
    "    const auto\u0026 map = GateMap();"
)
new2 = (
    "std::optional\u003cGateStateSnapshot\u003e ProviderRequestGate::GetGateState(const GateKey\u0026 key) {" + eol +
    "    std::lock_guard\u003cstd::mutex\u003e map_lock(GateMapMutex());" + eol +
    "    auto\u0026 map = GateMap();"
)
if old2 in text:
    text = text.replace(old2, new2)
    print('Fixed GetGateState const')
else:
    print('WARN: GetGateState old not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done')
