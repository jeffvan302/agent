import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw.startswith(b'\xff\xfe'):
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

# Move SelfManagedGate from after ResolveBindingTarget to before GateSlot::Acquire
# Extract the SelfManagedGate block
old = (
    "// Self-managed gate: no-op acquire/release for remote-provider-managed queues." + eol +
    "class SelfManagedGate {" + eol +
    "public:" + eol +
    "    static bool Acquire(const GateKey\u0026 key," + eol +
    "                        int /*effective_max_active*/," + eol +
    "                        int /*effective_max_queue*/," + eol +
    "                        const std::function\u003cvoid(const ProviderQueueStatus\u0026)\u003e\u0026 on_status) {" + eol +
    "        if (on_status) {" + eol +
    "            ProviderQueueStatus status;" + eol +
    "            status.provider_id = key.provider_id;" + eol +
    "            status.provider_name = key.provider_id;" + eol +
    "            status.state = \"active\";" + eol +
    "            status.queue_position = 0;" + eol +
    "            status.queue_depth = 0;" + eol +
    "            status.active_requests = 0;" + eol +
    "            status.max_active_requests = 0;" + eol +
    "            status.max_queue_size = 0;" + eol +
    "            on_status(status);" + eol +
    "        }" + eol +
    "        return true;" + eol +
    "    }" + eol +
    "    static void Release(const GateKey\u0026 /*key*/) { /* no-op */ }" + eol +
    "};"
)

# Remove from current location
if old in text:
    text = text.replace(old + eol + eol, "")
    print('Extracted SelfManagedGate')
else:
    print('WARN: SelfManagedGate block not found')
    write_file('src/openai_client.cpp', text, eol, bom)
    exit(1)

# Insert before GateSlot::Acquire
old_insert = "bool GateSlot::Acquire(const std::string\u0026 provider_name,"
new_insert = (
    "// Self-managed gate: no-op acquire/release for remote-provider-managed queues." + eol +
    "class SelfManagedGate {" + eol +
    "public:" + eol +
    "    static bool Acquire(const GateKey\u0026 key," + eol +
    "                        int /*effective_max_active*/," + eol +
    "                        int /*effective_max_queue*/," + eol +
    "                        const std::function\u003cvoid(const ProviderQueueStatus\u0026)\u003e\u0026 on_status) {" + eol +
    "        if (on_status) {" + eol +
    "            ProviderQueueStatus status;" + eol +
    "            status.provider_id = key.provider_id;" + eol +
    "            status.provider_name = key.provider_id;" + eol +
    "            status.state = \"active\";" + eol +
    "            status.queue_position = 0;" + eol +
    "            status.queue_depth = 0;" + eol +
    "            status.active_requests = 0;" + eol +
    "            status.max_active_requests = 0;" + eol +
    "            status.max_queue_size = 0;" + eol +
    "            on_status(status);" + eol +
    "        }" + eol +
    "        return true;" + eol +
    "    }" + eol +
    "    static void Release(const GateKey\u0026 /*key*/) { /* no-op */ }" + eol +
    "};" + eol + eol +
    "bool GateSlot::Acquire(const std::string\u0026 provider_name,"
)

if old_insert in text:
    text = text.replace(old_insert, new_insert)
    print('Inserted SelfManagedGate before GateSlot::Acquire')
else:
    print('WARN: insertion point not found')

write_file('src/openai_client.cpp', text, eol, bom)
print('Done')
