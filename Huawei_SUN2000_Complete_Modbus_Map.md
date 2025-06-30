# HUAWEI SUN2000 - Kompletna mapa rejestrów Modbus TCP
# Dokumentacja wszystkich dostępnych danych invertera

## 📋 PODSTAWOWE INFORMACJE
- **Protokół**: Modbus TCP
- **Port**: 6607 (domyślny)
- **Slave ID**: 0 (domyślny)
- **Timeout**: 5 sekund (zalecany)
- **Kolejność bajtów**: Big Endian (MSB first)

---

## 📊 REJESTRY IDENTYFIKACYJNE (30000-30099)

### Model invertera
| Rejestr | Typ | Opis | Format | Przykład |
|---------|-----|------|--------|----------|
| 30000-30014 | String | Model invertera | 15 rejestrów ASCII | "SUN2000-8KTL-M1" |
| 30015-30024 | String | Numer seryjny | 10 rejestrów ASCII | "123456789ABCDEF" |
| 30025-30029 | String | Wersja firmware | 5 rejestrów ASCII | "V100R001C00" |
| 30030-30034 | String | Data produkcji | 5 rejestrów ASCII | "20240315" |
| 30035-30049 | String | Dodatkowe info | 15 rejestrów ASCII | Dodatkowe dane |
| 30070 | Uint16 | Typ urządzenia | 1 = Inverter | 0x0001 |
| 30071 | Uint16 | Nominalna moc [W] | Bez dzielnika | 8000 |
| 30072 | Uint16 | Maksymalna moc [W] | Bez dzielnika | 8800 |
| 30073 | Uint16 | Liczba faz | 1/3 | 3 |
| 30074 | Uint16 | Liczba stringów PV | 1-4 | 2 |

---

## ⚡ REJESTRY MOCY I ENERGII (32060-32120)

> **Uwaga:** Poniższe rejestry zostały potwierdzone jako działające na modelu SUN2000-8KTL-M1. W niektórych źródłach (np. niektóre biblioteki Python) podawane są inne rejestry (np. 32016-32019), które NIE zwracają poprawnych wartości na tym modelu.

### Moc wejściowa PV (DC) — DZIAŁAJĄCE REJESTRY
| Rejestr      | Typ    | Opis           | Dzielnik | Jednostka | Zakres     |
|-------------|--------|----------------|----------|-----------|------------|
| 32060       | Uint16 | Napięcie PV1   | 10       | V         | 0-1500V    |
| 32061       | Uint16 | Napięcie PV2   | 10       | V         | 0-1500V    |
| 32062       | Uint16 | Prąd PV1       | 100      | A         | 0-15A      |
| 32063       | Uint16 | Prąd PV2       | 100      | A         | 0-15A      |
| 32064+32065 | Uint32 | Moc PV1 [W]    | 1        | W         | 0-5000W    |
| 32066+32067 | Uint32 | Moc PV2 [W]    | 1        | W         | 0-5000W    |
| 32064+32065 | Uint32 | **Całkowita moc wejściowa** | 1 | W | 0-10000W |

> **Notatka:**
> - Rejestry 32016-32019 (PV1/2 Voltage/Current) są obecne w niektórych dokumentacjach, ale na modelu M1 zwracają zawsze 0 lub błędne wartości.
> - Dla poprawnego odczytu prądów i napięć PV należy używać wyłącznie 32060-32063.

### Moc wyjściowa AC (sieć)
| Rejestr | Typ | Opis | Dzielnik | Jednostka | Zakres |
|---------|-----|------|----------|-----------|--------|
| 32069 | Uint16 | Napięcie AC faza A | 10 | V | 180-280V |
| 32070 | Uint16 | Napięcie AC faza B | 10 | V | 180-280V |
| 32071 | Uint16 | Napięcie AC faza C | 10 | V | 180-280V |
| 32072+32073 | Int32 | Prąd AC faza A | 1000 | A | -50A do +50A |
| 32074+32075 | Int32 | Prąd AC faza B | 1000 | A | -50A do +50A |
| 32076+32077 | Int32 | Prąd AC faza C | 1000 | A | -50A do +50A |
| 32078+32079 | Uint32 | Moc aktywna faza A | 1 | W | 0-3000W |
| 32080+32081 | Uint32 | **Moc aktywna całkowita** | 1 | W | 0-8800W |
| 32082+32083 | Int32 | Moc reaktywna | 1 | VAr | -8800 do +8800 |
| 32084 | Uint16 | Współczynnik mocy | 1000 | - | 0.8-1.0 |
| 32085 | Uint16 | **Częstotliwość sieci** | 100 | Hz | 49.5-50.5Hz |

