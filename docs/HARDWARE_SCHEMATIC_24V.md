# BDO Pneumatic Pump Controller - Hardware Schematic (24V System)
## ESP32-DevKit Based Design

**Date:** November 6, 2025
**Version:** 1.0
**Power System:** 24V DC

---

## Overview

This document provides complete wiring diagrams and component specifications for building the BDO Pneumatic Pump Controller using an ESP32-DevKit board with a 24V power system.

### Key Differences from Original Design

| Feature | Original (12V) | This Design (24V) |
|---------|----------------|-------------------|
| Power Input | 12V DC | **24V DC** |
| ESP32 Board | ESP32-DevKit | **ESP32-DevKit** (not XIAO) |
| DAC | Built-in 8-bit | **Built-in 8-bit** |
| Buck Converter | 12V→5V | **24V→5V + 24V→12V** |

---

## System Block Diagram

```
┌──────────────┐
│   24V DC     │ Main Power Input
│ Power Supply │
└──────┬───────┘
       │
       ├────────────► ITV2030 Electropneumatic Regulator (24V)
       │
       ├──[Buck 24V→12V]──► LCD Backlight (12V)
       │                  └► LM358 Op-Amp Power (12V)
       │
       └──[Buck 24V→5V]────► ESP32-DevKit (5V via USB/Vin)
                            └► PS-IN202 Scale (if 5V/12V DC model)
                            └► WS2812 LED Strip (5V, optional)
```

---

## Power System Design

### Primary Power Distribution

```
24V DC Input (1.5-2A recommended)
│
├─── ITV2030 Pneumatic Regulator: 24V @ 100mA
│
├───[LM2596 Buck 24V→12V, 1A]
│    ├─── LCD Backlight: 12V @ 30mA
│    └─── LM358 Op-Amp Supply: 12V @ 10mA
│
└───[LM2596 Buck 24V→5V, 3A]
     └─── ESP32-DevKit: 5V @ 500mA (via USB port or Vin with onboard regulator)
```

### Component Power Requirements

| Component | Voltage | Current | Notes |
|-----------|---------|---------|-------|
| ESP32-DevKit | 5V | 250-500mA | Via USB or Vin (has onboard 3.3V regulator) |
| ITV2030-33N2 | 24V | ~100mA | Electropneumatic regulator |
| LCD 1602 | 5V logic, 12V backlight | 30mA | I2C backpack |
| WS2812 LEDs (30) | 5V | 1.8A max | @ full brightness white (optional) |
| LM358 Op-Amp | 12V | 10mA | DAC output amplifier |
| Total Estimated | 24V input | **1.5-2.5A** | With safety margin (2.5A if using WS2812 at full brightness) |

### Buck Converters

**Buck Converter 1: 24V → 12V**
- Module: LM2596 DC-DC Buck Converter
- Input: 24V DC
- Output: 12V @ 1A
- Loads: LCD backlight, LM358 op-amp
- Adjustment: Use potentiometer to set exactly 12.0V

**Buck Converter 2: 24V → 5V**
- Module: LM2596 DC-DC Buck Converter (or equivalent)
- Input: 24V DC
- Output: 5V @ 3A
- Loads: ESP32-DevKit, WS2812 LED strip (optional)
- Adjustment: Set to exactly 5.0V for ESP32

### Recommended Power Supply

- **Voltage:** 24V DC regulated
- **Current:** 3A minimum (5A recommended for headroom)
- **Type:** Switching power supply (Mean Well LRS-75-24 or similar)
- **Protection:** Overcurrent, overvoltage, short circuit

---

## ESP32-DevKit Pinout and Connections

### GPIO Assignments

```
                    ┌─────────────────┐
                    │  ESP32-DevKit   │
                    │                 │
          [3V3] ────┤ 3V3         GND ├──── [GND]
  [ENCODER_SW] ────┤ VP/GPIO36   D23 ├────
          [EN] ────┤ EN          D22 ├──── [I2C_SCL] LCD
 [SAFETY_LED4] ────┤ GPIO34      TX0 ├────
 [SAFETY_BTN1] ────┤ GPIO35      RX0 ├────
 [SAFETY_BTN2] ────┤ GPIO32      D21 ├──── [I2C_SDA] LCD
 [ENCODER_CLK] ────┤ GPIO33      GND ├──── [GND]
  [ENCODER_DT] ────┤ GPIO25      D19 ├────
     [LED_STRIP]───┤ GPIO26      D18 ├────
      [DAC_OUT] ────┤ GPIO27       D5 ├──── [SAFETY_LED4]
 [SAFETY_BTN3] ────┤ GPIO14       TX2/D17├─[SCALE_TX]
[SAFETY_LED3] ────┤ GPIO12       RX2/D16├─[SCALE_RX]
          [GND] ────┤ GND          D4  ├────
[SAFETY_LED2] ────┤ GPIO13       D2  ├────
 [SAFETY_LED1] ────┤ GPIO9/SD2    D15 ├────
                [BOOT]──────┤ GPIO0/CMD    GND ├──── [GND]
                    │                 │
                    └─────────────────┘
```

