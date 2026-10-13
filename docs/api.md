# Data, security and USB contracts

## Cloud reads

The device requests `GET /api/v1/sources` every 10 seconds while connected. It sends the read token in the `Authorization: Bearer` header. It uses the ESP32 core's certificate bundle and hostname verification, requires a valid clock, and does not follow redirects. Tokens are never placed in URLs.

The response must contain `sources` and `serverTime`. Resources retain their metric definitions and order. Agent sources are grouped by `account.provider` and `account.id`. The newest observation supplies account measurements; equal timestamps use ascending ASCII source IDs. Account status considers every reporting project, so a newer working project cannot hide another project's input request or error. A provider can have multiple accounts.

Resources become `Stale` after 120 seconds without a receipt. Coding hooks report events rather than continuous heartbeats: after that boundary their status is neutral `Inactive`. Fresh errors and input requests remain actionable regardless of the original event's age. The device reevaluates each project's receipt and event age while offline. State labels, attention priorities and timers come from the pinned public App contract, shared with Web.

| Limit | Value |
| --- | --- |
| Reporting sources | 128 |
| Metrics per source | 32 |
| HTTP response body | 1 MiB |
| JSON allocation budget | 512 KiB |
| Visible metrics per card | First 3, following the Web order |
| Poll interval | 10 seconds |
| Retry delay for transient failures | 20, 40, then at most 60 seconds |
| Report stale boundary | 120 seconds since the server received it |

Missing measurements remain unavailable; they do not become zero. Invalid or incomplete snapshots do not replace the displayed catalog. Network failures retain the last accepted values. Rejected saved credentials stop automatic polling until the user retries or changes the key. A rejected replacement does not overwrite the previous saved connection. A verified server/key change clears old account selections before accepting the new workspace.

## Persistent settings

The `vibemon-touch` NVS namespace stores one versioned `config` value containing Wi-Fi credentials, server origin, key and four selections. One NVS write commits the complete configuration. Polling does not write flash. Selecting the same item or reconnecting with unchanged Wi-Fi credentials does not write flash. Leaving an in-progress setup form cancels it; a late cloud reply cannot save a canceled workspace.

The default firmware does not enable flash or NVS encryption. Physical access to the device or a flash backup can expose stored credentials. Use a dedicated read token and keep firmware backups private. The optional USB console is a local setup interface, not an authenticated network service.

The firmware has no inbound HTTP status server, captive portal or WebSocket connection. Collectors send reports to Web; the device only reads them.

## USB commands

Send one UTF-8 JSON object followed by LF at 115200 baud. Commands are limited to 1023 bytes before LF. Invalid and oversized commands return an error without echoing the input. At most 256 incoming bytes are processed per UI loop.

| Command | Fields | Result |
| --- | --- | --- |
| `info` | None | Version, board, memory, connection/page codes, item/selection counts, touch press count, completed frames and requested pixel clock; no credentials or names |
| `configure-wifi` | `ssid`, `password` | Validate and queue a connection. Save only after it connects. |
| `configure-cloud` | `origin`, `token` | Validate and queue an authenticated workspace read. Save only after it succeeds. |
| `dashboard` | None | Open the four-panel view. |
| `refresh` | None | Request a refresh, including an explicit retry after a rejected token. |
| `display-clock` | `hz`: 8000000, 12000000, 16000000, 20000000 or 26000000 | Local display diagnosis only. Change at a frame boundary; reboot restores 16 MHz. Higher values may cause display corruption. |

An optional unsigned 32-bit `requestId` is echoed in the response. The USB helper probes `info` after opening the port because CH340 adapters may reset the board. It waits for readiness before sending a settings command once, and ignores responses for older requests.

A setup response with `queued: true` is not proof that connection or storage succeeded. Use the screen or a subsequent `info` response.

Connection codes, in order: `0` Wi-Fi setup, `1` connecting Wi-Fi, `2` Wi-Fi unavailable, `3` setting clock, `4` server setup, `5` checking server, `6` online, `7` server unavailable, `8` token rejected, `9` storage error, `10` network time unavailable. Page codes: `0` dashboard, `1` network list, `2` Wi-Fi form, `3` server form, `4` item picker, `5` settings.

Use [tools/device.py](../tools/device.py) to prompt for secrets safely rather than including credentials in shell commands.
