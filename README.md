# Mini Lathe Controller v6.1

## Third-party components

This project uses code based on:

- [nopnop2002/esp-idf-ili9340](https://github.com/nopnop2002/esp-idf-ili9340)

Thanks to the original author.


Sterownik mini-tokarki CNC z jarzmem K11-3
i glowica podzialowa VEVOR BS4-KP100-57.

**Mikrokontroler**: ESP32-S3-WROOM-1 N16R8 (DevKitC-1)
**Framework**: ESP-IDF v5.5.1
**Display**: TFT SPI ST7735 / ILI9340 / ST7796 (modulowa obsluga przez menuconfig)
**Komunikacja**: USB JTAG/Serial, karta SD (FATFS przez SPI2), SPIFFS na fonty i bitmapy

---

## Sprzet

| Komponent | Model / parametry |
|---|---|
| Mikrokontroler | ESP32-S3-WROOM-1 N16R8, 16 MB Flash, 8 MB PSRAM |
| Wyswietlacz | ST7735 160x128 / ILI9340 320x240 / ST7796 480x320 |
| Silnik wrzeciona | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Silnik posuwu Z | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Silnik dosuwu X | NEMA23 57HD3403-16B, 3.75V, 2.5A/faze |
| Sterowniki | 3x DM556 (20-50V DC, do 5.6A, mikrostepping 12800 kr/obr) |
| Przekladnia wrzeciona | Glowica VEVOR BS4-KP100-57, slimakowa 1:6 |
| Zasilacz | 36V / 5.6A / 350VA SWPS | Active PFC Mean Well
| Bezpieczenstwo | Modul MOSFET PWM (5-55V, do 100A) odcina zasilanie DM556; E-STOP grzybkowy |
| SD card | FAT32 przez SPI2 (MOSI/SCLK/MISO wspoldzielone z TFT) |

---

## Mapa GPIO - kompletna (v6)

### SPI2 - magistrala wspoldzielona (TFT + karta SD)

Oo moje ustawienia TFT.
Ustawienia wyświetlacza TFT można zmienić w idf.py menuconfig.

| Sygnal | GPIO | Kierunek | Uwagi |
|---|---|---|---|
| MOSI | **11** | OUT | TFT (SDA) + SD (CMD) |
| SCLK | **12** | OUT | TFT (SCK) + SD (CLK) - strapping, OK po boot |
| MISO | **13** | IN | TFT (SDO) + SD (DAT0) |
| TFT CS | **10** | OUT | |
| SD CS | **44** | OUT | IO44 - wolny, bez konfliktu z LED (IO48) |
| TFT DC | **14** | OUT | Data/Command |
| TFT RST | **9** | OUT | |
| TFT BL | **38** | OUT | PWM LEDC do podswietlenia |

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
| Predkosc max | ~700 mm/min | ~120 mm/min |

> **Przed pierwszym ruchem** - zweryfikuj skok sruby i zaktualizuj `AXIS_Z_LEAD_MM` / `AXIS_X_LEAD_MM` w `components/axis/include/axis.h`.

---

## Struktura projektu

```
mini_lathe_v6/
├── CMakeLists.txt                # SPIFFS build (fonty + bitmapy)
├── partitions.csv                # 16 MB flash: factory + spiffs
├── sdkconfig.defaults            # piny, model TFT, wymiary
├── README.md
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                    # init, splash, bitmap demo
│   └── idf_component.yml         # nopnop2002/ili9340
├── managed_components/
│   └── nopnop2002__ili9340/      # driver TFT + FONTX
├── components/
│   ├── display/                  # wrapper TFT, 7 fontow, SPIFFS, bitmapy
│   │   ├── font/                 # 7x FNT + logo.raw
│   │   └── include/
│   ├── encoder/                  # PCNT kwadraturowy + GPIO ISR
│   ├── axis/                     # generyczna os (Z i X), GPTimer, rampa
│   ├── stepper/                  # kompat. wsteczna os Z
│   ├── spindle/                  # wrzeciono krokowe + MOSFET + E-STOP ISR
│   ├── motion/                   # ELS (Electronic Leadscrew), 19 presetow
│   ├── limits/                   # krancowki + homing
│   ├── gcode/                    # parser G-code
│   ├── sdcard/                   # karta SD FATFS przez SPI2
│   └── ui_menu/                  # 8 ekranow UI, nawigacja enkoderem
└── tools/
    ├── generate_logo.py          # generuje testowe logo 48x48
    └── png_to_raw.py             # konwertuje PNG -> raw RGB565
```

## Ekrany UI

| # | Ekran | Dostep | Funkcja |
|---|-------|--------|---------|
| 1 | Dashboard | start | Live: RPM, Z pos, X pos, stany osi |
| 2 | Menu | SW krotko | Lista trybow |
| 3 | JOG Z | Menu | Enkoder = krok osi Z |
| 4 | Posuw AUTO | Menu | Ciagly posuw osi Z |
| 5 | Wrzeciono | Menu | Start/stop, RPM, kierunek |
| 6 | Ustawienia | Menu | Skok sruby, mikrostepping |
| 7 | ELS | Menu | Gwintowanie, 19 presetow srubowych/calowych |
| 8 | Os X | Menu | JOG X, AUTO X, cykl ZX |

---

## Funkcjonalnosci

### Wrzeciono
- Naped krokowy przez przekladnie slimakowa 1:6
- Rampa start/stop 3000 ms, regulacja RPM 10-120
- Kierunek FWD/REV
- Rejestracja pozycji katowej (do ELS)
- Tryb awaryjny: MOSFET odcina 36V fizycznie

### ELS (Electronic Leadscrew)
- **19 presetow gwintow** - metryczne (M3-M12) i calowe (1/4"-1")
- Synchronizacja enkoder wrzeciona -> posuw Z przez Bresenham
- Wieloprzejscia (1-20 przejsc)
- Pomiar Z_start / Z_end z enkodera
- Zatrzymanie awaryjne: BTN1 LONG = E-STOP

### Osie Z i X
- GPTimer z rampa akceleracji/deceleracji (20000 steps/s^2)
- JOG krokowy (0.01 / 0.1 / 1.0 mm) i posuw ciagly
- Move do pozycji absolutnej
- Krancowki + homing (do aktywacji)

### Bezpieczenstwo
- **E-STOP**: styk NC grzybka odcina 36V sprzetowo; styk NO -> GPIO18 -> ISR wylacza MOSFET + zatrzymuje timery. Reset przez przycisk.
- **MOSFET PWM**: odcina zasilanie DM556 programowo (IO17)
- **Watchdog**: ESP32 task WDT 10s
- **Krancowki**: blokada ruchu w kierunku aktywnego limitu

### Karta SD
- FAT32 przez SPI2 (wspoldzielona magistrala z TFT)
- Listowanie plikow G-code (.nc, .gco, .gcd, .txt, .g)
- Odczytywanie zawartosci plikow
- Info o pojemnosci

### Wyswietlacz
- Obsluga wielu modeli TFT (menuconfig): ST7735, ILI9340, ILI9341, ST7789, ST7796
- 7 fontow FONTX: 3x Gothic (8/12/16px) + 3x Mincho (8/12/16px) + 1x Latin
- SPIFFS 14.5 MB na fonty i bitmapy
- Rysowanie bitmap w formacie raw RGB565 (z RAM i z SPIFFS)
- Framebuffer (opcjonalnie, przez menuconfig)

### G-code
- Parser G-code (G0, G1, G90, G91, M3, M5 itp.)
- Wykonywanie z karty SD

---

## Kompilacja i flashowanie

```bash
# Konfiguracja (model TFT, piny, rozdzielczosc)
idf.py menuconfig

# Budowanie (kompiluje + tworzy obraz SPIFFS)
idf.py build

# Flashowanie (firmware + partycje + SPIFFS z fontami/bitmapami)
idf.py -p COMxx flash

# Monitor szeregowy
idf.py -p COMxx monitor
```

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
- **Nowe ekrany UI** - dodaj `screen_xxx.inc`, zarejestruj w `ui_menu.c`
- **Nowe presety ELS** - rozszerz tablice `ELS_THREAD_PRESETS` w `components/motion/motion.c`
- **Bluetooth / WiFi** - ESP32-S3 ma wbudowane BLE + WiFi
- **Joystick / pedal** - dodaj jako kolejny przycisk w `encoder.c`

---

## Procedura E-STOP

1. **Wcisniecie grzybka** -> styk NC -> odcina 36V sprzetowo (DM556 bez zasilania)
2. Styk NO -> GPIO18 = LOW -> ISR:
   - MOSFET IN = LOW (odcina DM556#1 programowo)
   - Wrzeciono -> STOP natychmiastowe
   - Os Z/Os X -> STOP natychmiastowe
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
- **Kondensator 1000uF/63V** przy zaciskach zasilania DM556 - **zalecany**.