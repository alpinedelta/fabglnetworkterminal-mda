





[README.md](https://github.com/user-attachments/files/35123852/README.md)
# NetworkTerminal_MDA (ESP32 FabGL Monochrome MDA Telnet Terminal)

An **ESP32 + FabGL** firmware that turns a vintage **IBM MDA / Hercules TTL monochrome CRT** (720×348 @ 50Hz, 90×43 character terminal) — or a **Brother word processor monochrome CRT** (720×240 @ 60Hz) — into a standalone **Wi-Fi VT/ANSI Telnet terminal** with PS/2 keyboard support.

> **Attribution:** This project is a modification of the **NetworkTerminal** example included in the [FabGL](https://github.com/fdivitto/fabgl) library, created by **Fabrizio Di Vittorio** ([www.fabgl.com](http://www.fabgl.com)). All core terminal emulation, Telnet protocol handling, ICMP ping, and VGA/PS2 controller foundations come from Fabrizio Di Vittorio's original work, extended here with MDA/Brother monochrome timings, custom bitmap fonts, configurable boot auto-connect (`secrets.h`), and optional one-shot auto-login.

---

## Features

- **Native Monochrome TTL Video Output (`VGA2Controller`)**:
  - **MDA / Hercules CRT** (`720×348 @ 50Hz`, `+HSync -VSync`, `16.000 MHz` pixel clock — `90×43` terminal grid)
  - **Brother CRT** (`720×240 @ 60Hz`, `+HSync +VSync`, `14.31818 MHz` pixel clock — uncomment in `setup()`)
- **PS/2 Keyboard Support**: Built-in support for `us`, `uk`, `de`, `it`, `es`, `fr`, `be`, and `no` layouts.
- **VT/ANSI Telnet Client**: Negotiates terminal type (`wsvt25`) and window size over Telnet (`IAC`) for Linux hosts, BBSes, and retro terminal environments (such as IBM OfficeVision/400 MDA).
- **Interactive Shell**: `scan`, `wifi`, `telnet`, `ping`, `keyb`, `info`, `help`, `reboot`.
- **Private Local Configuration (`secrets.h`)**: Keep your Wi-Fi SSID, password, Telnet server IP, and optional auto-login credentials completely outside Git.

---

## Hardware Requirements & Pinout

- **Board**: ESP32 (e.g., LilyGO TTGO VGA32 v1.4 or standard ESP32 DevKit with PS/2 + TTL video wiring)
- **Keyboard**: PS/2 keyboard connected to **Keyboard Port 0**
- **Monitor**: IBM 5151 / MDA / Hercules TTL monochrome CRT (9-pin DE-9 TTL interface) or Brother monochrome CRT

### Default Video Pinout (VGA32 / TTL Monochrome)

Configured in `setup()` via `DisplayController.begin(...)`:

| Signal | Default GPIO (VGA32) | Alt ESP32 Pin Map (commented in sketch) |
| :--- | :--- | :--- |
| **Video (R/G/B tied)** | `GPIO 21` | `GPIO 22` |
| **HSync** | `GPIO 23` | `GPIO 23` |
| **VSync** | `GPIO 15` | `GPIO 15` |
| **PS/2 Keyboard** | `PS2Preset::KeyboardPort0` (`GPIO 33` CLK / `GPIO 32` DATA on VGA32) | `GPIO 26` / `GPIO 27` (commented) |

> **Hardware Warning:** Vintage IBM MDA monitors expect **TTL-level (5V) video/sync signals** and can be damaged by out-of-spec horizontal sync frequencies! Double-check your level shifting / voltage compatibility and verify the modeline matches your monitor before connecting power.

---

## Arduino IDE Setup

> **Important Compatibility Note:** **FabGL 1.0.9** requires **ESP32 Arduino Core 2.x** (tested with **`2.0.17`**). It does **not** compile on ESP32 Arduino Core 3.x.

1. **Install Arduino IDE** (v2.x or v1.8.x).
2. **Install ESP32 Board Support (`2.0.17`)**:
   - Open **File → Preferences** and add the Espressif board manager URL if not already present:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Open **Tools → Board → Boards Manager**, search for **`esp32`** by Espressif Systems, and install version **`2.0.17`**.
3. **Install FabGL (`1.0.9`)**:
   - Open **Tools → Manage Libraries** (Library Manager), search for **`FabGL`**, and install **`1.0.9`**.
4. **Select Board Settings**:
   - **Board**: `ESP32 Dev Module` (or your specific ESP32 board)
   - **Flash Frequency / Partition**: Default (`4MB` / Default 4MB with spiffs)

---

## Configuration (Wi-Fi & Telnet)

1. Copy the template config file:
   ```bash
   cp NetworkTerminal_MDA/secrets.h.example NetworkTerminal_MDA/secrets.h
   ```
2. Edit `NetworkTerminal_MDA/secrets.h` with your network details:
   ```cpp
   #define KEYB_LAYOUT "us"

   #define WIFI_SSID "MyNetwork"
   #define WIFI_PASS "MyPassword"

   #define TELNET_HOST "192.168.1.100"
   #define TELNET_PORT 23

   // Optional: only enable on a trusted local network (Telnet is unencrypted!)
   #define ENABLE_AUTO_LOGIN false
   #define SERVER_USER "myuser"
   #define SERVER_PASS "mypassword"
   ```
   - If you skip creating `secrets.h` (or leave `WIFI_SSID` / `TELNET_HOST` empty `""`), the sketch still compiles and boots straight to the interactive `>` prompt.
   - `secrets.h` is excluded by `.gitignore` so your credentials will never be pushed to GitHub.

3. **Monitor Selection** (in `NetworkTerminal_MDA/NetworkTerminal_MDA.ino` inside `setup()`):
   - Default is **IBM MDA / Hercules (`720×348 @ 50Hz`)**.
   - For a **Brother monochrome CRT (`720×240 @ 60Hz`)**, comment out the `MDA_50HZ_LOCK` line and uncomment the `BROTHER` `setResolution(...)` line.

4. Open `NetworkTerminal_MDA/NetworkTerminal_MDA.ino` in Arduino IDE, select your serial port, and click **Upload**.

---

## Built-in Terminal Commands

Once at the `>` prompt:

| Command | Description |
| :--- | :--- |
| `help` | Display available commands |
| `info` | Show screen resolution, terminal dimensions, and free DMA/32-bit RAM |
| `scan` | Scan nearby 2.4 GHz Wi-Fi networks |
| `wifi SSID PASSWORD` | Connect to a Wi-Fi network |
| `telnet HOST [PORT]` | Open a Telnet connection (default port `23`) |
| `ping HOST` | Ping an IP address or hostname (`Ctrl+C` to stop) |
| `keyb LAYOUT` | Change PS/2 keyboard layout (`us`, `uk`, `de`, `it`, `es`, `fr`, `be`, `no`) |
| `reboot` | Restart the ESP32 |

---

## Optional: Setting Up a Linux Telnet Host for 90×43 MDA

If you connect this terminal to a Linux machine on your private LAN:

1. Install a Telnet daemon (use only on an isolated/trusted LAN, as Telnet is unencrypted):
   ```bash
   sudo apt install inetutils-telnetd openbsd-inetd
   ```
2. Add the following to your Linux host's `~/.bashrc` so sessions from the ESP32 automatically configure a clean **90×43 monochrome VT100** layout:
   ```bash
   if [[ "$TERM" == "wsvt25" || "$TERM" == "vt100" || "$TERM" == "ansi" || "$MDA" == "1" ]]; then
       export TERM=vt100
       export MDA=1
       stty rows 43 cols 90 2>/dev/null
       export LINES=43
       export COLUMNS=90
       printf '\0337\033[?6l\033[1;43r\0338'
       alias ls='ls --color=never'
       alias grep='grep --color=never'
   fi
   ```

---

## Credits & Attribution

- This project is a modification of the **NetworkTerminal** example from the [FabGL](https://github.com/fdivitto/fabgl) library.
- Full credit for FabGL and the original `NetworkTerminal` example goes to **Fabrizio Di Vittorio** ([www.fabgl.com](http://www.fabgl.com) / [github.com/fdivitto/fabgl](https://github.com/fdivitto/fabgl)).