### Energia wyprodukowana
| Rejestr | Typ | Opis | Dzielnik | Jednostka | Resetowanie |
|---------|-----|------|----------|-----------|-------------|
| 32106+32107 | Uint32 | **Energia całkowita** | 100 | kWh | Nigdy |
| 32114+32115 | Uint32 | **Energia dzienna** | 100 | kWh | O północy |
| 32116+32117 | Uint32 | Energia miesięczna | 100 | kWh | 1. dnia miesiąca |
| 32118+32119 | Uint32 | Energia roczna | 100 | kWh | 1 stycznia |

---

## 🌡️ REJESTRY STANU I DIAGNOSTYKI (32080-32099)

### Stan pracy
| Rejestr | Typ | Opis | Wartości | Znaczenie |
|---------|-----|------|----------|-----------|
| 32089 | Uint16 | **Stan pracy** | 0-6 | Zobacz tabelę stanów |
| 32090 | Uint16 | **Kod błędu** | 0-9999 | Zobacz kody błędów |
| 32086 | Uint16 | **Sprawność** | 100 | % (0-100%) |
| 32087 | Uint16 | **Temperatura wewnętrzna** | 10 | °C (-40 do +80°C) |
| 32088 | Uint16 | Temperatura radiatora | 10 | °C (-40 do +120°C) |

### Stany pracy (rejestr 32089)
| Wartość | Stan | Opis | Normalność |
|---------|------|------|------------|
| 0 | Standby | Oczekiwanie (noc/brak słońca) | ✅ Normalny |
| 1 | Starting | Uruchamianie | ✅ Normalny |
| 2 | On-grid | Pracuje w sieci | ✅ Normalny |
| 3 | Grid-tied | Synchronizacja z siecią | ✅ Normalny |
| 4 | Off-grid | Praca wyspowa | ⚠️ Uwaga |
| 5 | Shutdown | Wyłączanie | ℹ️ Info |
| 6 | Fault | Awaria | 🚨 Błąd |

---

## 🚨 REJESTRY ALARMÓW I STATUSÓW (32000-32020)

### Statusy (patrz plik "Huawei SUN2000 - Statusy i Alarmy Bitowe.ini")
| Rejestr | Typ | Opis | Format |
|---------|-----|------|--------|
| 32002 | Uint16 | **STATE1** - Podstawowe statusy | 16 bitów |
| 32003 | Uint16 | **STATE2** - Statusy MPPT/zabezpieczeń | 16 bitów |
| 32004 | Uint16 | **STATE3** - Statusy komunikacji | 16 bitów |
| 32008 | Uint16 | **ALARM1** - Alarmy temperatury/wentylacji | 16 bitów |
| 32009 | Uint16 | **ALARM2** - Alarmy czujników | 16 bitów |
| 32010 | Uint16 | **ALARM3** - Alarmy systemowe | 16 bitów |

---

## 🔌 REJESTRY POMIARÓW DODATKOWYCH (32100-32150)

### Pomiary napięć i prądów
| Rejestr | Typ | Opis | Dzielnik | Jednostka |
|---------|-----|------|----------|-----------|
| 32100 | Uint16 | Napięcie DC Bus | 10 | V |
| 32101 | Uint16 | Prąd DC Bus | 100 | A |
| 32102 | Uint16 | Napięcie izolacji PV+ | 1 | V |
| 32103 | Uint16 | Napięcie izolacji PV- | 1 | V |
| 32104 | Uint16 | Rezystancja izolacji | 1 | kΩ |
| 32105 | Uint16 | Prąd upływu | 1000 | A |

### Pomiary sieci
| Rejestr | Typ | Opis | Dzielnik | Jednostka |
|---------|-----|------|----------|-----------|
| 32120 | Uint16 | Napięcie międzyfazowe AB | 10 | V |
| 32121 | Uint16 | Napięcie międzyfazowe BC | 10 | V |
| 32122 | Uint16 | Napięcie międzyfazowe CA | 10 | V |
| 32123 | Uint16 | THD napięcia | 100 | % |
| 32124 | Uint16 | THD prądu | 100 | % |

---

