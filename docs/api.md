# Data, security and USB contracts

## Cloud reads

The device requests `GET /api/v1/sources` every 10 seconds while connected. Polls add the `ids` and `accounts` filters, so Web returns only the selected panels' sources and those accounts' projects. A token check, the first panel defaults and an open item picker read every source; the picker lists every item when that reply arrives. It sends the read token in the `Authorization: Bearer` header. It uses the ESP32 core's certificate bundle and hostname verification, requires a valid clock, and does not follow redirects. A redirect response asks the user to check the server address. Tokens are never placed in URLs.

Polls are conditional. The device keeps the `ETag` of the displayed catalog together with the exact request that returned it: server origin, connection generation (a new server address or key) and the `ids`/`accounts` query. The next request with the same server, generation and query sends `If-None-Match` with that tag. Web answers `304 Not Modified` without a body when no listed source changed. The device then keeps its catalog and restarts the report clock from `X-Server-Time`, so ages advance by the server time since the cached reply. A 304 is invalid without a valid `X-Server-Time` or without a matching cached reply; the device then reports an invalid workspace and keeps the last readings. The device drops the tag when the server, key or query changes, when a reply has no valid `ETag`, and after a decode or validation failure; the next request is then unconditional and returns a complete body. Switching between panel reads and full reads therefore always transfers the full reply once.

The response must contain `sources` and `serverTime`. Resources retain their metric definitions and order. Agent sources are grouped by `account.provider` and `account.id`. The newest observation supplies account measurements; equal timestamps use ascending ASCII source IDs. `agent.usage5hResetsIn` and `agent.usageWeekResetsIn` are whole minutes until the session and weekly limits reset, measured at `observedAt`. The device subtracts the observation age and shows the countdown only beside a reported value. `agent.usageWeekModel` is the weekly usage of one model family, from 0 to 100. When the newest project reports it, the account shows it after the weekly limit as `Weekly {usageWeekModelLabel}`, or `Weekly model` without a label, with the `usageWeekModelResetsIn` countdown. Other values make the response invalid. Account status considers every reporting project, so a newer working project cannot hide another project's input request or error. A provider can have multiple accounts.

Resources become `Stale` after 120 seconds without a receipt. Coding hooks report events and repeat a running turn's state every 60 seconds as a heartbeat, so a long tool call or response stays active. A coding project's state follows its newest report through the shared state timeouts and becomes neutral `Inactive` when that clock reaches `sleep`; an event's health ages with its state. The device reevaluates each resource's receipt age and each project's event age while offline. State labels, attention priorities and timers come from the pinned public App contract, shared with Web.

Report age starts from `serverTime` and advances with accumulated device uptime. The 32-bit platform clock wrapping after about 49.7 days does not reset an offline report's age. A newly accepted snapshot resets only the elapsed time since that snapshot.

| Limit | Value |
| --- | --- |
| Reporting sources | 128 |
| Metrics per source | 32 |
| HTTP response body | 1 MiB |
| JSON allocation budget | 512 KiB |
| Visible metrics per card | First 3, following the Web order; details show all |
| Poll interval | 10 seconds |
| Retry delay for transient failures | 20, 40, then at most 60 seconds |
| Retry delay after HTTP 429 | The longer of that delay and `Retry-After` in seconds, at most 15 minutes. **Refresh** and the item picker also wait for `Retry-After`. |
| Resource stale boundary | 120 seconds since the server received it |

Missing measurements remain unavailable; they do not become zero. Invalid or incomplete snapshots do not replace the displayed catalog. The connection message names the first rule that the snapshot breaks, such as an invalid timestamp or more than 128 sources. It never quotes response content. Network failures retain the last accepted values. Rejected saved credentials stop automatic polling until the user retries or changes the key. A rejected replacement does not overwrite the previous saved connection. A verified server/key change clears old account selections before accepting the new workspace.

