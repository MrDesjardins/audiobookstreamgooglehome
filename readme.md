# Description

The code is for the client side of an audio book machine that has two components: ESP-32 Wroom and a TFT Display that has a potentiometer and a button.

- The ESP-32 Wroom send a signal to a HTTP REST endpoint depending of the current passed on potentiometer which will start an audio to a specific Google Home device.
- The ESP-32 connects to a 2.4-inch TFT Display with EC11 Rotary Encoder Combination Module SPI Interface LCD Display. 
- The LCD is used to show the name of the audio book. The button is pressed to send the HTTP REST endpoint. 
- The list of audio books is also coming from the HTTP server.

# Specification of the Module TFT Display with EC11 Rotary Encoder module

The module is a combination of 2.4 inch SPI interface display module and EC11 rotary encoder, the two are not related, just put on a board, made into an integrated module, with an additional key, the key interface is also independent, you can choose to use it according to the actual use of the situation. All-in-one design, more simple and beautiful, convenient DIY.

1. All-in-one design: display and encoder are integrated, simple and beautiful, convenient for DIY
2. Multiple size options: 2.4-inch TFT display, to meet different needs
3. High-resolution display: 2.4-inch 320x240 resolution, clear and delicate
4. SPI interface support: SPI interface, strong compatibility, easy to connect and program
5. Encoder with switch: EC11 rotary encoder, 20 pulses 20 positioning, with switch function
6. 2.4 inch TFT driver chip: ST7789
7. EC11: plum blossom handle, handle length 15mm, 20 pulses 20 positioning, 5 feet with switch

# Schema of the TFT Display with EC11 Rotary Encoder module


| Pin | Description                                           |
| --- | ----------------------------------------------------- |
| GND | Ground                                                |
| VCC | Power (usually 3.3V, check your module)               |
| SCL | SPI Clock (some modules may call this SCK)            |
| SDA | SPI Data (sometimes MOSI)                             |
| RST | Reset for the display                                 |
| DC  | Data/Command select                                   |
| CS  | Chip select for SPI                                   |
| BL  | Backlight (can be tied to 3.3V or controlled via PWM) |
| K0  | Confirm key |
| RES | Reset pin |
|A | Phase A of the EC11 |
|B | Phase B of the EC11 |


# ESP-32 Pins

## TFT Display (SPI)

```
LCD GND  → ESP32 GND
LCD VCC  → ESP32 3.3V
LCD SCL  → ESP32 GPIO18 (SPI SCK)
LCD SDA  → ESP32 GPIO23 (SPI MOSI)
LCD RST  → ESP32 GPIO4
LCD DC   → ESP32 GPIO2
LCD CS   → ESP32 GPIO5
LCD BL   → ESP32 GPIO25 (PWM for brightness control)
LCD K0   → ESP32 GPIO15 (brightness adjustment button)
```

## EC11 Rotary Encoder

```
Encoder CLK (A)      → ESP32 GPIO27
Encoder DT (B)       → ESP32 GPIO14
Encoder SW (Push)    → ESP32 GPIO13 (push encoder down to play)
GND                  → ESP32 GND
VCC                  → 3.3V (shared with display)
```

**Note:** SW = Switch (the push button inside the encoder knob). When you press down on the encoder, this pin connects to ground. It may be labeled SW, KEY, BTN, or S on your module.

# Libraries

- **TFT_eSPI** - Display driver (requires configuration, see SETUP.md)
- **ESP32Encoder** - Rotary encoder handling
- **ArduinoJson** - JSON parsing (v6 or v7)

**Note:** A pre-configured `Custom_Setup.h` file is included for TFT_eSPI. See SETUP.md for installation instructions.