## 📈 REJESTRY STATYSTYK (32130-32170)

### Czasy pracy
| Rejestr | Typ | Opis | Jednostka |
|---------|-----|------|-----------|
| 32130+32131 | Uint32 | **Całkowity czas pracy** | godziny |
| 32132+32133 | Uint32 | Czas pracy dzisiaj | minuty |
| 32134 | Uint16 | Liczba uruchomień dzisiaj | - |
| 32135+32136 | Uint32 | Całkowita liczba uruchomień | - |

### Maxima dzienne
| Rejestr | Typ | Opis | Dzielnik | Jednostka |
|---------|-----|------|----------|-----------|
| 32140+32141 | Uint32 | Maksymalna moc dziś | 1 | W |
| 32142 | Uint16 | Maksymalna temperatura dziś | 10 | °C |
| 32143 | Uint16 | Maksymalne napięcie PV dziś | 10 | V |
| 32144 | Uint16 | Maksymalny prąd PV dziś | 100 | A |

---

## 🌐 REJESTRY KOMUNIKACJI (32180-32200)

### Status łączności
| Rejestr | Typ | Opis | Format |
|---------|-----|------|--------|
| 32180 | Uint16 | Status WiFi | Bit field |
| 32181 | Uint16 | Siła sygnału WiFi | dBm (-100 do 0) |
| 32182 | Uint16 | Status Ethernet | Bit field |
| 32183 | Uint16 | Status RS485 | Bit field |
| 32184 | Uint16 | Status Cloud | Bit field |
| 32185+32186 | Uint32 | Liczba połączeń Cloud | - |
| 32187+32188 | Uint32 | Ostatnie połączenie | Unix timestamp |

---

## 🔋 REJESTRY OPTYMALIZATORÓW (32200-32300)

### Informacje o optimizerach (jeśli podłączone)
| Rejestr | Typ | Opis | Format |
|---------|-----|------|--------|
| 32200 | Uint16 | Liczba optimizerów | - |
| 32201 | Uint16 | Optimizery online | - |
| 32202 | Uint16 | Optimizery z alarmami | - |
| 32210-32250 | Array | Napięcia optimizerów | 10x Uint16 |
| 32260-32300 | Array | Prądy optimizerów | 10x Uint16 |

---

## 📊 REJESTRY KONFIGURACJI (32400-32500)

### Parametry systemowe (tylko odczyt)
| Rejestr | Typ | Opis | Jednostka |
|---------|-----|------|-----------|
| 32400 | Uint16 | Nominalne napięcie AC | V |
| 32401 | Uint16 | Nominalna częstotliwość | Hz |
| 32402 | Uint16 | Maksymalne napięcie PV | V |
| 32403 | Uint16 | Minimalne napięcie PV | V |
| 32404 | Uint16 | Maksymalny prąd PV | A |
| 32405 | Uint16 | Nominalna moc | W |

---

## 💡 PRZYKŁADY UŻYCIA W KODZIE

### Podstawowy odczyt danych
```cpp
// Najważniejsze rejestry do monitorowania
uint16_t device_state = readHoldingRegister(32089);      // Stan urządzenia
uint32_t active_power = readHoldingRegister32(32080);    // Moc aktywna [W]
uint32_t daily_energy = readHoldingRegister32(32114);    // Energia dzienna [Wh]
uint32_t total_energy = readHoldingRegister32(32106);    // Energia całkowita [Wh]
uint16_t temperature = readHoldingRegister(32087);       // Temperatura [°C/10]
uint16_t efficiency = readHoldingRegister(32086);        // Sprawność [%]

// Alarmy i statusy
uint16_t state1 = readHoldingRegister(32002);           // Podstawowe statusy
uint16_t alarm1 = readHoldingRegister(32008);           // Podstawowe alarmy
uint16_t fault_code = readHoldingRegister(32090);       // Kod błędu
```

### Odczyt danych sieciowych
```cpp
// Napięcia AC
uint16_t ac_voltage_a = readHoldingRegister(32069);     // Faza A [V/10]
uint16_t ac_voltage_b = readHoldingRegister(32070);     // Faza B [V/10] 
uint16_t ac_voltage_c = readHoldingRegister(32071);     // Faza C [V/10]

// Prądy AC (signed!)
int32_t ac_current_a = (int32_t)readHoldingRegister32(32072); // [mA]
int32_t ac_current_b = (int32_t)readHoldingRegister32(32074); // [mA]
int32_t ac_current_c = (int32_t)readHoldingRegister32(32076); // [mA]

// Częstotliwość sieci
uint16_t frequency = readHoldingRegister(32085);        // [Hz/100]
```

