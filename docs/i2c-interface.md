# I2C Interface

Vigilant Engine can expose an I2C master bus for node-specific sensors and peripherals. When I2C is enabled in
menuconfig, the bus is initialized during `vigilant_init()` and can then be used from the main firmware through
the public `vigilant_i2c_*` helpers.

## Enable I2C

In `idf.py menuconfig`, open `Vigilant Engine Configuration: I2C` and configure:

- `VE_ENABLE_I2C`: Enables the I2C interface in Vigilant Engine
- `VE_I2C_SCL_IO`: GPIO used for SCL
- `VE_I2C_SDA_IO`: GPIO used for SDA
- `VE_I2C_FREQ_HZ`: I2C bus frequency in Hertz

When enabled, Vigilant Engine builds the I2C driver, creates the bus during startup, and logs a scan of detected
7-bit device addresses.

## Runtime flow

- Call `vigilant_init(...)` first
- Define a `VigilantI2CDevice` object for your sensor or peripheral
- Add the device with `vigilant_i2c_add_device(&device)`
- Optionally verify identity with `vigilant_i2c_whoami_check(&device)`
- Write single-byte registers with `vigilant_i2c_set_reg8(&device, reg, value)`
- Read single-byte registers with `vigilant_i2c_read_reg8(&device, reg, &value)`
- Remove the device with `vigilant_i2c_remove_device(&device)` if you no longer need it

If I2C is disabled in menuconfig, the public `vigilant_i2c_*` helpers return `ESP_ERR_NOT_SUPPORTED`.

## Device object

The I2C interface uses a small device object so the caller only has to pass one pointer around.

___
#### `VigilantI2CDevice`, **struct**
Describes one I2C peripheral on the shared bus.

###### Fields:
- `address` 7-bit I2C device address
- `whoami_reg` Register used by the optional WHOAMI check
- `expected_whoami` Expected value returned by the WHOAMI register
- `handle` Runtime device handle managed by Vigilant Engine. Initialize this to `NULL`

## Public runtime functions

These are the functions intended to be used from the application in `main/`.

___
#### `vigilant_i2c_add_device`, **function**
Registers a device on the initialized Vigilant Engine I2C bus and stores the ESP-IDF device handle in the
provided `VigilantI2CDevice` object.

###### Parameters:
- `device` Pointer to the `VigilantI2CDevice` object to add

###### Returns:
- `ESP_OK` Device was added successfully, or was already added
- `ESP_ERR_INVALID_STATE` I2C bus is not initialized
- `ESP_ERR_INVALID_ARG` `device` is `NULL`
- `ESP_ERR_NOT_SUPPORTED` I2C support is disabled in menuconfig

___
#### `vigilant_i2c_remove_device`, **function**
Removes a previously added device from the I2C bus and clears its runtime handle.

###### Parameters:
- `device` Pointer to the `VigilantI2CDevice` object to remove

###### Returns:
- `ESP_OK` Device was removed successfully
- `ESP_ERR_INVALID_ARG` `device` is `NULL` or the device was not added before
- `ESP_ERR_NOT_SUPPORTED` I2C support is disabled in menuconfig

___
#### `vigilant_i2c_set_reg8`, **function**
Writes one 8-bit value to an 8-bit register on the selected I2C device.

###### Parameters:
- `device` Pointer to the `VigilantI2CDevice` object to write to
- `reg` Register address to transmit before the value byte
- `value` Register value to write

###### Returns:
- `ESP_OK` Register write succeeded
- `ESP_ERR_INVALID_ARG` `device` is `NULL` or the device was not added
- `ESP_ERR_NOT_SUPPORTED` I2C support is disabled in menuconfig

___
#### `vigilant_i2c_read_reg8`, **function**
Reads one 8-bit register from the selected I2C device.

###### Parameters:
- `device` Pointer to the `VigilantI2CDevice` object to read from
- `reg` Register address to transmit before reading
- `value` Output buffer for the returned register value

###### Returns:
- `ESP_OK` Register read succeeded
- `ESP_ERR_INVALID_ARG` `device` is `NULL`, the device was not added, or `value` is `NULL`
- `ESP_ERR_NOT_SUPPORTED` I2C support is disabled in menuconfig

___
#### `vigilant_i2c_whoami_check`, **function**
Reads the register stored in `device->whoami_reg` and compares it against `device->expected_whoami`.

###### Parameters:
- `device` Pointer to the `VigilantI2CDevice` object to validate

###### Returns:
- `ESP_OK` WHOAMI value matches the expected value
- `ESP_ERR_INVALID_ARG` `device` is `NULL` or the device was not added
- `ESP_ERR_INVALID_RESPONSE` The device responded, but the WHOAMI value did not match
- `ESP_ERR_NOT_SUPPORTED` I2C support is disabled in menuconfig

## Low-level I2C functions

These functions exist in the I2C component itself. In normal application code, prefer the
`vigilant_i2c_*` wrappers.

___
#### `i2c_init`, **function**
Initializes the shared I2C master bus, configures the GPIO pins from menuconfig, and logs a bus scan.

###### Returns:
- `ESP_OK` Bus is ready for use
- Any ESP-IDF error returned by `i2c_new_master_bus(...)`

___
#### `i2c_add_device`, **function**
Low-level variant of `vigilant_i2c_add_device(...)`. Adds a device to the bus using the provided device object.

___
#### `i2c_remove_device`, **function**
Low-level variant of `vigilant_i2c_remove_device(...)`. Removes a device and clears its handle.

___
#### `i2c_set_reg8`, **function**
Low-level variant of `vigilant_i2c_set_reg8(...)`. Writes one 8-bit register on a device.

___
#### `i2c_read_reg8`, **function**
Low-level variant of `vigilant_i2c_read_reg8(...)`. Reads one 8-bit register from a device.

___
#### `i2c_whoami_check`, **function**
Low-level variant of `vigilant_i2c_whoami_check(...)`. Compares the returned WHOAMI value with the expected one.

___
#### `i2c_deinit`, **function**
Deletes the shared I2C master bus. This is mainly intended for cleanup and is usually not needed in normal
application startup flow.

## Example

```c
VigilantI2CDevice accel = {
    .address = 0x18,
    .whoami_reg = 0x0F,
    .expected_whoami = 0x33,
    .handle = NULL,
};

ESP_ERROR_CHECK(vigilant_i2c_add_device(&accel));
ESP_ERROR_CHECK(vigilant_i2c_whoami_check(&accel));
ESP_ERROR_CHECK(vigilant_i2c_set_reg8(&accel, 0x20, 0x57));

uint8_t ctrl_reg = 0;
ESP_ERROR_CHECK(vigilant_i2c_read_reg8(&accel, 0x20, &ctrl_reg));
```

Use the device-specific address, register map, and expected WHOAMI value from your peripheral datasheet.

## Notes

- `vigilant_i2c_set_reg8(...)` and `vigilant_i2c_read_reg8(...)` operate on one 8-bit register at a time
- The I2C bus is shared, so multiple devices can be added as separate `VigilantI2CDevice` objects
- `vigilant_i2c_add_device(...)` must be called before any read, write, or WHOAMI check
