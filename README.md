# Mini Lathe Controller v6.2

## Third-party components

This project uses code based on:

- [nopnop2002/esp-idf-ili9340](https://github.com/nopnop2002/esp-idf-ili9340)

Thanks to the original author.


Sterownik mini-tokarki CNC z jarzmem K11-3
i glowica podzialowa VEVOR BS4-KP100-57.

**Mikrokontroler**: ESP32-S3-WROOM-1 N16R8 (DevKitC-1)
**Framework**: ESP-IDF v5.5.1
**Display**: TFT SPI ST7735 / ILI9340 / ILI9341 / ST7789 / ST7796 (modulowa obsluga przez menuconfig)
**Komunikacja**: USB JTAG/Serial, karta SD (FATFS przez SPI2), SPIFFS na fonty i bitmapy, **WiFi AP + Web Server**, **BLE GATT**

---

## Sprzet

| Komponent | Model / parametry |
|---|---|
| Mikrokontroler | ESP32-S3-WROOM-1 N16R8, 16 MB Flash, 8 MB PSRAM |
| Wyswietlacz | ST7735 160x128 / ILI9340 320x240 / ILI9341 / ST7789 / ST7796 480x320 |
| Silnik wrzeciona | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Silnik posuwu Z | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Silnik dosuwu X | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Sterowniki | 3x DM556 (20-50V DC, do 5.6A, mikrostepping 12800 kr/obr) |
| Przekladnia wrzeciona | Glowica VEVOR BS4-KP100-57, slimakowa 1:6 |
| Zasilacz | 36V / 5.6A / 350VA SWPS | Active PFC Mean Well |
| Bezpieczenstwo | Modul MOSFET PWM (5-55V, do 100A) odcina zasilanie DM556; E-STOP grzybkowy |
| Buzzer | Pasywny buzzer 4 kHz (GPIO48), PWM LEDC, sygnaly OK/WARN/ERROR/E-STOP |
| SD card | FAT32 przez SPI2 (MOSI/SCLK/MISO wspoldzielone z TFT) |

---

## Mapa GPIO - kompletna (v6)

### SPI2 - magistrala wspoldzielona (TFT + karta SD)

Ustawienia wyswietlacza TFT mozna zmienic w `idf.py menuconfig`.

| Sygnal | GPIO | Kierunek | Uwagi |
|---|---|---|---|
| MOSI | **11** | OUT | TFT (SDA) + SD (CMD) |
| SCLK | **12** | OUT | TFT (SCK) + SD (CLK) - strapping, OK po boot |
| MISO | **13** | IN | TFT (SDO) + SD (DAT0) |
| TFT CS | **10** | OUT | |
| SD CS | **44** | OUT | IO44 - wolny, bez konfliktu z LED (IO48) |
| TFT DC | **14** | OUT | Data/Command |
| TFT RST | **9** | OUT | |
| TFT BL | **19** | OUT | PWM LEDC do podswietlenia |

### Enkoder obrotowy + przyciski

| Sygnal | GPIO | Uwagi |
|---|---|---|
| ENC CLK | **4** | PCNT kwadraturowy |
| ENC DT | **5** | PCNT kwadraturowy |
| ENC SW | **6** | Przycisk w enkoderze |
| BTN1 | **7** | |
| BTN2 | **15** | Strapping pin - OK po boot |
| BTN3 | **16** | |

### DM556 #1 - Wrzeciono (NEMA23 -> glowica VEVOR 1:6)

| Sygnal | GPIO | Uwagi |
|---|---|---|
| STEP | **47** | przez BC846 + 330 Ohm, Common Anode |
| DIR | **21** | przez BC846 + 330 Ohm |
| ENA | **20** | przez BC846 + 330 Ohm, UWAGA: IO20 = USB_D+ (nieaktywne gdy USB JTAG) |

### DM556 #2 - Posuw os Z (NEMA23 -> sruba pociagowa)

| Sygnal | GPIO | Uwagi |
|---|---|---|
| STEP | **41** | przez BC846 + 330 Ohm, Common Anode |
| DIR | **40** | przez BC846 + 330 Ohm |
| ENA | **39** | przez BC846 + 330 Ohm |

### DM556 #3 - Dosuw os X (NEMA23 -> sanki narzedziowe)

| Sygnal | GPIO | Uwagi |
|---|---|---|
| STEP | **1** | przez BC846 + 330 Ohm, Common Anode |
| DIR | **2** | przez BC846 + 330 Ohm, strapping pin - OK po boot |
| ENA | **42** | przez BC846 + 330 Ohm |

### Krance / limit switches

| Sygnal | GPIO | Uwagi |
|---|---|---|
| Z_MIN | **8** | |
| Z_MAX | **3** | Input-only - wymagany zewn. pull-up |
| X_MIN | **46** | Strapping - OK po boot |
| X_MAX | **45** | |

### Bezpieczenstwo

| Sygnal | GPIO | Uwagi |
|---|---|---|
| MOSFET IN | **17** | HIGH = zasilanie DM556 wlaczone, przez MOSFET PWM |
| E-STOP NO | **18** | Zewn. 4.7k pull-up -> GND przy wcisnieciu (NEGEDGE ISR) |
| BUZZER | **48** | Pasywny buzzer 4 kHz PWM (LEDC) |

> **Schemat Common Anode (BC846):**
> `5V -> [330 Ohm] -> PUL+/DIR+/ENA+`
> `ESP32 GPIO -> [1k Ohm] -> Baza BC846 -> Kolektor -> PUL-/DIR-/ENA-`
> GPIO HIGH = BC846 nasycony = sygnal aktywny w DM556

---

## Parametry DM556

### Prad (SW1-SW3) - NEMA23 2.5A/faze

| SW1 | SW2 | SW3 | Prad peak | |
|-----|-----|-----|-----------|--------|
| ON | OFF | OFF | 2.92A | OK bezpieczny start |
| OFF | ON | OFF | 3.51A | max dla silnika |

**SW4 = OFF** -> automatyczna redukcja pradu w spoczynku (**zalecane**)

### Mikrostepping (SW5-SW8)

| SW5 | SW6 | SW7 | SW8 | Kroki/obr | |
|-----|-----|-----|-----|-----------|--------|
| OFF | ON | ON | OFF | **12800** | **<- AKTUALNIE** |
| OFF | ON | OFF | ON | 25600 | cicha alternatywa |

---

## Parametry mechaniczne

| Parametr | Os Z | Os X |
|---|---|---|
| Skok sruby | 2.0 mm | 1.25 mm |
| Mikrostepping | 12800 kr/obr | 12800 kr/obr |
| Krokow/mm | 6400 kr/mm | 10240 kr/mm |
| Predkosc max | ~420 mm/min | ~120 mm/min |

> **Przed pierwszym ruchem** - zweryfikuj skok sruby i zaktualizuj `AXIS_Z_LEAD_MM` / `AXIS_X_LEAD_MM` w `components/axis/include/axis.h`.

---

## Struktura projektu

```
mini_lathe_v6/
├── CMakeLists.txt                # SPIFFS build (fonty + bitmapy)
├── partitions.csv                # 16 MB flash: factory + spiffs
├── sdkconfig.defaults            # piny, model TFT, wymiary, QIO 80MHz, PSRAM, USB JTAG
├── README.md
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                    # init, logo+splash+homing warning, peryferia, ELS, UI, SD, G-code, buzzer
│   └── idf_component.yml         # nopnop2002/ili9340
├── managed_components/
│   └── nopnop2002__ili9340/      # driver TFT + FONTX
├── components/
│   ├── display/                  # wrapper TFT, 7 fontow, SPIFFS, bitmapy
│   │   ├── font/                 # 8 plikow (7x FNT + logo.raw)
│   │   └── include/
│   ├── encoder/                  # PCNT kwadraturowy + GPIO ISR (4 przyciski + enkoder)
│   ├── axis/                     # generyczna os (Z i X), GPTimer, rampa accel/decel
│   │   └── include/
│   ├── stepper/                  # kompat. wsteczna dla osi Z (wrapper inline na axis)
│   ├── spindle/                  # wrzeciono krokowe + MOSFET + E-STOP ISR + callback
│ ├── motion/                   # ELS (Electronic Leadscrew), 30 presetow (metryczne/imperial/BSP/NPT), lewe gwinty
│ ├── limits/                   # krancowki + homing + blokada kierunku + soft-limity
│ ├── gcode/                    # parser G-code (G0/G1/G4/G20/G21/G90/G91/G92/M3/M4/M5/M30 + S-word inline)
│ ├── sdcard/                   # karta SD FATFS przez SPI2
│ ├── homing_state/             # globalny stan bazowania (g_homed)
│ ├── buzzer/                   # sygnalizator dzwiekowy PWM 4 kHz (GPIO48)
│ ├── wifi_server/              # WiFi AP + HTTP REST API + embedded web DRO page
│ ├── ble_server/               # BLE GATT server (JSON telemetry + commands)
│ └── ui_menu/                  # 13 ekranow UI, nawigacja enkoderem, NVS, screensaver
│       ├── homing_state.c/.h     # globalny stan bazowania (g_homed)
│       ├── screen_homing.inc     # ekran bazowania osi
│       ├── screen_backlight.inc  # ekran regulacji podswietlenia
│       ├── screen_els.inc        # ekran ELS gwintowania
│       ├── screen_gcode.inc      # ekran G-code z karty SD
│       ├── screen_axis_x.inc     # ekran osi X (JOG / AUTO / CYKL ZX)
│       └── screen_position.inc   # ekran pozycji i presetow (NVS)
└── tools/
    ├── generate_logo.py          # generuje testowe logo 48x48
    └── png_to_raw.py             # konwertuje PNG -> raw RGB565
```

## Ekrany UI

| # | Ekran | Dostep | Funkcja |
|---|-------|--------|---------|
|   | Menu | SW krotko | Lista 12 trybow |
| 1 | Dashboard | start | Live: RPM, Z pos, X pos, stany osi, status bazowania, SD, heap, wskaznik H/X |
| 2 | JOG Z | Menu | Enkoder = krok osi Z, 5 rozmiarow kroku, regulacja predkosci % |
| 3 | Posuw AUTO | Menu | Ciagly posuw osi Z, V_set/V_act, kierunek CW/CCW |
| 4 | Wrzeciono | Menu | Start/stop, RPM, kierunek FWD/REV, zasilanie, E-STOP reset |
| 5 | ELS | Menu | Gwintowanie, 30 presetow, lewe/prawe gwinty, pauza z retrakcja X, wskaznik gwintowy |
| 6 | Os X | Menu | JOG X, AUTO X, CYKL ZX (automatyczny cykl dosuwu) |
| 7 | Bazowanie osi | Menu | Homing Z i X, podejscie do krancowek |
| 8 | G-code (SD) | Menu | Lista plikow, progress bar, pauza/stop, wykonanie linii |
| 9 | Pozycja / Presety | Menu | Ustaw Z/X, 3 zapisywalne presety pozycji (NVS) |
| 10 | DRO | Menu | Maksymalnie duze cyfry Z/X, regulacja jasnosci na zywo |
| 11 | Ustawienia tokarki | Menu | Skok sruby Z/X, mikrostepping, max V, max RPM (NVS) |
| 12 | Ustawienia systemowe | Menu | **Podmenu**: Podswietlenie, WiFi ON/OFF, BLE ON/OFF |

---

## Funkcjonalnosci

### Sekwencja startowa
1. **Logo** — pelnoekranowa bitmapa (jesli `/spiffs/logo.raw` istnieje) lub tekst "Mini Lathe v6.1" — 5s
2. **Splash** — czarny ekran z niebieskim paskiem: ESP32-S3 / IDF 5.5 / 3x DM556+NEMA23 / ELS+E-STOP — 5s
3. **Ostrzezenie o braku bazowania** — migajacy czerwony ekran "! UWAGA ! Brak bazowania osi!" — 5s
4. Inicjalizacja: NVS → enkoder → osie Z/X → wrzeciono → ELS → UI (dashboard) → SD card (nieblokujaco) → G-code parser

### Sygnalizacja dzwiekowa (buzzer)
- Pasywny buzzer 4 kHz na GPIO48, sterowany PWM LEDC
- 4 wzorce sygnalow (nieblokujace, task):
  - **OK** — krotki pojedynczy (50 ms)
  - **WARN** — dwa krotkie (50+80+50 ms)
  - **ERROR** — dlugi (400 ms)
  - **E-STOP** — trzy krotkie + dlugi (awaria)
- Wywolywany automatycznie: E-STOP, koniec ELS, blad, potwierdzenia UI

### Wrzeciono
- Naped krokowy przez przekladnie slimakowa 1:6
- Rampa start/stop 3000 ms, regulacja RPM 10-120
- Kierunek FWD/REV
- Rejestracja pozycji katowej (do ELS) — licznik krokow absolutny
- Tryb awaryjny: MOSFET odcina 36V fizycznie
- E-STOP ISR: natychmiastowe zatrzymanie timera + odciecie zasilania
- Rejestracja callbacka obrotu (dla ELS) i E-STOP (dla UI)
- Mozliwosc ustawienia max RPM z UI (NVS)

### ELS (Electronic Leadscrew)
- **31 presetow gwintow**:
  - Metryczne: M3-M12, M14, M16, M20, M24 (11)
  - Trapezowe: Tr10, Tr12, Tr16 (3)
  - Calowe UNC: 1/4-20, 5/16-18, 3/8-16, 1/2-13 (4)
  - Calowe UNF: 1/4-28 (1)
  - **BSP (British Standard Pipe)**: G 1/8-28, G 1/4-19, G 3/8-19, G 1/2-14, G 3/4-14, G 1-11 (6)
  - **NPT (National Pipe Taper)**: 1/8-27, 1/4-18, 3/8-18, 1/2-14, 3/4-14 (6)
- Synchronizacja enkoder wrzeciona -> posuw Z przez Bresenham (ISR-safe task)
- Wieloprzejscia (1-20 przejsc) z automatycznym dosuwem X
- **5 parametrow konfiguracji**: preset gwintu, Z_start, Z_end, liczba przejsc, glebokosc X na przejscie (krok 0.02 mm, zakres 0.02–1.00 mm)
- **Ekran potwierdzenia** przed startem — podglad wszystkich parametrow
- BTN3 = zapisz Z_start, BTN3 (dlugi) = zapisz Z_end z biezacej pozycji
- **Lewe gwinty**: przełącznik PRAWY / LEWY (param_sel=5), odwraca kierunek posuwu
- **Pauza z retrakcja**: BTN2 podczas pracy = wycofaj X o 0.5mm + zatrzymaj Z; BTN2 ponownie = wznów
- **Wskaznik gwintowy**: pasek kata wrzeciona (0-100%)
- Zatrzymanie awaryjne: BTN1 LONG = E-STOP
- Podczas pracy: aktualny stan (IDLE/CZEKA/BIEG/POWRT/PAUZA/BLAD), przejscie, Z, RPM, status synchronizacji, liczba krokow, **szacowany czas pozostaly**

### Osie Z i X
- GPTimer z rampa akceleracji/deceleracji (20000 steps/s^2)
- 5 stanow: IDLE, RUN, ACCEL, DECEL, ERROR
- JOG krokowy (1/8/16/80/160 krokow) z regulacja predkosci (10-100%)
- Posuw ciagly z regulacja V_set (10-500 mm/min), wyswietlana V_act
- Move do pozycji absolutnej (axis_move_to_mm)
- Reset pozycji (SW dlugi w JOG = zero)
- Krancowki + pelny homing UI (Menu > Bazowanie osi)
- Blokada ruchu przed zhomowaniem — dozwolony tylko kierunek do krancowki home

### Os X — tryby rozszerzone
- **JOG X**: reczne pozycjonowanie enkoderem, 5 rozmiarow kroku
- **AUTO X**: zadaj cel absolutny + predkosc, BTN3 = wykonaj
- **CYKL ZX**: automatyczny cykl dosuwu:
  - Dosun X o zadana glebokosc na przejscie
  - Przejazd Z od start do end z predkoscia robocza
  - Powrot Z do startu
  - Powtorz dla N przejsc
  - Flaga abort (BTN1 = STOP cyklu)

### Ekran Pozycja / Presety
- Edycja i ustawianie pozycji Z i X
- 3 zapisywalne **presety pozycji** (P1, P2, P3) w NVS
- BTN3 = zastosuj wartosc (Z lub X)
- SW=skocz do presetu, BTN3=zapisz biezaca pozycje do presetu
- Trwale przez resek — zapisane w NVS

### Dashboard (ekran glowny)
- RPM: aktualne / zadane + kolor (zielony = at speed, pomaranczowy = ramping)
- Kierunek wrzeciona + status zasilania (>ON / <OF)
- Pozycja Z: X.XX mm + stan osi (IDL/RUN/ACC/DEC/ERR)
- Status bazowania per-os: **[OK]** lub **[--]**
- Pozycja X (jesli os X zainicjowana)
- **Ostrzezenie o krancowkach**: czerwony pasek "!!! KRANCOWKA !!!" gdy wyzwolona
- Rzeczywista predkosc posuwu: V: X.X mm/min
- **Status karty SD**: SD:OK / SD:--
- **Wolna pamiec heap**: XXXX K
- Czerwony krzyzyk "X" w stopce gdy brak bazowania
- Auto-odswiezanie co 200 ms

### Bezpieczenstwo
- **E-STOP**: styk NC grzybka odcina 36V sprzetowo; styk NO -> GPIO18 -> ISR wylacza MOSFET + zatrzymuje timery. Reset przez przycisk BTN2.
- **MOSFET PWM**: odcina zasilanie DM556 programowo (IO17)
- **Watchdog**: ESP32 task WDT 10s
- **Krancowki**: blokada ruchu w kierunku aktywnego limitu; ISR natychmiastowe zatrzymanie osi
- **Soft-limity**: programowe ograniczniki ruchu Z/X aktywne po zhomowaniu (nawet bez fizycznych krancowek)
- **E-STOP wszedzie**: scentralizowany handler — dziala na wszystkich 13 ekranach (wlacznie z Menu i Ustawieniami)
- **Sygnal dzwiekowy E-STOP**: 3 krotkie + 1 dlugi beep
- **Limit predkosci**: gorne ograniczenie w `axis_run()` chroni przed przekroczeniem max predkosci
- **Wygaszacz ekranu**: auto-dim do 15% po 60s bezczynnosci (nieaktywny podczas E-STOP)

### WiFi / Web Server
- **Tryb**: Access Point — ESP32 tworzy wlasna siec
- **SSID / haslo**: konfigurowalne w `wifi_server.c` lub NVS (`wifi_ssid` / `wifi_pass`)
- **Szyfrowanie**: WPA2-PSK (wymuszane)
- **Strona DRO**: `http://192.168.4.1` — pelny podglad Z/X/RPM + sterowanie (Jog, Spindle, E-STOP)
- **REST API**: `/api/status` (JSON), `/api/jog`, `/api/spindle`, `/api/estop`
- **Wlaczanie/wylaczanie**: Ustawienia systemowe → WiFi ON/OFF (wymaga restartu)

### BLE (Bluetooth Low Energy)
- **GATT serwer**: UUID serwisu `0x00FF`
- **DRO characteristic** (`0xFF01`): read + notify — JSON `{"z":0.00,"x":0.00,"rpm":0,"estop":0,"homed":0,"spindle":0}`
- **CMD characteristic** (`0xFF02`): write — komendy JSON `{"cmd":"jog","axis":"Z","steps":10}`
- **MAC**: widoczny w logu boot (`Bluetooth MAC: 44:1b:f6:d3:71:6e`)
- **Wlaczanie/wylaczanie**: Ustawienia systemowe → BLE ON/OFF (wymaga restartu, domyslnie OFF)

### Karta SD
- FAT32 przez SPI2 (wspoldzielona magistrala z TFT)
- Listowanie plikow G-code (.nc, .gco, .gcd, .txt, .g)
- Odczytywanie zawartosci plikow
- Info o pojemnosci (calkowita MB, wolna MB)
- Wykrywanie obecnosci — dashboard pokazuje SD:OK/SD:--

### G-code
- Parser G-code: G0, G1, G4, G20, G21, G90, G91, G92, M3, M4, M5, M30
- S-word (predkosc wrzeciona)
- Wykonywanie z karty SD z **paskiem postepu** (0-100%)
- Pauza / wznowienie (BTN2 w trakcie wykonania)
- MDI (exec pojedynczej linii) przez `gcode_exec_line()`
- Przeliczanie jednostek cal <-> mm
- Obsluga komentarzy (; i ())
- Wykrywanie krancowek przed ruchem
- Obsluga bledow z komunikatem

### Wyswietlacz
- Obsluga wielu modeli TFT (menuconfig): ST7735, ILI9340, ILI9341, ST7789, ST7796
- 7 fontow FONTX: 3x Gothic (16/24/32 px) + 3x Mincho (16/24/32 px) + 1x Latin (32 px)
- SPIFFS ~14.5 MB na fonty i bitmapy
- Rysowanie bitmap w formacie raw RGB565 (z RAM i z SPIFFS)
- Framebuffer (opcjonalnie, przez menuconfig)
- Regulacja podswietlenia PWM (ekran Podswietlenie + zapis NVS)
- **Adaptacyjne layouty UI** — wszystkie ekrany skaluja sie do rozdzielczosci (160x128 do 480x320)
- **Adaptacyjne fonty** — `FONT_LABEL` / `FONT_VALUE` / `FONT_HEADER` zalezne od `DISP_H` (FONT_LG na duzych ekranach)
- **Kompatybilnosc display_compat.h** — mapowanie scale->font dla starszego API

### Ustawienia (NVS)
Ustawienia przechowywane w NVS (trwale przez resek):
- **Tokarka**: Skok sruby Z (mm), Skok sruby X (mm), Mikrostepping, Max predkosc Z (mm/min), Max RPM wrzeciona
- **Systemowe**: WiFi ON/OFF, BLE ON/OFF, SSID, haslo WiFi
- **Podswietlenie**: Jasnosc (0-100%)
- **Presety**: 3 presety pozycji (Z+X)

---

## Kompilacja i flashowanie

```bash
# Konfiguracja (model TFT, piny, rozdzielczosc)
idf.py menuconfig

# Budowanie (kompiluje + tworzy obraz SPIFFS)
idf.py build

# Flashowanie (firmware + partycje + SPIFFS z fontami/bitmapami)
idf.py -p COMxx flash

# Monitor szeregowy (USB JTAG/Serial)
idf.py -p COMxx monitor
```

`sdkconfig.defaults` zawiera prekonfiguracje dla ESP32-S3: QIO 80MHz, 16MB flash, 8MB PSRAM, USB JTAG console, task WDT 10s, SPIFFS partition table.

---

## Bitmapy - format .raw

Projekt obsluguje bitmapy w surowym formacie RGB565 z 4-bajtowym naglowkiem:

```
[2B] szerokosc (uint16 LE)
[2B] wysokosc (uint16 LE)
[Nx2B] piksele RGB565, wiersz po wierszu
```

### Generowanie

```bash
# Logo testowe (zebatka 48x48)
python tools/generate_logo.py

# Konwersja PNG
python tools/png_to_raw.py ikona.png --size 48x48
python tools/png_to_raw.py logo.png -o components/display/font/logo.raw
```

### Uzycie w kodzie

```c
// Z pamieci RAM (inline bitmap)
display_draw_bitmap(x, y, width, height, data);

// Z pliku SPIFFS (wgrany automatycznie przy flash)
display_draw_bitmap_file(x, y, "/spiffs/logo.raw");
```

Pliki `.raw` wrzucone do `components/display/font/` sa automatycznie pakowane do obrazu SPIFFS przy `idf.py build` - nic nie trzeba konfigurowac.

---

## Opcje rozbudowy

- **Wiekszy wyswietlacz** - zmien model TFT w `menuconfig`, reszta kodu dziala automatycznie (adaptacyjne layouty UI)
- **Dodatkowe bitmapy** - wrzuc `.raw` do `components/display/font/` lub na karte SD
- **Nowe fonty** - dowolny FONTX, dodaj do katalogu, zaktualizuj `FONT_PATHS[]`, `g_font[]` i definicje w `display.h`
- **Nowe ekrany UI** - dodaj `screen_xxx.inc`, zarejestruj w `ui_menu.c` i `screen_id_t`
- **Nowe presety ELS** - rozszerz tablice `ELS_THREAD_PRESETS` w `components/motion/motion.c`
- **WiFi / Web Server** — juz zaimplementowane (AP + REST API + embedded DRO page)
- **Bluetooth BLE** — juz zaimplementowane (GATT server, JSON telemetry + komendy)
- **Joystick / pedal** — dodaj jako kolejny przycisk w `encoder.c`

---

## Procedura E-STOP

1. **Wcisniecie grzybka** -> styk NC -> odcina 36V sprzetowo (DM556 bez zasilania)
2. Styk NO -> GPIO18 = LOW -> ISR:
   - MOSFET IN = LOW (odcina DM556#1 programowo)
   - Wrzeciono -> STOP natychmiastowe
   - Os Z/Os X -> STOP natychmiastowe
   - Buzzer -> sygnal E-STOP (3 krotkie + dlugi)
3. **Reset**: zwolnij grzybek -> przejdz do ekranu Wrzeciono/Dashboard -> BTN2 = reset E-STOP
4. Zasilanie przywracane dopiero po jawnym `START`

---

## Pull-upy

- 4.7 kOhm na wszystkich wejsciach GPIO (przyciski, E-STOP, enkoder)
- Krancowki: zewnetrzne pull-upy (szczegolnie IO3 - input-only)

---

## Uwagi

- **IO12** - strapping pin (MTDI), uzywany jako SCLK. LOW przy boot = 3.3V flash. W praktyce OK.
- **IO2** - strapping pin, uzywany jako DIR osi X. OK po boot.
- **IO15** - strapping pin, uzywany jako BTN2. OK po boot.
- **IO20** - USB_D+, uzywany jako ENA wrzeciona. Aktywny tylko gdy USB nieaktywne. Przy debugowaniu przez USB JTAG/Serial - brak konfliktu.
- **IO46** - strapping pin (log level), uzywany jako X_MIN. OK po boot.
- **IO38** - BUILTIN LED na DevKitC-1. PWM podswietlenia TFT bedzie migac dioda na plytce.
- **IO48** - BUILTIN LED / wolny GPIO. Uzywany jako buzzer PWM. Dioda nie bedzie migac.
- **Kondensator 1000uF/63V** przy zaciskach zasilania DM556 - **zalecany**.

---

## Wskazówki pre-konstrukcyjne

### Uruchomienie krańcówek (limit switches)

Funkcje `limits_init()` i procedura homingu są domyślnie **wyłączone** — system pracuje bez
fizycznych krańcówek, wyłącznie na symulowanym bazowaniu (10-sekundowy timer w UI).

Aby włączyć rzeczywiste krańcówki i homing:

1. **Podłącz fizycznie krańcówki NC** do GPIO zgodnie z `components/limits/include/limits.h`:
   - `Z_MIN` = IO8
   - `Z_MAX` = IO3 (input-only — wymagany zewnętrzny pull-up)
   - `X_MIN` = IO46
   - `X_MAX` = IO45

2. **Odkomentuj** `limits_init()` w `main/main.c` oraz w
   `components/ui_menu/screen_homing.inc` wywołania `limits_home_axis()`.

3. **Skompiluj i flashuj** — `idf.py build flash`.

Po włączeniu:
- ISR krańcówek natychmiast zatrzymuje oś przy aktywacji limitu.
- `limits_can_move()` blokuje ruch w stronę aktywnego limitu.
- Przed zhomowaniem dozwolony jest tylko ruch w stronę krańcówki home (AXIS_DIR_NEG).
- Ekran główny pokazuje czerwony pasek `!!! KRANCOWKA !!!` przy wyzwolonej krańcówce.

### Testowanie ELS z głębokością skrawania

Ekran ELS ma teraz 5 parametrów (przełączanych przyciskiem SW):
1. Preset gwintu
2. Z start
3. Z koniec
4. Ilość przejść
5. **Głębokość X na przejście** (krok 0.02 mm, zakres 0.02–1.00 mm)

`depth_per_pass` jest przekazywany do `els_config_t` — oś X będzie dosuwana
o zadaną wartość przed każdym kolejnym przejściem. Domyślnie 0.10 mm/przejście.

Przed startem ELS pojawia się ekran potwierdzenia z podglądem wszystkich parametrów.
BTN3 zapisuje Z_start z bieżącej pozycji, BTN3 (długi) zapisuje Z_end.