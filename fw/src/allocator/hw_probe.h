#pragma once
// hw_probe.h — Lightweight hardware presence probes for the Allocator.
//
// Each probe is a one-shot, non-destructive check that a physical peripheral
// is present and responding.  Probes run during the Allocator preflight
// (Allocator::compute()) before any peripheral driver objects are initialised.
//
// Rules:
//   - A probe MUST NOT permanently configure any peripheral.
//     Any driver state it sets up (UART baud, I2C clock) MUST be either
//     released (i2c_deinit) or left in a state the driver's own init() is
//     happy to overwrite.
//   - A probe MUST complete within ~2 seconds.  The allocator is serial.
//   - A probe MUST be safe to call on hardware where the peripheral is absent.
//   - The call returns true if the peripheral responded, false otherwise.
//
// In host tests (JLPICART_HOST_TEST) every probe returns true so that
// capability-registry and allocator tests are hardware-agnostic.

bool hw_probe(const char* capability_name);
