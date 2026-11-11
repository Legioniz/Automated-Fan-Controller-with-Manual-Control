# Automated Fan Controller with Manual Control

Arduino firmware for a DC fan controller with automatic temperature response and button-selected manual speeds. The current sketch is `fan_controller_v6.ino`.

## Hardware interface used by the sketch

| Signal | Arduino pin | Purpose |
|---|---:|---|
| LM35 output | A0 | Analog temperature input |
| Fan driver control | D3 (PWM) | PWM speed command to an appropriately rated MOSFET/driver |
| Mode button | D2 | Button input using `INPUT_PULLUP` |
| Status LED | D13 | On in automatic mode, off in manual modes |

The sketch calculates temperature using a 5 V analog reference and a 10-bit ADC conversion. Use a compatible board or adjust the conversion for the actual board and reference voltage.

**Electrical safety:** Do not connect a fan directly to an Arduino I/O pin. Use a correctly rated driver, a suitable external supply for the fan, appropriate motor protection, and a shared ground where the circuit requires it. Check the fan, MOSFET, wiring, and power-supply ratings before applying power.

## Operating modes

Each debounced button press advances through the modes:

1. **AUTO** — the fan maintains a LOW minimum and periodically selects LOW, MED, or HIGH according to the temperature samples.
2. **LOW** — fixed low speed.
3. **MED** — fixed medium speed.
4. **HIGH** — fixed high speed.
5. **OFF** — fan output off.

The sequence then repeats.

## Automatic control logic

- Temperature is sampled every 500 ms.
- Ten samples are collected for each voting window (approximately five seconds).
- The automatic thresholds are 35 °C for MED and 40 °C for HIGH.
- A 600 ms full-output startup kick helps the fan start or accelerate before settling at the selected PWM value.
- The mode button uses a 50 ms debounce interval.
- Serial diagnostics are sent at 9600 baud.

In AUTO mode, LOW is intentionally the permanent minimum. The OFF state is available only through the manual mode sequence.

## Upload and test

1. Open `fan_controller_v6.ino` in the Arduino IDE.
2. Select the correct board and port.
3. Confirm the actual wiring and power arrangement against your hardware.
4. Compile and upload.
5. Open Serial Monitor at **9600 baud** to inspect readings and mode changes.
6. Test with the fan unloaded or safely restrained as appropriate, then verify temperature transitions and every manual mode.

This repository contains the firmware sketch, not a complete wiring diagram or a certified electrical design. Hardware compatibility and safe operation must be verified on the actual build.

## Project status

The source is a standalone Arduino sketch. The README describes the configured pins and control constants in the current code; it does not claim that the sketch has been compiled or tested on every Arduino board.

Copyright © 2026 King Art Dedullo Pacumberto.
