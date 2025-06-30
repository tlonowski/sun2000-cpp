# Huawei SUN2000 FTXUI Dashboard

Interaktywny dashboard w terminalu do monitorowania inwertera Huawei SUN2000 w czasie rzeczywistym, wykorzystujący protokół Modbus TCP/IP i bibliotekę FTXUI.

## Funkcje

- **Dashboard w czasie rzeczywistym** - nowoczesny interfejs terminalowy
- **Monitorowanie PV** - napięcia i prądy paneli słonecznych (PV1, PV2)
- **Parametry AC** - napięcia, prądy i częstotliwość sieci
- **Monitoring mocy** - moc aktywna, bierna i pozorna
- **Statystyki energii** - produkcja dzienna i całkowita
- **Temperatura** - monitoring temperatury inwertera
- **Zweryfikowane rejestry** - używa sprawdzonych adresów dla modelu M1

## Wymagania

- Linux (testowane na Ubuntu/Debian)
- Połączenie sieciowe z inverterem Huawei SUN2000
- Biblioteki: libmodbus-dev, cmake, make

## Instalacja

1. Zainstaluj zależności systemowe:
```bash
sudo apt update
sudo apt install libmodbus-dev cmake make build-essential
```

2. Skompiluj program:
```bash
make -f Makefile.ftxui
```

## Użycie

### Podstawowe uruchomienie
```bash
./sun_ftxui
```

Program uruchomi interaktywny dashboard w terminalu, który będzie odświeżał dane co 2 sekundy.

### Sterowanie
- **q** lub **Ctrl+C** - wyjście z programu
- Dashboard automatycznie odświeża dane w czasie rzeczywistym

### Konfiguracja IP inwertera
Domyślnie program łączy się z adresem `10.88.45.1`. Aby zmienić adres, edytuj zmienną `INVERTER_IP` w pliku `sun_ftxui.cpp` i przekompiluj.
## Wyświetlane dane

Dashboard pokazuje następujące informacje:

### Panele PV
- **PV1 Voltage** - napięcie string 1 [V] (rejestr 32060)
- **PV1 Current** - prąd string 1 [A] (rejestr 32061)  
- **PV2 Voltage** - napięcie string 2 [V] (rejestr 32062)
- **PV2 Current** - prąd string 2 [A] (rejestr 32063)

### Parametry AC
- **AC Voltage A** - napięcie AC faza A [V] (rejestr 32069)
- **AC Current A** - prąd AC faza A [A] (rejestr 32072)
- **Grid Frequency** - częstotliwość sieci [Hz] (rejestr 32085)

### Moc i energia
- **Active Power** - moc aktywna [W] (rejestry 32080-32081)
- **Reactive Power** - moc bierna [var] (rejestry 32082-32083)
- **Apparent Power** - moc pozorna [VA] (obliczana)
- **Daily Energy** - energia dzienna [kWh] (rejestry 32114-32115)
- **Total Energy** - energia całkowita [kWh] (rejestry 32106-32107)

### Status
- **Inverter State** - stan inwertera (rejestr 32000)
- **Temperature** - temperatura [°C] (rejestr 32087)

## Konfiguracja sieci

Program wymaga połączenia z inverterem Huawei SUN2000:
- **Metoda 1**: Bezpośrednie połączenie WiFi z hotspot inwertera (zalecane)
  - SSID: zazwyczaj `SUN2000-XXXXXXXXX`
  - IP inwertera: `10.88.45.1` (domyślnie)
- **Metoda 2**: Połączenie przez sieć domową (jeśli inwerter jest podłączony do routera)

## Zweryfikowane rejestry Modbus

Program używa **zweryfikowanych w praktyce** adresów rejestrów dla modelu SUN2000-8KTL-M1:

### ⚠️ WAŻNE - Rejestry PV
- **POPRAWNE**: 32060-32063 (PV1/PV2 napięcia i prądy) ✅
- **BŁĘDNE**: 32016-32019 (często podawane w dokumentacjach, ale nie działają na M1) ❌

Pełna lista zweryfikowanych rejestrów znajduje się w pliku `Huawei_SUN2000_Complete_Modbus_Map.md`.

## Rozwiązywanie problemów

### 1. Brak połączenia z inverterem
```bash
# Sprawdź połączenie
ping 10.88.45.1

# Sprawdź czy port Modbus jest otwarty
telnet 10.88.45.1 6607
```

### 2. Błędy kompilacji FTXUI
```bash
# Usuń build i spróbuj ponownie
make -f Makefile.ftxui clean
make -f Makefile.ftxui
```

### 3. Błędy odczytu rejestrów
- Sprawdź czy Modbus TCP jest włączony w inwerterze
- Upewnij się, że używasz poprawnego IP
- Niektóre rejestry mogą być niedostępne w zależności od modelu

### 4. Dashboard nie wyświetla się poprawnie
- Sprawdź czy terminal obsługuje kolory i UTF-8
- Spróbuj powiększyć okno terminala
- Na niektórych terminalach może być potrzebne ustawienie `TERM=xterm-256color`

## Budowa projektu

```
huawei_cpp/
├── sun_ftxui.cpp              # Główny kod aplikacji
├── Makefile.ftxui             # Makefile do budowania
├── ftxui/                     # Biblioteka FTXUI (submoduł)
├── README.md                  # Dokumentacja
├── Huawei_SUN2000_Complete_Modbus_Map.md  # Mapa rejestrów
└── Huawei SUN2000 - Statusy i Alarmy Bitowe.ini  # Definicje alarmów
```

## Licencja

Projekt open source. Biblioteka FTXUI używana na licencji MIT.

## Autor

Program stworzony dla monitorowania inwertera Huawei SUN2000 z weryfikacją rejestrów dla modelu M1.