### Odczyt danych PV
```cpp
// Napięcia i prądy PV
uint16_t pv1_voltage = readHoldingRegister(32060);      // [V/10]
uint16_t pv2_voltage = readHoldingRegister(32061);      // [V/10]
uint16_t pv1_current = readHoldingRegister(32062);      // [A/100]
uint16_t pv2_current = readHoldingRegister(32063);      // [A/100]

// Moce PV
uint32_t pv1_power = readHoldingRegister32(32064);     // [W]
uint32_t pv2_power = readHoldingRegister32(32066);     // [W]
uint32_t total_pv_power = pv1_power + pv2_power;       // Moc wejściowa całkowita
```

---

## 🛠️ NARZĘDZIA DIAGNOSTYCZNE

### Sprawdzanie krytycznych alarmów
```cpp
bool checkCriticalAlarms() {
    uint16_t alarm1 = readHoldingRegister(32008);
    uint16_t alarm2 = readHoldingRegister(32009);
    uint16_t alarm3 = readHoldingRegister(32010);
    
    // Alarmy krytyczne wymagające natychmiastowej reakcji
    bool critical = false;
    
    // ALARM1 - krytyczne
    if (alarm1 & 0x1000) critical = true; // Niska rezystancja izolacji
    if (alarm1 & 0x8000) critical = true; // Błąd AFCI (łuk elektryczny)  
    if (alarm1 & 0x0300) critical = true; // Odwrócona polaryzacja PV
    if (alarm1 & 0x0C00) critical = true; // PV doziemiony
    if (alarm1 & 0x0007) critical = true; // Wysoka temperatura
    
    // ALARM2 - zabezpieczenia sieciowe
    if (alarm2 & 0xE000) critical = true; // Zabezpieczenia napięcia/częstotliwości/wyspy
    if (alarm2 & 0x0400) critical = true; // Prąd upływu
    
    // ALARM3 - błędy systemowe
    if (alarm3 & 0x8000) critical = true; // Błąd krytyczny systemu
    if (alarm3 & 0x4000) critical = true; // Błąd aktualizacji firmware
    if (alarm3 & 0x1000) critical = true; // Błąd inicjalizacji
    
    return critical;
}
```

### Monitorowanie wydajności
```cpp
struct PerformanceData {
    double efficiency;        // Sprawność %
    double power_ratio;       // Stosunek mocy wyjściowej do wejściowej
    double daily_peak;        // Szczyt dzienny
    double temperature;       // Temperatura pracy
    
    void update() {
        uint16_t eff_reg = readHoldingRegister(32086);
        efficiency = eff_reg / 100.0;
        
        uint32_t input_power = readHoldingRegister32(32064);
        uint32_t output_power = readHoldingRegister32(32080);
        
        if (input_power > 0) {
            power_ratio = (double)output_power / input_power;
        }
        
        uint16_t temp_reg = readHoldingRegister(32087);
        temperature = temp_reg / 10.0;
    }
};
```

---

## 📋 LISTA KONTROLNA MONITOROWANIA

### Codziennie (automatycznie):
- [x] Moc aktywna (32080)
- [x] Energia dzienna (32114)  
- [x] Stan pracy (32089)
- [x] Temperatura (32087)
- [x] Alarmy krytyczne (32008, 32009, 32010)

### Co godzinę:
- [x] Sprawność (32086)
- [x] Napięcia AC (32069-32071)
- [x] Częstotliwość (32085)
- [x] Napięcia PV (32060-32061)

### Codziennie (raport):
- [x] Energia całkowita (32106)
- [x] Czasy pracy (32130)
- [x] Maxima dzienne (32140-32144)
- [x] Status komunikacji (32180-32188)

### Cotygodniowo:
- [x] Wszystkie statusy (32002-32004)
- [x] Rezystancja izolacji (32104)
- [x] THD napięcia/prądu (32123-32124)
- [x] Optimizery (32200-32202)

---

**Data utworzenia:** 18.06.2025  
**Autor:** System automatyczny na podstawie dokumentacji Huawei  
**Wersja:** 1.0  
**Źródło:** Huawei SUN2000 Modbus TCP Interface Specification