### Detailed Pin Connections

| GPIO | Function | Connected To | Signal Type |
|------|----------|--------------|-------------|
| GPIO25 | DAC1 Output | LM358 Input | Analog 0-3.3V |
| GPIO17 | UART2 TX | Scale RX (via MAX3232) | Serial |
| GPIO16 | UART2 RX | Scale TX (via MAX3232) | Serial |
| GPIO21 | I2C SDA | LCD SDA | I2C Data |
| GPIO22 | I2C SCL | LCD SCL | I2C Clock |
| GPIO32 | Encoder CLK | Rotary Encoder A | Digital Input |
| GPIO33 | Encoder DT | Rotary Encoder B | Digital Input |
| GPIO34 | Encoder SW | Rotary Encoder Button (also for safety confirms) | Digital Input (input-only) |
| GPIO27 | LED Strip | WS2812 Data In | Digital Output |
| GPIO26 | ITV Feedback | ITV2030 PNP Output | Digital Input |
| GPIO35-39, GPIO14, GPIO12-15, GPIO4 | (Available) | Unused - can be used for future expansion | - |

---

## Circuit Schematics

### 1. DAC Output Amplifier (0-10V for ITV2030)

```
ESP32 GPIO25 (DAC1)                           To ITV2030
0-3.3V Output                                 0-10V Input
    │                                             │
    └────[10kΩ]────┬──────────────┐              │
                   │              │              │
                  [+] LM358      [─]             │
                   │    Op-Amp    │              │
                   │   (Non-Inv)  │              │
                  GND             └──[20kΩ]──────┤
                                        │        │
                                       GND    0-10V Out
                                               │
    12V ───[100nF]─┬─────────────────────────┐ │
                   │                         │ │
                  [+]                       [─]┘
                   │  LM358 Power Supply     │
                  GND                       GND

Gain = 1 + (R_feedback / R_input) = 1 + (20kΩ / 10kΩ) = 3.0×
Output = Input × 3.0 = 3.3V × 3.0 = 9.9V (close to 10V)

Components:
- U1: LM358 Dual Op-Amp (only one channel used)
- R1: 10kΩ 1/4W resistor (input)
- R2: 20kΩ 1/4W resistor (feedback)
- C1: 100nF ceramic capacitor (power decoupling)
- Power: 12V from buck converter
```

**Notes:**
- Use 1% metal film resistors for accuracy
- Add 100nF capacitor close to LM358 power pins
- Actual output: ~9.9V (acceptable for ITV2030 0-10V input)
- For exact 10V, adjust R2 to 20.3kΩ (use 20kΩ + trim pot)

---

### 2. RS232 Level Shifter (PS-IN202 Scale)

```
ESP32                MAX3232                PS-IN202 Scale

GPIO17 (TX2) ──────► T1in   T1out ──────────► RX (Pin 2)
                      │
GPIO16 (RX2) ◄─────── R1out  R1in ◄────────── TX (Pin 3)
                      │
         3.3V ────────┤VCC
                      │
          GND ────────┤GND ───────────────────► GND (Pin 5)
                      │
                   [4× 0.1µF caps]

Capacitors:
C1+, C1-, C2+, C2-: 0.1µF ceramic (charge pump)

DB9 or Terminal Block for Scale:
Pin 2: RX (from MAX3232 T1out)
Pin 3: TX (to MAX3232 R1in)
Pin 5: GND
```

**Alternative:** Use USB-to-TTL module if scale has USB interface

---

### 3. LCD 1602 I2C Connection

```
ESP32-DevKit              LCD 1602 with I2C Backpack (PCF8574)

GPIO21 (SDA) ──────────────► SDA
GPIO22 (SCL) ──────────────► SCL
5V (from buck) ────────────► VCC (logic)
12V (from buck) ───────────► BL (backlight, if separate pin)
GND ───────────────────────► GND

I2C Address: 0x27 (typical) or 0x3F (check with I2C scanner)

Pull-up resistors: Usually included on I2C backpack (4.7kΩ)
If not included, add external 4.7kΩ pull-ups on SDA and SCL to 3.3V
```

---

### 4. Rotary Encoder Connection

```
Rotary Encoder            ESP32-DevKit

CLK (A) ───────────────────► GPIO32
DT  (B) ───────────────────► GPIO33
SW (Button) ───────────────► GPIO34
GND ───────────────────────► GND
+ (if powered) ────────────► 3.3V

Note: Some encoders have internal pull-ups.
If not, add external 10kΩ pull-ups to 3.3V on CLK, DT, and SW pins.
```

