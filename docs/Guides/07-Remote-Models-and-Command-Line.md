# Remote Models and Command Line

## Remote Model Config

`Remote Model Config` creates a JSON configuration for running selected provider models from another Agent Desktop instance over an HTTPS remote worker.

Typical use:

1. Configure local/cloud providers and their models on the machine that will host the worker.
2. Click `Remote Model Config`.
3. Set the remote worker identity and network endpoint.
4. Generate a shared secret and a self-signed certificate, or load an existing worker JSON.
5. Check which provider models will be exported.
6. Save the JSON.
7. Run the worker from a command line using that JSON file.
8. On the consuming Agent Desktop instance, create an `Agent HTTPS Remote Worker` provider and use `Load Remote JSON`.

### Remote Model Config fields

| Field or action | Description |
| --- | --- |
| `Load JSON` | Load a prior worker definition. |
| `Save JSON` / `Save As` | Save the worker definition used by command-line worker execution and importing clients. |
| `Remote name` | Worker display name. |
| `Bind address` | Interface the worker listens on, default `0.0.0.0`. |
| `HTTPS port` | Worker serving port, default `8765`. |
| `Shared secret` and `Generate` | Bearer secret clients use for authentication. |
| `Certificate fingerprint` and `Generate Certificate` | Generates self-signed TLS material and exposes its trust fingerprint. |
| `Providers` | Existing providers available for export. |
| `Models to export` | Checked models served by the worker. |
| `Select All` / `Deselect All` | Toggle models for the selected provider. |
| `Preview JSON` | Inspect the configuration about to be saved. |

Binding models include their target definitions; when required, target provider/models are included for routing. An exported OpenAI OAuth provider may include its imported authentication record.

## Remote Worker Security

The worker JSON is sensitive. It can contain:

- Shared secret.
- Certificate private key material.
- Provider API keys.
- Imported OAuth access/refresh token material when an OAuth provider is exported.

Restrict file access, transmit it securely, and do not attach it to general project instructions or chat prompts.

## Configure a Client to Use a Remote Worker

1. Transfer only as securely as required the worker connection JSON or its connection values to the client administrator.
2. On the client desktop, open `Providers` and click `Add Provider`.
3. Choose `Agent HTTPS Remote Worker`.
4. Click `Load Remote JSON`, or enter endpoint, secret and certificate fingerprint.
5. Save and add/refresh its models.
6. Test the provider/model.
7. Assign the chosen remote model in `Project Settings`.

The Agent HTTPS provider uses the remote worker shared secret as bearer authentication and uses the certificate fingerprint for trust configuration.

## Command Line Options

Run these commands from a PowerShell or Command Prompt in the directory containing `agent.exe`, or use its full path.

| Option | Operation |
| --- | --- |
| `--web-config` | Opens the desktop Web Config dialog directly. |
| `--log-level error|warn|info|debug` | Sets logging verbosity for this invocation. |
| `--remote-worker-setup FILE` | Generates/updates shared secret and self-signed certificate values in a remote worker JSON file. |
| `--remote-worker FILE` | Runs the HTTPS remote provider worker defined in that JSON file. |
| `--ollama-setup FILE` | Installs/prepares Ollama and pulls the configured vision model for image ingestion. |
| `--ollama-remote FILE` | Runs remote image-ingestion Ollama endpoints described by the image-ingestion JSON settings. |
| `--help` or `-h` | Displays the available command options. |

The older misspelled aliases `--olama-setup` and `--olama-remote` are also accepted by the application.

### Examples

Open Web Config with debug logs:

```powershell
.\agent.exe --web-config --log-level debug
```

Prepare a worker JSON with worker secret/certificate material:

```powershell
.\agent.exe --remote-worker-setup C:\AgentConfig\worker.json
```

Run the HTTPS remote model worker:

```powershell
.\agent.exe --remote-worker C:\AgentConfig\worker.json
```

Prepare local Ollama for a saved image-ingestion configuration:

```powershell
.\agent.exe --ollama-setup C:\AgentConfig\rag_image_ingest_settings.json
```

Host the specified Ollama image-processing endpoints:

```powershell
.\agent.exe --ollama-remote C:\AgentConfig\rag_image_ingest_settings.json
```

## Remote Provider Worker Runtime

When `--remote-worker FILE` runs successfully, it exposes authenticated HTTPS operations including:

- `/health` for worker/model availability.
- `/v1/models` for listed exported models.
- Compatible chat routes for exported providers/models.

The worker listens on the configured bind address and HTTPS port. It routes requests to the configured exported providers, starting configured local Ollama processes where needed.

## Remote Image Ingestion Runtime

The `--ollama-setup` and `--ollama-remote` modes operate from image ingestion settings rather than general provider worker JSON:

1. Configure and save `Image Ingest Settings`, or prepare equivalent settings JSON.
2. Run `--ollama-setup FILE` on the machine responsible for the Ollama vision runtime.
3. Run `--ollama-remote FILE` to start the specified endpoints.
4. Configure image ingestion to use a reachable Ollama host/base URL, or preferably use a normal `Provider` entry if serving vision through an Agent HTTPS remote worker.

The application now directs Agent remote vision usage through normal Providers: add the remote worker as a provider, mark its model as vision capable, and select `Vision provider: Provider`.

## Failure Checks

| Problem | Check |
| --- | --- |
| Remote provider cannot connect | Confirm the worker command is running, bind address/port is reachable, firewall permits it, and shared secret matches. |
| TLS/certificate mismatch | Regenerate/import current worker JSON and confirm fingerprint on the client provider. |
| Model absent on client | Ensure it was checked in `Models to export`, saved to JSON, and refresh/add models on the client. |
| OAuth remote model fails | Confirm the host's OAuth provider is authenticated and protect/recreate worker export carefully because its auth record is embedded. |
| Remote image description unavailable | Use `Image Ingest Settings` -> `Provider` with a configured vision-capable remote provider, or verify Ollama endpoints and selected URL. |

