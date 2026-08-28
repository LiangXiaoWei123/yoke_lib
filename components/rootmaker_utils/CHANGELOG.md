# Changelog

## 1.0.0 - 2026-08-28

### Added

* `rootmaker_port_t` interface port model (no predefined port instances; the
  application declares its own).
* I2C master bus pool keyed by (i2c_num, SDA, SCL) with idempotent sharing and
  conflict detection.
* `rootmaker_get_port_pins()` helper for non-I2C drivers.