---

### 5. Safety Interlock System (LCD-Based)

**No separate safety buttons required!**

Safety checks are performed using LCD display prompts with rotary encoder button confirmation:

```
Safety Check Sequence (displayed on LCD):

Check 1: "Air Line OK?"
         "Press to confirm"
         [Wait for encoder button press]

Check 2: "Fill Hose OK?"
         "Press to confirm"
         [Wait for encoder button press]

Check 3: "Tank Position?"
         "Press to confirm"
         [Wait for encoder button press]

Check 4: "Ready to Fill?"
         "Press to START"
         [Wait for encoder button press]

Each check has 30-second timeout.
Encoder button (GPIO34) is used for all confirmations.
```

**Benefits:**
- Simplified hardware (no additional buttons needed)
- Lower cost (saves ~$20)
- Reduced wiring complexity
- Still maintains 4-stage safety verification
- Clear visual prompts on LCD

---

### 6. ITV2030 Connections

```
ITV2030-33N2 Terminal Block

24V Power:
    Terminal 1 (+): 24V DC
    Terminal 2 (-): GND

Control Signal (0-10V):
    Terminal 3: Control Signal (from LM358 op-amp output)
    Terminal 4: Signal GND (common with ESP32 GND)

PNP Output (Pressure Reached):
    NPN/PNP Output: Configurable switch output

    For PNP configuration (typically used):
    When pressure reached:
        Output pulls HIGH to supply voltage

    Connection to ESP32:
    PNP Out ─────[10kΩ]─────┬──► GPIO26
                            │
                         [10kΩ]
                            │
                           GND

    (Voltage divider if output is 24V, otherwise direct if configured for 5V/12V)
```

**ITV2030 Configuration:**
- Consult SMC manual (ITVop.pdf) for DIP switch settings
- Set output type: PNP or NPN
- Set output condition: Pressure reached, valve open, etc.

---

### 7. WS2812 LED Strip (Optional)

```
ESP32-DevKit              WS2812 LED Strip (30 LEDs)

GPIO27 ──────[330Ω]──────────► DIN (Data In)
5V (3A buck) ────────────────► 5V
GND ─────────────────────────► GND

Notes:
- 30 LEDs @ full white = 1.8A (60mA × 30)
- Use thick wires for 5V and GND (18-20 AWG)
- Add 1000µF capacitor across 5V and GND near strip
- 330Ω resistor protects first LED data input
```

---

## Bill of Materials (BOM)

### Core Components

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 1 | ESP32-DevKit | 38-pin development board | $8.00 |
| 1 | LM2596 (24V→12V) | DC-DC buck converter module, 1A | $2.00 |
| 1 | LM2596 (24V→5V) | DC-DC buck converter module, 3A | $2.50 |
| 1 | Power Supply | 24V 3-5A switching PSU | $15.00 |
| 1 | ITV2030-33N2 | SMC electropneumatic regulator | (existing) |
| 1 | PS-IN202 | Scale with RS232 interface | (existing) |

### Interface Components

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 1 | LCD1602 I2C | 16×2 LCD with I2C backpack | $5.00 |
| 1 | Rotary Encoder | KY-040 or similar with button | $2.50 |
| 1 | MAX3232 | RS232 level shifter module | $2.00 |
| 1 | LM358 | Dual op-amp DIP-8 IC | $0.50 |
| 1 | WS2812 Strip | 30 LED strip, 5V (optional) | $8.00 |

### Passive Components

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 2 | 10kΩ 1% | Metal film resistor, 1/4W (DAC circuit) | $0.20 |
| 1 | 20kΩ 1% | Metal film resistor, 1/4W (DAC circuit) | $0.10 |
| 1 | 330Ω | Resistor for WS2812 data | $0.05 |
| 3 | 10kΩ | Pull-up resistors (encoder, ITV feedback) | $0.15 |
| 5 | 0.1µF | Ceramic capacitors (decoupling) | $0.50 |
| 1 | 1000µF | Electrolytic cap (LED strip, optional) | $0.50 |

### Wiring and Enclosure

| Qty | Part | Description | Est. Cost |
|-----|------|-------------|-----------|
| 1 | Enclosure | Project box (200×150×75mm) | $15.00 |
| 1 | Wire Kit | 22AWG hookup wire, various colors | $10.00 |
| 1 | Terminal Blocks | Screw terminals for connections | $5.00 |
| 1 | Perfboard/PCB | Prototype board for circuits | $5.00 |
| 1 | Connectors | JST, Dupont, misc connectors | $5.00 |

