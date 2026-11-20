/*
 * ESP32 MDA / Monochrome Network Terminal (FabGL)
 *
 * This sketch is a modification of the "NetworkTerminal" example from the
 * FabGL library by Fabrizio Di Vittorio (https://github.com/fdivitto/fabgl).
 * Original NetworkTerminal example Copyright (c) 2019-2022 Fabrizio Di Vittorio.
 *
 * Drives a vintage IBM MDA / Hercules monochrome CRT (720x348 @ 50Hz, 90x43 text)
 * or Brother monochrome CRT using FabGL VGA2Controller + PS/2 keyboard,
 * providing a Wi-Fi Telnet terminal with optional auto-connect and auto-login.
 *
 * Configuration:
 *   Copy `secrets.h.example` to `secrets.h` and fill in your Wi-Fi and Telnet
 *   server details. `secrets.h` is git-ignored so credentials stay private.
 *   If `secrets.h` is not present, the terminal boots directly to the `>` prompt.
 */

#include <fabgl.h>
#define FABGL_FONT_INCLUDE_DEFINITION
#include "font_brother2.h"
#include "font_brother_bold.h"

#include <WiFi.h>
#include "network/ICMP.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// =========================================================================
// Fallback defaults (override these in secrets.h — see secrets.h.example)
// =========================================================================
#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS ""
#endif
#ifndef TELNET_HOST
  #define TELNET_HOST ""
#endif
#ifndef TELNET_PORT
  #define TELNET_PORT 23
#endif
#ifndef KEYB_LAYOUT
  #define KEYB_LAYOUT "us"
#endif
#ifndef ENABLE_AUTO_LOGIN
  #define ENABLE_AUTO_LOGIN false
#endif
#ifndef SERVER_USER
  #define SERVER_USER ""
#endif
#ifndef SERVER_PASS
  #define SERVER_PASS ""
#endif


enum class State { Prompt,
                   PromptInput,
                   UnknownCommand,
                   Help,
                   Info,
                   Wifi,
                   TelnetInit,
                   Telnet,
                   Scan,
                   Ping,
                   Reset,
                   Keyb
                 };


State        state = State::Prompt;
WiFiClient   client;
char const * currentScript = nullptr;
bool         error = false;
static char  autoexecBuf[256] = {0};


fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller  PS2Controller;
fabgl::Terminal       Terminal;
fabgl::LineEditor     LineEditor(&Terminal);


void build_autoexec_script()
{
  size_t pos = 0;
  pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "info\r");

  if (strlen(KEYB_LAYOUT) > 0 && pos < sizeof(autoexecBuf) - 1) {
    pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "keyb %s\r", KEYB_LAYOUT);
  }

  if (strlen(WIFI_SSID) > 0 && pos < sizeof(autoexecBuf) - 1) {
    if (strlen(WIFI_PASS) > 0) {
      pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "wifi %s %s\r", WIFI_SSID, WIFI_PASS);
    } else {
      pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "wifi %s\r", WIFI_SSID);
    }
  }

  if (strlen(TELNET_HOST) > 0 && pos < sizeof(autoexecBuf) - 1) {
    if (TELNET_PORT != 23) {
      pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "telnet %s %d\r", TELNET_HOST, TELNET_PORT);
    } else {
      pos += snprintf(autoexecBuf + pos, sizeof(autoexecBuf) - pos, "telnet %s\r", TELNET_HOST);
    }
  }
}