`Retry-After` belongs to the HTTPS origin that returned it. Checking another server does not delay the saved server or erase its existing wait. Cancelled setup replies cannot change the workspace, but their server's delay is still kept. Expired waits are removed even while the device is offline.

Plan usage bars (metric keys that start with `usage`) use the App contract's tones: warning from 70 and danger from 90, both inclusive. Other bars use the accent color. Percentage and GiB readings use the shared App/Web precision: at most two decimal places, without trailing zeros. Other large readings use compact suffixes on cards; details show their full numeric representation and unit. Long names and labels wrap in the scrollable detail view. The stored measurements retain their original precision.

## Persistent settings

The `vibemon-touch` NVS namespace stores one versioned `config` value containing Wi-Fi credentials, server origin, key and four selections. One NVS write commits the complete configuration. Polling does not write flash. Selecting the same item or reconnecting with unchanged Wi-Fi credentials does not write flash. Leaving an in-progress setup form cancels it; a late cloud reply cannot save a canceled workspace.

Server addresses use lowercase hostnames and numeric ports without leading zeros. The default HTTPS port `443` is omitted, so `https://vibemon.io:00443` and `https://vibemon.io` keep the same key and panel selections. Older stored addresses are normalized when read, without an automatic flash write.

The default firmware does not enable flash or NVS encryption. Physical access to the device or a flash backup can expose stored credentials. Use a dedicated read token and keep firmware backups private. In RAM, the HTTPS worker and the USB console overwrite their temporary copies of the token and Wi-Fi password after use; copies inside the HTTP and TLS libraries are not overwritten. The optional USB console is a local setup interface, not an authenticated network service.

The firmware has no inbound HTTP status server, captive portal or WebSocket connection. Collectors send reports to Web; the device only reads them.

## USB commands

Send one UTF-8 JSON object followed by LF at 115200 baud. Commands are limited to 1023 bytes before LF. Invalid and oversized commands return an error without echoing the input. At most 256 incoming bytes are processed per UI loop.

| Command | Fields | Result |
| --- | --- | --- |
| `info` | None | Version, board, memory, connection/page codes, the connection message, item/selection counts (items in the last reply, which can hold only the selected panels), touch presses, completed scanout frames, LVGL redraws, rendered pixels and requested pixel clock, running firmware slot and firmware update state, available version and progress; no credentials or names. Frame/pixel counters wrap at 32 bits. |
| `configure-wifi` | `ssid`, `password` | Validate and queue a connection. Save only after it connects. |
| `configure-cloud` | `origin`, `token` | Validate and queue an authenticated workspace read. Save only after it succeeds. |
| `dashboard` | None | Open the four-panel view. |
| `refresh` | None | Request a refresh, including an explicit retry after a rejected token. |
| `update-check` | None | Check for a firmware release now. A newer release installs and the device restarts. |
| `display-clock` | `hz`: 8000000, 12000000, 16000000, 20000000 or 26000000 | Local display diagnosis only. Change at a frame boundary; reboot restores 16 MHz. Higher values may cause display corruption. |

An optional unsigned 32-bit `requestId` is echoed in the response. The USB helper probes `info` after opening the port because CH340 adapters may reset the board. It waits for readiness before sending a settings command once, and ignores responses for older requests.

A setup response with `queued: true` is not proof that connection or storage succeeded. Use the screen or a subsequent `info` response.

Connection codes, in order: `0` Wi-Fi setup, `1` connecting Wi-Fi, `2` Wi-Fi unavailable, `3` setting clock, `4` server setup, `5` checking server, `6` online, `7` server unavailable, `8` token rejected, `9` storage error, `10` network time unavailable. Firmware update states: `0` check pending, `1` checking, `2` up to date, `3` check failed, `4` installing, `5` restarting, `6` install failed. Page codes: `0` dashboard, `1` network list, `2` Wi-Fi form, `3` server form, `4` item picker, `5` settings, `6` details.

Use [tools/device.py](../tools/device.py) to prompt for secrets safely rather than including credentials in shell commands.
