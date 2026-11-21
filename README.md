# Smart Digital Door Lock Using ATmega32

A two-factor door lock: a **4-digit PIN** on a keypad, followed by **voice verification**
(spoken phrase + speaker identity) running on a laptop. The door opens only when both checks pass.

Higher Technological Institute, Electrical Department. Supervised by Prof. Mohamed Torad.

## Features

- 4-digit PIN stored permanently in the ATmega32 EEPROM (default: `1234`)
- 16x2 LCD over I2C (PCF8574, address `0x27`), green and red LEDs, buzzer
- Voice verification on a laptop with two AI models:
  - **Faster-Whisper** checks that the spoken phrase is "open the door"
  - **SpeechBrain ECAPA-TDNN** checks that the voice belongs to the enrolled owner
- ESP32 works as a UART bridge between the ATmega32 and the laptop
- Fail-safe behaviour: no answer, timeout or invalid recording means access denied
- 3 wrong PINs: alarm, voice identity check, then the PIN can be changed
- 3 failed voice checks: lockout alarm

## System Architecture

```
ATmega32 (keypad, LCD, LEDs, buzzer)
   |  UART 9600 baud (5 V / 3.3 V level shifting)
ESP32 (UART bridge)
   |  USB serial 115200 baud
Laptop (Python: Whisper + ECAPA-TDNN)
```

## Hardware

- ATmega32 with a 14.7456 MHz crystal
- 4x4 matrix keypad
- 16x2 LCD with I2C backpack (0x27)
- Green LED, red LED, buzzer
- ESP32 development board
- Voltage divider (for example 1 k and 2 k) or level shifter on the ATmega32 TX to ESP32 RX line
- Laptop with a microphone

### ATmega32 Pin Mapping

| Function | Pin |
|----------|-----|
| Keypad rows | PA0 - PA3 |
| Keypad columns | PA4 - PA7 |
| I2C LCD (SCL / SDA) | PC0 / PC1 |
| Green LED | PD2 |
| Red LED | PC7 |
| Buzzer | PC6 |
| UART RXD / TXD | PD0 / PD1 |

### ATmega32 to ESP32 Wiring

| ATmega32 | ESP32 | Note |
|----------|-------|------|
| PD1 (TXD) | GPIO16 (RX2) | Through a voltage divider |
| PD0 (RXD) | GPIO17 (TX2) | Direct |
| GND | GND | Common ground |

## Communication Protocol

| Direction | Data | Meaning |
|-----------|------|---------|
| ATmega32 to ESP32 | `V` | Request voice verification |
| ESP32 to PC | line `V` | Request forwarded to Python |
| PC to ESP32 | `1` / `0` | Verified / rejected |
| ESP32 to ATmega32 | `1` / `0` | Result forwarded |

Timeouts: ATmega32 60 s, ESP32 55 s (no answer = denied).

## Keypad

`0-9` enter a digit, `*` clear the entry, `#` confirm.

## Repository Structure

```
firmware/
  atmega32_lock/     ATmega32 C code (main.c) and hex file
  esp32_bridge/      ESP32 Arduino sketch
python/              Voice verification program (smart.py) and requirements.txt
docs/                Full project report (PDF)
```

## How to Run

### 1. ATmega32
1. Open `firmware/atmega32_lock/main.c` in Microchip Studio (or compile with avr-gcc), `F_CPU = 14745600UL`.
2. Program the ATmega32 with the generated hex file.

### 2. ESP32
1. Open `firmware/esp32_bridge/esp32_bridge.ino` in the Arduino IDE.
2. Select your ESP32 board and upload.

### 3. Python (laptop)
```bash
cd python
python -m venv .venv
.venv\Scripts\activate          # Windows
pip install -r requirements.txt
```

Edit `SERIAL_PORT` in `smart.py` (for example `COM3`) to match the ESP32 port.
Close the Arduino Serial Monitor before running.

Create the owner's voiceprint (say "open the door" 8 times):
```bash
python smart.py enroll
```

Start the verification server:
```bash
python smart.py run
```

Other mode: `python smart.py test` tries the voice check without the hardware.

The voiceprint (`voiceprint.npy`) is the owner's biometric data and is not included in this repository.
Every user must enroll with their own voice.

## Limitations

- The spoken phrase is fixed, so a recording of the owner could be replayed. A random challenge phrase would fix this.
- The voice threshold (`THRESH = 0.45`) is an initial value and should be calibrated with genuine and impostor tests.
- The PIN is stored unencrypted in EEPROM.
- The lock needs the laptop and the ESP32 to complete the second factor.

## Documentation

Full report: [docs/Smart_Digital_Door_Lock_Report.pdf](docs/Smart_Digital_Door_Lock_Report.pdf)

## Authors

- Abdalrhman Mostafa El Mortada
- Reem Mohamed
- Ahmed Mohamed Mohamed
