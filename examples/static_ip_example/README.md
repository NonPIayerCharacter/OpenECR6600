# static_ip_example

This example demonstrates the smallest practical Wi-Fi verification path after `standalone` boots successfully.

It focuses on:

- reusing the shared `standalone` startup path
- configuring a static IP for STA or SoftAP
- validating `wifi_sta` plus IP configuration without introducing product BLE provisioning logic

## Scope

This example is SDK-oriented and does not depend on `apps/ai_sys` product logic.

Compared with `wifi_netcfg_multiap_example`, it is intentionally simpler:

- no BLE provisioning bridge
- no SmartConfig flow
- no multi-AP credential manager behavior
- easier to tell whether a failure belongs to Wi-Fi basics, router settings, or the example itself

## Relationship With Standalone

This example reuses the `Boards/ecr6600/standalone/` startup path instead of defining a fully independent board package.

- The example `makefile` points `BOARD_TOOL_DIR` to `Boards/ecr6600/standalone/`
- On build, it creates example-local copies of `standalone/ref_bin/`, `defconfig`, `Kconfig`, and `main.c` when needed
- It appends `examples_config` to the copied `defconfig`
- These copied files are build artifacts and are intentionally ignored by git

For first-time bring-up, verify `Boards/ecr6600/standalone/README.md` first, then switch here for the first networking self-test.

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

- the example-generated application image under `build/static_ip_example/`
- the example-local `ref_bin/` copied from `standalone` during build

If only the application image changes and the base `standalone` flash assets stay compatible, you can usually keep the matching base bins unchanged.

## CLI

After boot, the example prints a simple workflow and adds the `static_ip` command:

```text
static_ip set [sta|ap] ip <ip> mask <mask> gw <gw> [dns1 <d>] [dns2 <d>]
static_ip get [sta|ap]
static_ip dhcp [sta|ap]
```

You can combine it with the common Wi-Fi commands already available in the reused `standalone` path:

```text
help
wifi_scan
wifi_sta <ssid> [password]
wifi_get_info
```

## Recommended Test Flow

Before the networking test flow, first confirm the board already passed the basic startup sanity check:

1. Serial log is visible.
2. No `flash init error`, `partition error`, or `easyflash error` appears.
3. The log shows `[EXAMPLE] app_example_main enter` or the static IP example banner.

Then use the following minimal flow:

1. Run `help`.
2. Run `wifi_scan`.
3. Choose an unused IP address in the same subnet as your router.
4. Run:

```text
static_ip set sta ip 192.168.1.123 mask 255.255.255.0 gw 192.168.1.1 dns1 8.8.8.8
```

5. Run:

```text
wifi_sta <ssid> <password>
```

6. After connection, run:

```text
static_ip get sta
```

7. If you want to restore normal DHCP behavior, run:

```text
static_ip dhcp sta
```

## Success Criteria

The example is considered basically healthy when:

- `wifi_scan` can see nearby APs
- `wifi_sta <ssid> <password>` can associate successfully
- the log prints `STA got IP, current configuration follows`
- `static_ip get sta` shows the configured IP, netmask, and gateway
- after `static_ip dhcp sta`, the STA path returns to DHCP mode

## Common Pitfalls

- The static IP must be in the same subnet as the router gateway.
- Do not choose an IP that is already in use by another device.
- If your router requires DHCP-only policy or portal authentication, the static IP test may fail even if Wi-Fi association itself succeeds.
- If `wifi_scan` already fails, solve that first before debugging `static_ip`.

## Recommended Next Step

After static IP verification succeeds, continue with `../wifi_netcfg_multiap_example/README.md` to test provisioning, credential storage, reconnect, and SmartConfig bridging.