void exe_info()
{
  Terminal.write("\e[97m* * VT/ANSI MDA Network Terminal\r\n");
  Terminal.printf("\e[92mScreen Size        :\e[93m %d x %d\r\n", DisplayController.getViewPortWidth(), DisplayController.getViewPortHeight());
  Terminal.printf("\e[92mTerminal Size      :\e[93m %d x %d\r\n", Terminal.getColumns(), Terminal.getRows());
  Terminal.printf("\e[92mFree DMA Memory    :\e[93m %d\r\n", heap_caps_get_free_size(MALLOC_CAP_DMA));
  Terminal.printf("\e[92mFree 32 bit Memory :\e[93m %d\r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT));
  /*
  if (WiFi.status() == WL_CONNECTED) {
    Terminal.printf("\e[92mWiFi SSID          :\e[93m %s\r\n", WiFi.SSID().c_str());
    Terminal.printf("\e[92mCurrent IP         :\e[93m %s\r\n", WiFi.localIP().toString().c_str());
  }*/
  Terminal.write("\n\e[92mType \e[93mhelp\e[92m to print all available commands.\r\n");
  error = false;
  state = State::Prompt;
}


void exe_help()
{
  Terminal.write("\e[93mhelp\e[92m\r\n");
  Terminal.write("\e[97m  Shows this help.\r\n");
  Terminal.write("\e[93minfo\r\n");
  Terminal.write("\e[97m  Shows system info.\r\n");
  Terminal.write("\e[93mscan\r\n");
  Terminal.write("\e[97m  Scan for WiFi networks.\r\n");
  Terminal.write("\e[93mwifi [SSID PASSWORD]\r\n");
  Terminal.write("\e[97m  Connect to SSID using PASSWORD.\r\n");
  Terminal.write("\e[97m  Example:\r\n");
  Terminal.write("\e[97m    wifi MyWifi MyPassword\r\n");
  Terminal.write("\e[93mtelnet HOST [PORT]\r\n");
  Terminal.write("\e[97m  Open telnet session with HOST (IP or host name) using PORT.\r\n");
  Terminal.write("\e[97m  Example:\r\n");
  Terminal.write("\e[97m    telnet towel.blinkenlights.nl\e[92m\r\n");
  Terminal.write("\e[93mping HOST\r\n");
  Terminal.write("\e[97m  Ping a HOST (IP or host name).\r\n");
  Terminal.write("\e[97m  Example:\r\n");
  Terminal.write("\e[97m    ping 8.8.8.8\e[92m\r\n");
  Terminal.write("\e[93mreboot\r\n");
  Terminal.write("\e[97m  Restart the system.\e[92m\r\n");
  Terminal.write("\e[93mkeyb LAYOUT\r\n");
  Terminal.write("\e[97m  Set keyboard layout. LAYOUT can be 'us', 'uk', 'de', 'it', 'es', 'fr', 'be', 'no'\r\n");
  Terminal.write("\e[97m  Example:\r\n");
  Terminal.write("\e[97m    keyb de\e[92m\r\n");
  error = false;
  state = State::Prompt;
}


void decode_command()
{
  auto inputLine = LineEditor.get();
  if (*inputLine == 0)
    state = State::Prompt;
  else if (strncmp(inputLine, "help", 4) == 0)
    state = State::Help;
  else if (strncmp(inputLine, "info", 4) == 0)
    state = State::Info;
  else if (strncmp(inputLine, "wifi", 4) == 0)
    state = State::Wifi;
  else if (strncmp(inputLine, "telnet", 6) == 0)
    state = State::TelnetInit;
  else if (strncmp(inputLine, "scan", 4) == 0)
    state = State::Scan;
  else if (strncmp(inputLine, "ping", 4) == 0)
    state = State::Ping;
  else if (strncmp(inputLine, "reboot", 6) == 0)
    state = State::Reset;
  else if (strncmp(inputLine, "keyb", 4) == 0)
    state = State::Keyb;
  else
    state = State::UnknownCommand;
}


void exe_prompt()
{
  if (currentScript) {
    // process commands from script
    if (*currentScript == 0 || error) {
      // end of script, return to prompt
      currentScript = nullptr;
      state = State::Prompt;
    } else {
      // execute current line and move to the next one
      const char * cr = strchr(currentScript, '\r');
      if (!cr) {
        currentScript = nullptr;
        state = State::Prompt;
        return;
      }
      int linelen = cr - currentScript;
      LineEditor.setText(currentScript, linelen);
      currentScript += linelen + 1;
      decode_command();
    }
  } else {
    // process commands from keyboard
    Terminal.write(">");
    state = State::PromptInput;
  }
}


void exe_promptInput()
{
  LineEditor.setText("");
  LineEditor.edit();
  decode_command();
}


void exe_scan()
{
  static char const * ENC2STR[] = { "Open", "WEP", "WPA-PSK", "WPA2-PSK", "WPA/WPA2-PSK", "WPA-ENTERPRISE" };
  Terminal.write("Scanning...");
  Terminal.flush();
  int networksCount = WiFi.scanNetworks();
  Terminal.printf("%d network(s) found\r\n", networksCount);
  if (networksCount) {
    Terminal.write   ("\e[90m #\e[4GSSID\e[45GRSSI\e[55GCh\e[60GEncryption\e[92m\r\n");
    for (int i = 0; i < networksCount; ++i) {
      int enc = WiFi.encryptionType(i);
      const char * encStr = (enc >= 0 && enc < 6) ? ENC2STR[enc] : "Unknown";
      Terminal.printf("\e[93m %d\e[4G%s\e[93m\e[45G%d dBm\e[55G%d\e[60G%s\e[92m\r\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), encStr);
    }
  }
  WiFi.scanDelete();
  error = false;
  state = State::Prompt;
}


void exe_wifi()
{
  static const int MAX_SSID_SIZE = 32;
  static const int MAX_PSW_SIZE  = 64;
  char ssid[MAX_SSID_SIZE + 1];
  char psw[MAX_PSW_SIZE + 1] = {0};
  error = true;
  auto inputLine = LineEditor.get();
  if (sscanf(inputLine, "wifi %32s %64s", ssid, psw) >= 1) {
    Terminal.write("Connecting WiFi...");
    Terminal.flush();
    WiFi.disconnect(true, true);
    for (int i = 0; i < 2; ++i) {
      WiFi.begin(ssid, psw);

      if (WiFi.waitForConnectResult() == WL_CONNECTED)
        break;
      WiFi.disconnect(true, true);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Terminal.printf("connected to %s, IP is %s\r\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      error = false;
    } else {
      Terminal.write("failed!\r\n");
    }
  }
  state = State::Prompt;
}


// =========================================================================
// AUTO-LOGIN STATE (Configured via secrets.h)
// =========================================================================
int  autoLoginStep = 0; // 0 = off, 1 = waiting for user, 2 = waiting for pass
bool autoLoginUsed = false;

void exe_telnetInit()
{
  static const int MAX_HOST_SIZE = 64;
  char host[MAX_HOST_SIZE + 1];
  int port;
  error = true;
  autoLoginStep = 0;

  auto inputLine = LineEditor.get();
  int pCount = sscanf(inputLine, "telnet %64s %d", host, &port);
  if (pCount > 0) {
    if (pCount == 1)
      port = 23;
    Terminal.printf("Trying %s...\r\n", host);
    if (client.connect(host, port)) {
      Terminal.printf("Connected to %s\r\n", host);
      error = false;
      state = State::Telnet;

      // Arm auto-login once on power-up if enabled in secrets.h
      if (ENABLE_AUTO_LOGIN && !autoLoginUsed && strlen(TELNET_HOST) > 0 &&
          strlen(SERVER_USER) > 0 && strcmp(host, TELNET_HOST) == 0) {
        autoLoginStep = 1;
        autoLoginUsed = true;
      }
    } else {
      Terminal.write("Unable to connect to remote host\r\n");
      state = State::Prompt;
    }
  } else {
    Terminal.write("Mistake\r\n");
    state = State::Prompt;
  }
}


int clientWaitForChar()
{
  while (!client.available())
    ;
  return client.read();
}


void exe_telnet()
{
  static char lineBuf[64] = {0};
  static int  lineIdx = 0;

  // process data from remote host (up to 1024 codes at a time)
  for (int i = 0; client.available() && i < 1024; ++i) {
    int c = client.read();
    if (c == 0xFF) {
      // IAC (Interpret As Command)
      uint8_t cmd = clientWaitForChar();
      uint8_t opt = clientWaitForChar();
      if (cmd == 0xFD && opt == 0x1F) {
        // DO WINDOWSIZE (NAWS)
        client.write("\xFF\xFB\x1F", 3);
        client.write("\xFF\xFA\x1F" "\x00\x50\x00\x19" "\xFF\xF0", 9);
      } else if (cmd == 0xFD && opt == 0x18) {
        // DO TERMINALTYPE
        client.write("\xFF\xFB\x18", 3);
      } else if (cmd == 0xFA && opt == 0x18) {
        // SB TERMINALTYPE
        c = clientWaitForChar();
        c = clientWaitForChar();
        c = clientWaitForChar();
        client.write("\xFF\xFA\x18\x00" "wsvt25" "\xFF\xF0", 12);
      } else {
        uint8_t pck[3] = {0xFF, 0, opt};
        if (cmd == 0xFD)
          pck[1] = 0xFC;
        else if (cmd == 0xFB)
          pck[1] = 0xFD;
        client.write(pck, 3);
      }
    } else {
      Terminal.write(c);

      // Watch incoming text for login and password prompts when auto-login is armed
      if (autoLoginStep > 0) {
        if (c == '\r' || c == '\n') {
          lineIdx = 0;
          lineBuf[0] = '\0';
        } else if (c >= 32 && c <= 126 && lineIdx < 62) {
          lineBuf[lineIdx++] = tolower(c);
          lineBuf[lineIdx] = '\0';

          if (autoLoginStep == 1 && (strstr(lineBuf, "login:") || strstr(lineBuf, "user"))) {
            delay(300);
            client.print(SERVER_USER);
            client.write('\r');
            autoLoginStep = 2;
            lineIdx = 0;
            lineBuf[0] = '\0';
          } else if (autoLoginStep == 2 && strstr(lineBuf, "pass")) {
            delay(300);
            client.print(SERVER_PASS);
            client.write('\r');
            autoLoginStep = 0;
            lineIdx = 0;
            lineBuf[0] = '\0';
          }
        }
      }
    }
  }

  // process data from terminal (keyboard)
  while (Terminal.available()) {
    client.write(Terminal.read());
  }

  // return to prompt?
  if (!client.connected()) {
    client.stop();
    autoLoginStep = 0;
    state = State::Prompt;
  }
}


void exe_ping()
{
  char host[64];
  auto inputLine = LineEditor.get();
  int pcount = sscanf(inputLine, "ping %63s", host);
  if (pcount > 0) {
    int sent = 0, recv = 0;
    fabgl::ICMP icmp;
    while (true) {

      // CTRL-C ?
      if (Terminal.available() && Terminal.read() == 0x03)
        break;

      int t = icmp.ping(host);
      if (t >= 0) {
        Terminal.printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\r\n", icmp.receivedBytes(), icmp.hostIP().toString().c_str(), icmp.receivedSeq(), icmp.receivedTTL(), (double)t/1000.0);
        delay(1000);
        ++recv;
      } else if (t == -2) {
        Terminal.printf("Cannot resolve %s: Unknown host\r\n", host);
        break;
      } else {
        Terminal.printf("Request timeout for icmp_seq %d\r\n", icmp.receivedSeq());
      }
      ++sent;

    }
    if (sent > 0) {
      Terminal.printf("--- %s ping statistics ---\r\n", host);
      Terminal.printf("%d packets transmitted, %d packets received, %.1f%% packet loss\r\n", sent, recv, (double)(sent - recv) / sent * 100.0);
    }
  }
  state = State::Prompt;
}


void exe_keyb()
{
  if (PS2Controller.keyboard()->isKeyboardAvailable()) {
    char layout[3];
    auto inputLine = LineEditor.get();
    if (sscanf(inputLine, "keyb %2s", layout) == 1) {
      if (strcasecmp(layout, "US") == 0)
        Terminal.keyboard()->setLayout(&fabgl::USLayout);
      else if (strcasecmp(layout, "UK") == 0)
        Terminal.keyboard()->setLayout(&fabgl::UKLayout);
      else if (strcasecmp(layout, "DE") == 0)
        Terminal.keyboard()->setLayout(&fabgl::GermanLayout);
      else if (strcasecmp(layout, "IT") == 0)
        Terminal.keyboard()->setLayout(&fabgl::ItalianLayout);
      else if (strcasecmp(layout, "ES") == 0)
        Terminal.keyboard()->setLayout(&fabgl::SpanishLayout);
      else if (strcasecmp(layout, "FR") == 0)
        Terminal.keyboard()->setLayout(&fabgl::FrenchLayout);
      else if (strcasecmp(layout, "BE") == 0)
        Terminal.keyboard()->setLayout(&fabgl::BelgianLayout);
      else if (strcasecmp(layout, "NO") == 0)
        Terminal.keyboard()->setLayout(&fabgl::NorwegianLayout);
      else {
        Terminal.printf("Error! Invalid keyboard layout.\r\n");
        state = State::Prompt;
        return;
      }
    }
    Terminal.printf("\r\nKeyboard layout is : \e[93m%s\e[92m\r\n\r\n", Terminal.keyboard()->getLayout()->desc);
  } else {
    Terminal.printf("No keyboard present\r\n");
  }
  state = State::Prompt;
}


void setup()
{
  //Serial.begin(115200); // DEBUG ONLY

  PS2Controller.begin(PS2Preset::KeyboardPort0);
  //PS2Controller.begin(GPIO_NUM_26, GPIO_NUM_27, GPIO_UNUSED, GPIO_UNUSED);

  // Start VGA in monochrome mode with standard ESP32 pin map:
  //DisplayController.begin(GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_15);

  // Start TTL Video in monochrome mode with VGA32 pin map (R/G/B=GPIO21, HSync=GPIO23, VSync=GPIO15):
  DisplayController.begin(GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_23, GPIO_NUM_15);

  // 720x348 @ 50Hz MDA Hercules - Enable this for IBM MDA / Hercules monochrome monitors
  DisplayController.setResolution("\"MDA_50HZ_LOCK\" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync");

  // 720x240 @ 60Hz BROTHER - Enable this for Brother word processor CRTs
  //DisplayController.setResolution("\"BROTHER\" 14.31818 720 736 776 880 240 243 246 270 +HSync +VSync");

  Terminal.begin(&DisplayController);
  Terminal.connectLocally();      // to use Terminal.read(), available(), etc.
  //Terminal.setLogStream(Serial);  // DEBUG ONLY

  Terminal.setBackgroundColor(Color::Black);
  Terminal.setForegroundColor(Color::White);
  Terminal.clear();

  Terminal.enableCursor(true);

  build_autoexec_script();
  currentScript = autoexecBuf;
}


void loop()
{
  switch (state) {

    case State::Prompt:
      exe_prompt();
      break;

    case State::PromptInput:
      exe_promptInput();
      break;

    case State::Help:
      exe_help();
      break;

    case State::Info:
      exe_info();
      break;

    case State::Wifi:
      exe_wifi();
      break;

    case State::TelnetInit:
      exe_telnetInit();
      break;

    case State::Telnet:
      exe_telnet();
      break;

    case State::Scan:
      exe_scan();
      break;

    case State::Ping:
      exe_ping();
      break;

    case State::Reset:
      ESP.restart();
      break;

    case State::Keyb:
      exe_keyb();
      break;

    case State::UnknownCommand:
      Terminal.write("\r\nMistake\r\n");
      state = State::Prompt;
      break;

    default:
      Terminal.write("\r\nNot Implemented\r\n");
      state = State::Prompt;
      break;

  }
}
