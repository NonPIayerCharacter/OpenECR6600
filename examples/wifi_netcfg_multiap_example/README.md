# wifi_netcfg_multiap_example

This example demonstrates how to use the shared `wifi_netcfg` framework to:
- store multiple AP credentials in NV
- trigger scan / connect / reconnect by the shared manager
- simulate BLE provisioning by CLI command input
- bridge real smartconfig credentials into the shared manager

## Scope

This example is SDK-oriented and does not depend on `apps/ai_sys`.
WiFi ownership stays inside `wifi_netcfg` and the example bridge layer.

## Relationship With Standalone

This example reuses the `Boards/ecr6600/standalone/` startup path instead of defining a fully independent board package.

- The example `makefile` points `BOARD_TOOL_DIR` to `Boards/ecr6600/standalone/`
- On build, it creates example-local copies of `standalone/ref_bin/`, `defconfig`, `Kconfig`, and `main.c` when needed
- It appends `examples_config` to the copied `defconfig`
- It appends `examples_kconfig` to the copied `Kconfig` for the example-only `WIFI_NETCFG_MULTIAP_EXAMPLE` option
- These copied files are build artifacts and are intentionally ignored by git

For first-time bring-up, it is usually safer to verify `Boards/ecr6600/standalone/` can boot first, then switch to this example for Wi-Fi self-test.

## Build

From the example directory:

```sh
make defconfig
make all
```

The generated image is placed under the example build output directory together with the ELF and map files.

For a clean template refresh:

```sh
make EXAMPLES_FILE_CLEAN
make all
```

## Flash Notes

This example is not a standalone flash layout definition. Its base flash assets come from `Boards/ecr6600/standalone/ref_bin/`.

When preparing a first full download set, usually pay attention to:

- the example-generated application image under `build/wifi_netcfg_multiap_example/`
- the example-local `ref_bin/` copied from `standalone` during build

If only the application image changes and the base `standalone` flash assets stay compatible, you can usually keep the matching base bins unchanged.

## CLI

After boot, use `sc_test` and `smartcfg` to operate the example:

```text
sc_test list
sc_test status
sc_test add <ssid> <password>
sc_test del <index|ssid>
sc_test clear
sc_test start
sc_test connect <index|ssid>
sc_test submit <ssid> <password>
sc_test scan
sc_test disc
smartcfg start
smartcfg stop
smartcfg status
```

## SmartConfig Debug Path

The example now supports two provisioning paths:

- Manual path: use `sc_test submit <ssid> <password>` to inject credentials directly into `wifi_netcfg`
- Real path: use `smartcfg start` and broadcast credentials from a SmartConfig-capable phone app

In the real path, SmartConfig is used only as a credentials source.
After SSID/password are decoded, the example stops SmartConfig and forwards the credentials into `wifi_netcfg`.
Actual WiFi connect, storage, scan, reconnect, and retry logic still stay inside the shared `wifi_netcfg` manager.

## Recommended Test Flow

1. Run `sc_test clear` to remove stored credentials.
2. Run `smartcfg start`.
3. Use the phone app to send real SmartConfig packets.
4. Check that the example logs `Smartconfig got credentials`, then `wifi_netcfg` enters connect flow.
5. Verify the device stores the AP and auto-reconnects after reboot or disconnect.

Before the Wi-Fi test flow, also confirm the board has already passed the basic startup sanity check:

1. Serial log is visible.
2. No `flash init error`, `partition error`, or `easyflash error` appears.
3. The log shows `[EXAMPLE] app_example_main enter`.

## Design Notes

- `wifi_netcfg_multiap_app.c` owns the example manager instance and wraps all manager operations.
- `cmd_wifi_netcfg_multiap.c` only parses CLI arguments and calls example APIs.
- The shared event handling path is `system_event -> wifi_netcfg_adapter -> wifi_netcfg_manager`.
- Station stop / scan / IP status helpers are routed through `wifi_netcfg_port.h`.
- SmartConfig is bridged as an external credentials input, not as a direct `wifi_start_station()` owner.

For shared framework integration details, see components/wifi_netcfg/README.md.