**Total Estimated Cost: ~$88** (excluding ITV2030 and scale)

**Cost Savings vs Original Design:**
- No separate safety buttons: -$20.00
- Fewer resistors/transistors: -$1.50
- **Total Savings: ~$21.50**

---

## Assembly Notes

### Step 1: Power System Assembly

1. Mount both LM2596 buck converters
2. Connect 24V input to both converters
3. Adjust output voltages:
   - Converter 1: 12.0V (measure with multimeter)
   - Converter 2: 5.0V (measure with multimeter)
4. Connect 5V output to ESP32 USB/Vin pin
5. Connect 12V to LCD backlight and LM358

### Step 2: ESP32 Connections

1. Solder headers to ESP32-DevKit if needed
2. Wire I2C to LCD (SDA, SCL, 5V, GND)
3. Wire rotary encoder (CLK, DT, SW, GND)
4. Wire UART to MAX3232 (TX, RX)
5. Add all GPIO connections per pinout table

### Step 3: DAC Amplifier Circuit

1. Build on small perfboard or breadboard first
2. Test with multimeter:
   - ESP32 DAC output: 0-3.3V
   - Op-amp output: 0-9.9V
3. Once verified, solder permanent circuit
4. Mount near ESP32 to minimize noise

### Step 4: Safety Button Panel

1. Mount 4 illuminated buttons on front panel
2. Wire button contacts through voltage dividers
3. Wire button LEDs with NPN transistor drivers
4. Test each button and LED individually

### Step 5: Final Integration

1. Connect scale RS232 cable
2. Connect ITV2030 control and power
3. Optional: Connect WS2812 LED strip
4. Label all connections
5. Secure all wiring with zip ties
6. Close enclosure and test system

---

## Testing Procedure

### 1. Power-On Test

- [ ] 24V input measured and stable
- [ ] 12V buck output correct
- [ ] 5V buck output correct
- [ ] ESP32 powers on (LED indicator)
- [ ] LCD backlight illuminates

### 2. Communication Test

- [ ] LCD displays text correctly
- [ ] Rotary encoder direction and button
- [ ] Scale sends weight data (check serial monitor)
- [ ] WiFi connects
- [ ] MQTT publishes test message

### 3. DAC Output Test

- [ ] Measure voltage at op-amp output
- [ ] Should vary from 0V to ~10V as ESP32 DAC changes
- [ ] ITV2030 responds to control signal

### 4. Safety System Test

- [ ] Each button press detected
- [ ] Each button LED illuminates on command
- [ ] Safety sequence completes properly

### 5. Full System Test

- [ ] Run fill cycle with empty container
- [ ] Verify pressure control zones
- [ ] Check WebUI accessibility
- [ ] Verify MQTT data in Grafana

---

## Safety Warnings

⚠️ **ELECTRICAL SAFETY:**
- Disconnect power before working on circuits
- Use properly rated power supply with protections
- Ensure all connections are secure and insulated
- Do not exceed component voltage/current ratings

⚠️ **PNEUMATIC SAFETY:**
- Verify air system is properly depressurized before maintenance
- Follow all safety interlocks before operation
- Use proper pressure regulators and relief valves
- Consult ITV2030 manual for safe operating limits

⚠️ **TESTING:**
- Test with empty containers first
- Verify scale accuracy before production use
- Calibrate DAC output voltage precisely
- Monitor first fills closely for proper operation

---

## Troubleshooting

### No Power / ESP32 Won't Boot

- Check 24V input voltage
- Verify buck converter outputs (12V and 5V)
- Check ESP32 USB connection or Vin pin
- Press EN button to reset

### LCD Not Displaying

- Check I2C address (0x27 or 0x3F)
- Verify 5V power to LCD
- Check SDA/SCL connections
- Adjust contrast pot on I2C backpack

### Scale Not Reading

- Check RS232 wiring (TX/RX may be swapped)
- Verify MAX3232 power (3.3V or 5V)
- Check baud rate (9600 default)
- Test scale separately with serial monitor

### ITV2030 Not Responding

- Measure DAC output voltage (should be 0-10V)
- Check op-amp power supply (12V)
- Verify resistor values (10kΩ and 20kΩ)
- Check ITV2030 power (24V)

### WiFi Won't Connect

- Check SSID and password in config.h
- Verify 2.4GHz network (ESP32 doesn't support 5GHz)
- Check router firewall settings
- Move ESP32 closer to router for testing

---

## Schematic Diagrams

Full schematic diagrams available in KiCad format:
- `hardware/schematic/bdo_pump_controller.kicad_sch`
- `hardware/pcb/bdo_pump_controller.kicad_pcb`

---

**Document Version:** 1.0
**Last Updated:** November 6, 2025
**Status:** Production Ready
