# WiFi Master Mode

WiFi master mode is an optional feature for nodes that coordinate other Vigilant Engine devices over the node's SoftAP.
It is disabled by default so leaf devices do not compile the HTTP polling task.

## Enable It

Enable `VE_ENABLE_WIFI_MASTER` in menuconfig:

```text
Vigilant Engine Configuration: WiFi -> Enable WiFi master mode
```

Then set the runtime config to master mode:

```c
VigilantConfig config = {
    .unique_component_name = "Vigilant Master",
    .network_mode = NW_MODE_APSTA,
    .is_master = true,
};
```

Both settings are required:

- `VE_ENABLE_WIFI_MASTER=y` compiles the master polling code in `src/master.c`.
- `is_master=true` starts the master polling task at runtime.
- `network_mode` must be `NW_MODE_APSTA`.

If `is_master=true` is used in firmware built without `VE_ENABLE_WIFI_MASTER`, `vigilant_init()` returns
`ESP_ERR_NOT_SUPPORTED`.

## Device Magic

Every Vigilant Engine node exposes its identity on `/info`:

- HTTP header: `X-Vigilant-Device: vigilant-engine-device-v1`
- JSON fields: `is_vigilant_device: true` and `vigilant_magic: "vigilant-engine-device-v1"`

The master probes unknown AP clients with a short `/info` request. Clients that do not answer with the magic are marked
as `other` and are probed less often, which prevents phones and laptops on the setup AP from being polled continuously.

`/wifiinfo` reports the cached identity for connected AP clients:

- `identity`: `unknown`, `vigilant`, or `other`
- `is_vigilant_device`: compatibility boolean
- `mac`
- `address_ip`

The main dashboard's Connected Devices page includes those WiFi clients next to the local bus devices. When a client is
identified as Vigilant, the dashboard can fetch its `/info` data through the master and request `/rebootfactory` on that
client so it reboots into recovery.
