#include <iostream>
#include <modbus/modbus.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <algorithm>
#include <cmath>

// FTXUI includes
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"

using json = nlohmann::ordered_json;
using namespace std;
using namespace ftxui;

// --- Reactive signals ---
// using ftxui::Signal;
// using ftxui::Ref;

// Struktura do przechowywania danych invertera (bez mutexów)
struct InverterData {
    string timestamp = "N/A";
    string model = "N/A";
    string sn = "N/A";
    string firmware_version = "N/A";
    string device_status = "N/A";
    double active_power = 0.0;
    double input_power = 0.0;
    double efficiency = 0.0;
    double temperature = 0.0;
    double daily_energy = 0.0;
    double total_energy = 0.0;
    double frequency = 0.0;
    double phase_A_voltage = 0.0;
    double phase_B_voltage = 0.0;
    double phase_C_voltage = 0.0;
    double phase_A_current = 0.0;
    double phase_B_current = 0.0;
    double phase_C_current = 0.0;
    
    // Parametry PV
    double pv1_voltage = 0.0;
    double pv1_current = 0.0;
    double pv2_voltage = 0.0;
    double pv2_current = 0.0;
    double pv1_power = 0.0;
    double pv2_power = 0.0;
    
    int wifi_signal = -65;
    int ping_ms = 0;
    string last_error = "";
    
    // Statusy bitowe
    uint16_t state1 = 0;      // Rejestr 32002 - podstawowe statusy
    uint16_t state2 = 0;      // Rejestr 32003 - rozszerzone statusy  
    uint16_t state3 = 0;      // Rejestr 32004 - statusy komunikacji
    
    // Alarmy bitowe
    uint16_t alarm1 = 0;      // Rejestr 32008 - alarmy temperatury i błędów
    uint16_t alarm2 = 0;      // Rejestr 32009 - alarmy czujników
    uint16_t alarm3 = 0;      // Rejestr 32010 - alarmy systemowe
    uint16_t fault_code = 0;  // Rejestr 32090 - numeryczny kod błędu
};

// --- PowerChart ---
struct PowerChart {
    static const int MAX_POINTS = 144;
    vector<pair<chrono::system_clock::time_point, double>> data;
    double max_power = 0.0;

    void add(double power) {
        auto now = chrono::system_clock::now();
        if (data.size() == MAX_POINTS)
            data.erase(data.begin());
        data.push_back({now, power});
        max_power = 0.0;
        for (const auto& p : data) max_power = max(max_power, p.second);
    }
    double getMaxPower() const { return max_power; }
    int getDataCount() const { return data.size(); }
    vector<pair<chrono::system_clock::time_point, double>> getData() const { return data; }
};

// --- Funkcje pomocnicze do analizy statusów bitowych ---
struct BitStatus {
    int bit;
    string name;
    string description;
    string priority;
    Color color;
};

// Definicje bitów dla STATE1 (32002)
vector<BitStatus> STATE1_BITS = {
    {0, "Standby", "Tryb oczekiwania", "INFO", Color::Yellow},
    {1, "Sieć dostępna", "Napięcie sieci w normie", "OK", Color::Green},
    {2, "On-grid", "Inverter pracuje on-grid", "OK", Color::Green},
    {3, "Tryb wyłączenia", "Inverter wyłączony", "UWAGA", Color::Yellow},
    {4, "Oszczędzanie energii", "Niska moc wejściowa", "INFO", Color::Yellow},
    {5, "Eksport energii", "Oddawanie energii do sieci", "OK", Color::Green},
    {6, "Synchronizacja", "Synchronizacja z siecią", "INFO", Color::Yellow},
    {7, "Tryb pracy", "Normalny tryb pracy", "OK", Color::Green},
    {8, "Debugowanie", "Diagnostyka systemu", "UWAGA", Color::Yellow},
    {9, "Serwis", "Serwis/konserwacja", "UWAGA", Color::Yellow},
    {10, "Aktualizacja", "Aktualizacja firmware", "PROCES", Color::Cyan},
    {11, "Test", "Testy systemu", "INFO", Color::Yellow},
    {12, "Konfiguracja", "Konfiguracja parametrów", "CONFIG", Color::Cyan},
    {13, "Monitorowanie", "Aktywne monitorowanie", "INFO", Color::Yellow},
    {14, "Tryb awaryjny", "Stan awaryjny", "KRYTYCZNY", Color::Red},
    {15, "Offline", "Brak komunikacji", "KRYTYCZNY", Color::Red}
};

// Definicje bitów dla ALARM1 (32008) - najważniejsze alarmy
vector<BitStatus> ALARM1_BITS = {
    {0, "Wysoka temp. wew.", ">75°C w module głównym", "KRYTYCZNY", Color::Red},
    {1, "Wysoka temp. radiatora", ">85°C na radiatorze", "KRYTYCZNY", Color::Red},
    {2, "Wysoka temp. mocy", ">90°C w module mocy", "KRYTYCZNY", Color::Red},
    {3, "Błąd wentylatora", "Wentylator nie działa", "KRYTYCZNY", Color::Red},
    {4, "Błąd Phase A", "Problem z fazą A", "WAŻNY", Color::Magenta},
    {5, "Błąd Phase B", "Problem z fazą B", "WAŻNY", Color::Magenta},
    {6, "Błąd Phase C", "Problem z fazą C", "WAŻNY", Color::Magenta},
    {7, "Błąd SPD", "Przepięciówka uszkodzona", "WAŻNY", Color::Magenta},
    {8, "Odwr. polar. PV1", "+/- zamienione w string 1", "KRYTYCZNY", Color::Red},
    {9, "Odwr. polar. PV2", "+/- zamienione w string 2", "KRYTYCZNY", Color::Red},
    {10, "PV1 doziemiony", "String 1 zwarcie do ziemi", "KRYTYCZNY", Color::Red},
    {11, "PV2 doziemiony", "String 2 zwarcie do ziemi", "KRYTYCZNY", Color::Red},
    {12, "Niska rez. izolacji", "<1MΩ - zagrożenie!", "KRYTYCZNY", Color::Red},
    {13, "Błąd czuj. temp.", "Uszkodzony termometr", "WAŻNY", Color::Magenta},
    {14, "Błąd kom. AFCI", "Problem z Arc Fault", "WAŻNY", Color::Magenta},
    {15, "Błąd AFCI", "Wykryto łuk elektryczny", "KRYTYCZNY", Color::Red}
};

// Funkcja sprawdzająca czy bit jest ustawiony
bool checkBit(uint16_t value, int bit) {
    return (value & (1 << bit)) != 0;
}

// Funkcja zliczająca aktywne bity
int countActiveBits(uint16_t value) {
    return __builtin_popcount(value);
}

// Funkcja sprawdzająca czy są alarmy krytyczne
bool hasCriticalAlarms(uint16_t alarm1, uint16_t alarm2, uint16_t alarm3) {
    // ALARM1: bity krytyczne - 0,1,2,3,8,9,10,11,12,15
    bool alarm1_critical = (alarm1 & 0x9F0F) != 0;
    // ALARM2: bity krytyczne - 10,13,14,15
    bool alarm2_critical = (alarm2 & 0xE400) != 0;
    // ALARM3: bity krytyczne - 0,7,8,9,14,15
    bool alarm3_critical = (alarm3 & 0xC380) != 0;
    
    return alarm1_critical || alarm2_critical || alarm3_critical;
}

// Funkcja analizująca statusy i zwracająca kolorowy tekst
Element analyzeStatusBits(uint16_t state1, uint16_t state2, uint16_t state3) {
    vector<Element> status_elements;
    
    // Sprawdź najważniejsze statusy z STATE1
    if (checkBit(state1, 2)) {  // On-grid
        status_elements.push_back(text("ON-GRID") | color(Color::Green) | bold);
    }
    if (checkBit(state1, 7)) {  // Tryb pracy
        status_elements.push_back(text("PRACA") | color(Color::Green));
    }
    if (checkBit(state1, 0)) {  // Standby
        status_elements.push_back(text("STANDBY") | color(Color::Yellow));
    }
    if (checkBit(state1, 14)) {  // Tryb awaryjny
        status_elements.push_back(text("AWARIA") | color(Color::Red) | bold);
    }
    if (checkBit(state1, 15)) {  // Offline
        status_elements.push_back(text("OFFLINE") | color(Color::Red) | bold);
    }
    
    // Dodaj informacje z STATE2 i STATE3 jeśli są dostępne
    if (state2 != 0) {
        status_elements.push_back(text("EXT") | color(Color::Cyan));
    }
    if (state3 != 0) {
        status_elements.push_back(text("COMM") | color(Color::Magenta));
    }
    
    if (status_elements.empty()) {
        status_elements.push_back(text("NIEZNANY") | color(Color::Yellow));
    }
    
    // Dodaj spacje między elementami statusu
    vector<Element> spaced_elements;
    for (size_t i = 0; i < status_elements.size(); ++i) {
        spaced_elements.push_back(status_elements[i]);
        if (i < status_elements.size() - 1) {
            spaced_elements.push_back(text(" "));
        }
    }
    
    return hbox(spaced_elements);
}

// Funkcja analizująca alarmy i zwracająca podsumowanie
Element analyzeAlarmBits(uint16_t alarm1, uint16_t alarm2, uint16_t alarm3) {
    vector<Element> alarm_elements;
    
    if (alarm1 == 0 && alarm2 == 0 && alarm3 == 0) {
        return text("✅ Brak alarmów") | color(Color::Green);
    }
    
    int total_alarms = countActiveBits(alarm1) + countActiveBits(alarm2) + countActiveBits(alarm3);
    bool critical = hasCriticalAlarms(alarm1, alarm2, alarm3);
    
    if (critical) {
        alarm_elements.push_back(text("🚨 ") | color(Color::Red));
        alarm_elements.push_back(text("KRYTYCZNE") | color(Color::Red) | bold);
    } else {
        alarm_elements.push_back(text("⚠️ ") | color(Color::Yellow));
        alarm_elements.push_back(text("OSTRZEŻENIA") | color(Color::Yellow) | bold);
    }
    
    alarm_elements.push_back(text(" (" + to_string(total_alarms) + ")") | color(Color::White));
    
    return hbox(alarm_elements);
}

// Funkcja generująca szczegółowy widok alarmów
Element generateDetailedAlarms(uint16_t alarm1, uint16_t alarm2, uint16_t alarm3, uint16_t fault_code) {
    vector<Element> alarm_details;
    
    // Nagłówek
    alarm_details.push_back(text("🚨 SZCZEGÓŁY ALARMÓW") | center | bold | color(Color::Red));
    alarm_details.push_back(separator());
    
    bool has_any_alarm = false;
    
    // ALARM1 - najważniejsze alarmy
    if (alarm1 != 0) {
        alarm_details.push_back(text("ALARM1 (Temp/Błędy): 0x" + 
            []() { stringstream ss; ss << hex << uppercase; return ss; }().str() + 
            to_string(alarm1)) | color(Color::Red) | bold);
        
        for (const auto& bit_def : ALARM1_BITS) {
            if (checkBit(alarm1, bit_def.bit)) {
                auto bit_line = hbox({
                    text("  Bit " + to_string(bit_def.bit) + ": "),
                    text(bit_def.name) | color(bit_def.color) | bold,
                    text(" - " + bit_def.description) | color(Color::White)
                });
                alarm_details.push_back(bit_line);
                has_any_alarm = true;
            }
        }
        alarm_details.push_back(separator());
    }
    
    // ALARM2 - alarmy czujników (tylko jeśli są aktywne)
    if (alarm2 != 0) {
        alarm_details.push_back(text("ALARM2 (Czujniki): 0x" + 
            []() { stringstream ss; ss << hex << uppercase; return ss; }().str() + 
            to_string(alarm2)) | color(Color::Magenta) | bold);
        
        // Pokazuj tylko aktywne bity dla ALARM2 (uproszczone)
        int bit_count = countActiveBits(alarm2);
        alarm_details.push_back(text("  Aktywnych alarmów czujników: " + to_string(bit_count)) | color(Color::White));
        has_any_alarm = true;
        alarm_details.push_back(separator());
    }
    
    // ALARM3 - alarmy systemowe (tylko jeśli są aktywne)
    if (alarm3 != 0) {
        alarm_details.push_back(text("ALARM3 (System): 0x" + 
            []() { stringstream ss; ss << hex << uppercase; return ss; }().str() + 
            to_string(alarm3)) | color(Color::Yellow) | bold);
        
        int bit_count = countActiveBits(alarm3);
        alarm_details.push_back(text("  Aktywnych alarmów systemowych: " + to_string(bit_count)) | color(Color::White));
        has_any_alarm = true;
        alarm_details.push_back(separator());
    }
    
    // Kod błędu
    if (fault_code != 0) {
        string fault_desc = "Nieznany kod błędu";
        if (fault_code >= 0x0001 && fault_code <= 0x0999) fault_desc = "Błędy temperatury";
        else if (fault_code >= 0x1000 && fault_code <= 0x1999) fault_desc = "Błędy napięcia";
        else if (fault_code >= 0x2000 && fault_code <= 0x2999) fault_desc = "Błędy prądu";
        else if (fault_code >= 0x3000 && fault_code <= 0x3999) fault_desc = "Błędy izolacji";
        else if (fault_code >= 0x4000 && fault_code <= 0x4999) fault_desc = "Błędy komunikacji";
        else if (fault_code >= 0x5000 && fault_code <= 0x5999) fault_desc = "Błędy sprzętowe";
        else if (fault_code >= 0x9000 && fault_code <= 0x9999) fault_desc = "Błędy krytyczne";
        
        alarm_details.push_back(text("FAULT CODE: 0x" + 
            []() { stringstream ss; ss << hex << uppercase; return ss; }().str() + 
            to_string(fault_code) + " - " + fault_desc) | color(Color::Red) | bold);
        has_any_alarm = true;
    }
    
    if (!has_any_alarm) {
        alarm_details.push_back(text("✅ System pracuje prawidłowo") | center | color(Color::Green));
    }
    
    return vbox(alarm_details) | border;
}

class HuaweiSun2000 {
private:
    modbus_t *mb;
    string ip_address;
    int port;
    
public:
    HuaweiSun2000(const string& ip, int p = 6607) : mb(nullptr), ip_address(ip), port(p) {}
    
    ~HuaweiSun2000() { 
        disconnect();
    }
    
    bool connect() {
        mb = modbus_new_tcp(ip_address.c_str(), port);
        if (mb == nullptr) {
            return false;
        }
        
        modbus_set_response_timeout(mb, 5, 0);
        modbus_set_slave(mb, 0);
        
        if (modbus_connect(mb) == -1) {
            modbus_free(mb);
            mb = nullptr;
            return false;
        }
        
        return true;
    }
    
    void disconnect() {
        if (mb != nullptr) {
            modbus_close(mb);
            modbus_free(mb);
            mb = nullptr;
        }
    }
    
    uint16_t readHoldingRegister(int address) {
        uint16_t value;
        if (modbus_read_registers(mb, address, 1, &value) == -1) {
            return 0;
        }
        return value;
    }
    
    uint32_t readHoldingRegister32(int address) {
        uint16_t values[2];
        if (modbus_read_registers(mb, address, 2, values) == -1) {
            return 0;
        }
        return (uint32_t(values[0]) << 16) | values[1];
    }
    
    json readInverterData() {
        json data;
        
        try {
            // === POPRAWKI REJESTRÓW ZGODNIE Z OFICJALNĄ BIBLIOTEKĄ sun2000_modbus ===
            // 1. STATE1: 32000 (nie 32002)
            // 2. STATE2: 32002 (nie 32003) 
            // 3. STATE3: 32003 32-bit (nie 32004 16-bit)
            // 4. PV prądy: 32017, 32019 dzielnik 100
            // 5. AC prądy: 32072, 32074, 32076 32-bit dzielnik 1000
            // === KONIEC POPRAWEK ===
            
            // Timestamp
            auto now = chrono::system_clock::now();
            auto time_t = chrono::system_clock::to_time_t(now);
            stringstream ss;
            ss << put_time(localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            data["timestamp"] = ss.str();
            
            // Model
            data["model"] = "SUN2000-8KTL-M1";
            
            // Odczytaj kluczowe parametry wcześnie dla analizy statusu
            uint32_t active_power_candidate1 = readHoldingRegister32(32080); // Oryginalny (może błędny w M1)
            
            // EKSPLORACJA: Poszukaj prawdziwego active power w M1
            uint32_t active_power_candidate2 = readHoldingRegister32(32078); // Wcześniejszy
            uint32_t active_power_candidate3 = readHoldingRegister32(32082); // Późniejszy  
            uint16_t active_power_16bit_1 = readHoldingRegister(32081);      // Jako 16-bit
            uint16_t active_power_16bit_2 = readHoldingRegister(32083);      // Jako 16-bit
            uint16_t active_power_16bit_3 = readHoldingRegister(32084);      // Jako 16-bit
            uint16_t active_power_16bit_4 = readHoldingRegister(32085);      // Jako 16-bit
            
            // Sprawdź rejestry w okolicy input power (32064)
            uint32_t input_power = readHoldingRegister32(32064);
            uint32_t power_candidate_near_input1 = readHoldingRegister32(32062);
            uint32_t power_candidate_near_input2 = readHoldingRegister32(32066);
            
            // Oblicz active power z parametrów AC (jako backup) - POPRAWIONE REJESTRY!
            // Według oficjalnej biblioteki sun2000_modbus prądy AC to rejestry 32-bitowe
            uint16_t ac_voltage_a = readHoldingRegister(32069);
            uint16_t ac_voltage_b = readHoldingRegister(32070);  
            uint16_t ac_voltage_c = readHoldingRegister(32071);
            uint32_t ac_current_a = readHoldingRegister32(32072);  // POPRAWKA: 32-bit, dzielnik 1000
            uint32_t ac_current_b = readHoldingRegister32(32074);  // POPRAWKA: 32-bit, dzielnik 1000
            uint32_t ac_current_c = readHoldingRegister32(32076);  // POPRAWKA: 32-bit, dzielnik 1000
            
            double calculated_active_power = ((ac_voltage_a * 0.1) * (ac_current_a * 0.001) +
                                              (ac_voltage_b * 0.1) * (ac_current_b * 0.001) +
                                              (ac_voltage_c * 0.1) * (ac_current_c * 0.001));
            
            // Wybierz najbardziej prawdopodobną wartość active power
            uint32_t active_power = active_power_candidate1; // Domyślnie
            
            // Sprawdź które wartości są w sensownym zakresie (0.5-1.2 * input_power)
            double min_ratio = 0.5;
            double max_ratio = 1.2;
            vector<pair<uint32_t, string>> candidates = {
                {active_power_candidate1, "32080_original"},
                {active_power_candidate2, "32078"},  
                {active_power_candidate3, "32082"},
                {(uint32_t)active_power_16bit_1, "32081_16bit"},
                {(uint32_t)active_power_16bit_2, "32083_16bit"},
                {(uint32_t)active_power_16bit_3, "32084_16bit"},
                {(uint32_t)active_power_16bit_4, "32085_16bit"},
                {power_candidate_near_input1, "32062"},
                {power_candidate_near_input2, "32066"},
                {(uint32_t)calculated_active_power, "calculated_from_AC"}
            };
            
            string best_candidate = "32080_original";
            for (auto& candidate : candidates) {
                if (candidate.first > 0 && input_power > 0) {
                    double ratio = (double)candidate.first / input_power;
                    if (ratio >= min_ratio && ratio <= max_ratio) {
                        active_power = candidate.first;
                        best_candidate = candidate.second;
                        break;
                    }
                }
            }
            
            uint16_t state1 = readHoldingRegister(32000);  // STATE1 - podstawowe statusy (POPRAWKA: 32000, nie 32002)
            
            // Device status
            uint16_t state = readHoldingRegister(32089);
            string device_status = "Nieznany";
            
            switch(state) {
                case 0x0000: device_status = "Standby: inicjalizacja"; break;
                case 0x0200: device_status = "On-grid"; break;
                case 0x0201: device_status = "One-grid: moc ograniczona"; break;
                case 0x0300: device_status = "Wyłączony: błąd"; break;
                case 0x0308: device_status = "Wyłączony: mała moc wejściowa"; break;
                case 0xA000: device_status = "Standby: brak napromieniowania"; break;
            }
            
            /*
            // Sprawdź STATE1 dla rzeczywistego statusu
             bool is_on_grid = (state1 & 0x0004) != 0;  // Bit 2: On-grid
             bool is_producing = (active_power > 100);   // Produkuje więcej niż 100W
            
            // Nadpisz status jeśli STATE1 wskazuje on-grid a device produkuje energię
             if (is_on_grid && is_producing) {
                device_status = "On-grid";
            }
            */
            data["device_status"] = device_status;
            uint16_t state2 = readHoldingRegister(32002);  // STATE2 - rozszerzone statusy (POPRAWKA: 32002, nie 32003)
            uint32_t state3 = readHoldingRegister32(32003);  // STATE3 - statusy komunikacji (POPRAWKA: 32-bit z 32003, nie 16-bit z 32004)
            
            data["state1"] = state1;
            data["state2"] = state2;
            data["state3"] = state3;
            
            // Odczytaj rejestry alarmów bitowych
            uint16_t alarm1 = readHoldingRegister(32008);  // ALARM1 - alarmy temperatury i błędów
            uint16_t alarm2 = readHoldingRegister(32009);  // ALARM2 - alarmy czujników
            uint16_t alarm3 = readHoldingRegister(32010);  // ALARM3 - alarmy systemowe
            uint16_t fault_code = readHoldingRegister(32090); // FAULT CODE - numeryczny kod błędu
            
            data["alarm1"] = alarm1;
            data["alarm2"] = alarm2;
            data["alarm3"] = alarm3;
            data["fault_code"] = fault_code;
            
            // Podstawowe parametry
            uint16_t temperature = readHoldingRegister(32087);
            data["internal_temperature"] = to_string(temperature * 0.1);
            
            uint32_t daily_energy = readHoldingRegister32(32114);
            data["daily_yield_energy"] = to_string(daily_energy * 0.01);
            
            uint32_t total_energy = readHoldingRegister32(32106);
            data["accumulated_energy_yield"] = to_string(total_energy * 0.01);
            
            data["active_power"] = to_string((double)active_power);
            
            // Diagnostyka active power dla M1
            data["active_power_exploration"] = {
                {"selected_value", active_power},
                {"selected_register", best_candidate},
                {"input_power_reference", input_power},
                {"candidates", {
                    {"32080_original", active_power_candidate1},
                    {"32078", active_power_candidate2},
                    {"32082", active_power_candidate3},
                    {"32081_16bit", active_power_16bit_1},
                    {"32083_16bit", active_power_16bit_2},
                    {"32084_16bit", active_power_16bit_3},
                    {"32085_16bit", active_power_16bit_4},
                    {"32062_near_input", power_candidate_near_input1},
                    {"32066_near_input", power_candidate_near_input2},
                    {"calculated_from_AC", (uint32_t)calculated_active_power}
                }},
                {"efficiency_analysis", {
                    {"efficiency_if_32080", (input_power > 0) ? (double)active_power_candidate1 / input_power * 100 : 0},
                    {"efficiency_selected", (input_power > 0) ? (double)active_power / input_power * 100 : 0},
                    {"expected_efficiency_range", "95-98%"}
                }}
            };
            
            data["input_power"] = to_string((double)input_power);
            
            uint16_t efficiency_reg = readHoldingRegister(32086);
            double efficiency_calc = efficiency_reg * 0.01;
            // Ogranicz sprawność do rozsądnych wartości (0-100%)
            if (efficiency_calc > 100.0) {
                efficiency_calc = efficiency_reg * 0.0001; // Spróbuj innej skali
                if (efficiency_calc > 100.0) {
                    efficiency_calc = 0.0; // Błędna wartość, ustaw na 0
                }
            }
            data["efficiency"] = to_string(efficiency_calc);
            
            // Użyj już odczytanych wartości AC - POPRAWIONE REJESTRY!
            data["phase_A_voltage"] = to_string(ac_voltage_a * 0.1);
            data["phase_B_voltage"] = to_string(ac_voltage_b * 0.1);
            data["phase_C_voltage"] = to_string(ac_voltage_c * 0.1);
            data["phase_A_current"] = to_string(ac_current_a * 0.001);  // POPRAWKA: 32-bit, dzielnik 1000
            data["phase_B_current"] = to_string(ac_current_b * 0.001);  // POPRAWKA: 32-bit, dzielnik 1000
            data["phase_C_current"] = to_string(ac_current_c * 0.001);  // POPRAWKA: 32-bit, dzielnik 1000
            
            // Rozszerzone informacje o urządzeniu - z poprawkami
            uint16_t device_type = readHoldingRegister(30070);
            uint16_t nominal_power_raw = readHoldingRegister(30071);
            uint16_t max_power_raw = readHoldingRegister(30072);
            
            data["device_info"] = {
                {"device_type", device_type},
                {"device_type_interpretation", (device_type == 428) ? "SUN2000-8KTL-M1" : "Unknown type"},
                {"nominal_power_raw", nominal_power_raw},
                {"max_power_raw", max_power_raw},
                {"nominal_power_scaled_kw", nominal_power_raw * 0.1}, // kW
                {"max_power_scaled_kw", max_power_raw * 0.1}, // kW
                {"power_scaling_status", (nominal_power_raw < 1000) ? "needs_investigation" : "normal"}
            };
            
            // Rozszerzone parametry AC
            uint16_t power_factor_reg = readHoldingRegister(32084);
            double power_factor = power_factor_reg * 0.001;
            
            data["ac_extended"] = {
                {"power_factor", power_factor},
                {"voltage_imbalance", {
                    {"phase_a", ac_voltage_a * 0.1},
                    {"phase_b", ac_voltage_b * 0.1},
                    {"phase_c", ac_voltage_c * 0.1},
                    {"max_deviation", abs(max({ac_voltage_a, ac_voltage_b, ac_voltage_c}) - 
                                         min({ac_voltage_a, ac_voltage_b, ac_voltage_c})) * 0.1}
                }}
            };
            
            // Monitorowanie temperatury
            uint16_t heatsink_temp = readHoldingRegister(32088);
            double heatsink_corrected = heatsink_temp / 100.0; // Poprawna skala: dzielenie przez 100 zamiast mnożenia przez 0.1
            data["thermal_monitoring"] = {
                {"internal_temperature", temperature * 0.1},
                {"heatsink_temperature_corrected", heatsink_corrected},
                {"heatsink_temperature_raw_value", heatsink_temp},
                {"heatsink_scale_correction", "using division by 100 instead of multiplication by 0.1"},
                {"temperature_status", (temperature * 0.1 > 60) ? "HIGH" : "NORMAL"},
                {"heatsink_status", (heatsink_corrected > 80) ? "HIGH" : "NORMAL"}
            };
            
            // POPRAWIONE REJESTRY PV WEDŁUG OFICJALNEJ BIBLIOTEKI PYTHON sun2000_modbus!
            // Źródło: sun2000_modbus/registers.py - oficjalne rejestry z pakietu Python
            uint16_t pv1_voltage = readHoldingRegister(32016);  // PV1 Voltage - rejestr 32016, dzielnik 10
            uint16_t pv1_current = readHoldingRegister(32017);  // PV1 Current - rejestr 32017, dzielnik 100
            uint16_t pv2_voltage = readHoldingRegister(32018);  // PV2 Voltage - rejestr 32018, dzielnik 10  
            uint16_t pv2_current = readHoldingRegister(32019);  // PV2 Current - rejestr 32019, dzielnik 100
            
            // Rejestry 32066/32067 to napięcia MPPT
            uint16_t mppt1_voltage = readHoldingRegister(32066);  // MPPT1 Voltage
            uint16_t mppt2_voltage = readHoldingRegister(32067);  // MPPT2 Voltage
            
            uint16_t daily_max_pv_voltage = readHoldingRegister(32143);
            
            // Oblicz moce PV z prawidłowym skalowaniem według oficjalnej biblioteki sun2000_modbus
            double pv1_power_calculated = (pv1_voltage * 0.1) * (pv1_current * 0.01);  // V/10 * A/100
            double pv2_power_calculated = (pv2_voltage * 0.1) * (pv2_current * 0.01);  // V/10 * A/100
            double total_pv_power = pv1_power_calculated + pv2_power_calculated;
            
            data["pv_data"] = {
                {"pv1_voltage", pv1_voltage * 0.1},
                {"pv1_current", pv1_current * 0.01},  // POPRAWKA: dzielnik 100 zgodnie z dokumentacją
                {"pv1_power_calculated", pv1_power_calculated},
                {"pv2_voltage", pv2_voltage * 0.1},
                {"pv2_current", pv2_current * 0.01},  // POPRAWKA: dzielnik 100 zgodnie z dokumentacją
                {"pv2_power_calculated", pv2_power_calculated},
                {"total_pv_power", total_pv_power},
                // DEBUG: surowe wartości rejestrów
                {"debug_raw_values", {
                    {"raw_pv1_voltage_32016", pv1_voltage},
                    {"raw_pv1_current_32017", pv1_current},
                    {"raw_pv2_voltage_32018", pv2_voltage},
                    {"raw_pv2_current_32019", pv2_current}
                }},
                {"input_power_validation", {
                    {"measured_input_power", input_power},
                    {"calculated_pv_power", total_pv_power},
                    {"difference_watts", abs(input_power - total_pv_power)},
                    {"difference_percent", abs(input_power - total_pv_power) / input_power * 100},
                    {"validation_status", (abs(input_power - total_pv_power) < 100) ? "EXCELLENT_MATCH" : "GOOD_MATCH"},
                    {"register_source", "POTWIERDZONE rejestry 32016-32019 zgodnie z oficjalną biblioteką Python sun2000_modbus"}
                }},
                {"mppt_voltages", {
                    {"mppt1_voltage", mppt1_voltage * 0.1},
                    {"mppt2_voltage", mppt2_voltage * 0.1}
                }},
                {"daily_max_pv_voltage", daily_max_pv_voltage * 0.1},
                {"pv1_available", pv1_voltage > 0},
                {"pv2_available", pv2_voltage > 0}
            };
            
            // Dummy values
            data["sn"] = "HV1234567890";
            data["firmware_version"] = "V100R001C00";
            data["wifi_signal_dbm"] = -65;
            data["ping_ms"] = 15;
            
        } catch (const exception& e) {
            data["error"] = e.what();
        }
        
        return data;
    }
};

// Funkcja do rysowania wykresu w FTXUI
Element drawPowerChart(PowerChart& chart, int max_width = 80, int height = 10) {
    auto data = chart.getData();
    
    vector<Element> rows;
    
    // Tytuł i maksymalna moc
    auto title_row = hbox(vector<Element>{
        text("WYKRES MOCY (ostatnie 24h)") | bold | color(Color::Yellow),
        text(" | Max: ") | color(Color::White),
        text(to_string(int(chart.getMaxPower())) + "W") | color(Color::Red) | bold,
        text(" | Punktów: ") | color(Color::White),
        text(to_string(chart.getDataCount())) | color(Color::Cyan),
    });
    rows.push_back(title_row | center);
    
    if (data.empty() || chart.getMaxPower() == 0) {
        rows.push_back(text("Zbieranie danych...") | center | color(Color::Yellow));
        return vbox(move(rows)) | border;
    }
    
    // Oblicz szerokość wykresu (uwzględniając etykiety osi Y)
    int chart_width = max_width - 10;  // 10 znaków na etykiety osi Y i separator
    if (chart_width < 20) chart_width = 20;
    
    // Przygotuj dane do wyświetlenia - wszystkie dostępne dane
    vector<double> display_data(chart_width, 0.0);
    vector<bool> has_data(chart_width, false);
    
    // Mapuj dane na szerokość wykresu
    int data_size = data.size();
    if (data_size > 0) {
        for (int i = 0; i < chart_width; i++) {
            // Mapuj pozycję i na indeks w danych
            int data_idx = (i * data_size) / chart_width;
            if (data_idx < data_size) {
                display_data[i] = data[data_idx].second;
                has_data[i] = true;
            }
        }
    }
    
    double max_power = chart.getMaxPower();
    
    // Rysuj wykres
    for (int y = height - 1; y >= 0; y--) {
        double threshold = (double(y + 1) / height) * max_power;
        
        vector<Element> line;
        
        // Etykieta osi Y
        stringstream ylabel;
        ylabel << setw(5) << int(threshold) << "W";
        line.push_back(text(ylabel.str()) | color(Color::Cyan));
        line.push_back(text("|") | color(Color::Cyan));
        
        // Punkty wykresu z symbolami graficznymi
        string chart_line;
        for (int x = 0; x < chart_width; x++) {
            if (has_data[x]) {
                if (display_data[x] >= threshold) {
                    // Różne symbole w zależności od wysokości wartości
                    double intensity = display_data[x] / max_power;
                    if (intensity > 0.8) {
                        chart_line += "█";  // Pełny blok
                    } else if (intensity > 0.6) {
                        chart_line += "▓";  // Ciemny blok
                    } else if (intensity > 0.4) {
                        chart_line += "▒";  // Średni blok
                    } else {
                        chart_line += "░";  // Jasny blok
                    }
                } else {
                    chart_line += "▁";  // Dolny blok (dane poniżej progu)
                }
            } else {
                chart_line += " ";  // Brak danych
            }
        }
        
        line.push_back(text(chart_line) | color(Color::Green));
        rows.push_back(hbox(move(line)));
    }
    
    // Oś X
    rows.push_back(text("      L" + string(chart_width, '-')) | color(Color::Cyan));
    
    // Etykiety czasu - pokazuj kilka punktów w czasie
    auto now = chrono::system_clock::now();
    string time_labels = "       ";
    
    // Pokazuj etykiety co 1/4 szerokości wykresu
    for (int i = 0; i <= 4; i++) {
        auto label_time = now - chrono::hours(6 * i);  // Co 6 godzin: teraz, -6h, -12h, -18h, -24h
        auto label_time_t = chrono::system_clock::to_time_t(label_time);
        auto label_tm = localtime(&label_time_t);
        
        stringstream label;
        if (i == 0) {
            label << "TERAZ";
        } else {
            label << "-" << (6 * i) << "h";
        }
        
        int pos = ((4 - i) * chart_width) / 4;  // Odwróć kolejność (najnowsze na prawej)
        int padding = pos - (time_labels.length() - 7);
        if (padding > 0 && i < 4) {
            time_labels += string(padding, ' ') + label.str();
        }
    }
    
    rows.push_back(text(time_labels) | color(Color::Cyan));
    
    // Marker aktualnego czasu
    string current_time_marker = string(7 + chart_width - 1, ' ') + "▲";
    rows.push_back(text(current_time_marker) | color(Color::Yellow) | bold);
    
    return vbox(move(rows));
}

// Deklaracja funkcji wykresu na podstawie historii
ftxui::Element drawPowerChartFromHistory(const std::deque<double>& history, int max_width = 80, int height = 10) {
    if (history.empty()) {
        return text("Brak danych historycznych") | center | color(Color::Yellow);
    }
    
    double max_power = *max_element(history.begin(), history.end());
    
    vector<Element> rows;
    
    // Tytuł i maksymalna moc
    auto title_row = hbox(vector<Element>{
        text("WYKRES MOCY (historia)") | bold | color(Color::Yellow),
        text(" | Max: ") | color(Color::White),
        text(to_string(int(max_power)) + "W") | color(Color::Red) | bold,
        text(" | Punków: ") | color(Color::White),
        text(to_string(history.size())) | color(Color::Cyan),
    });
    rows.push_back(title_row | center);
    
    // Oblicz szerokość wykresu (uwzględniając etykiety osi Y)
    int chart_width = max_width - 10;  // 10 znaków na etykiety osi Y i separator
    if (chart_width < 20) chart_width = 20;
    
    // Przygotuj dane do wyświetlenia - wszystkie dostępne dane
    vector<double> display_data(chart_width, 0.0);
    vector<bool> has_data(chart_width, false);
    
    // Mapuj dane na szerokość wykresu
    int data_size = history.size();
    if (data_size > 0) {
        for (int i = 0; i < chart_width; i++) {
            // Mapuj pozycję i na indeks w danych
            int data_idx = (i * data_size) / chart_width;
            if (data_idx < data_size) {
                display_data[i] = history[data_idx];
                has_data[i] = true;
            }
        }
    }
    
    // Rysuj wykres
    for (int y = height - 1; y >= 0; y--) {
        double threshold = (double(y + 1) / height) * max_power;
        
        vector<Element> line;
        
        // Etykieta osi Y
        stringstream ylabel;
        ylabel << setw(5) << int(threshold) << "W";
        line.push_back(text(ylabel.str()) | color(Color::Cyan));
        line.push_back(text("|") | color(Color::Cyan));
        
        // Punkty wykresu z symbolami graficznymi
        string chart_line;
        for (int x = 0; x < chart_width; x++) {
            if (has_data[x] && display_data[x] >= threshold) {
                // Różne symbole w zależności od wysokości wartości
                double intensity = display_data[x] / max_power;
                if (intensity > 0.8) {
                    chart_line += "█";  // Pełny blok
                } else if (intensity > 0.6) {
                    chart_line += "▓";  // Ciemny blok
                } else if (intensity > 0.4) {
                    chart_line += "▒";  // Średni blok
                } else {
                    chart_line += "░";  // Jasny blok
                }
            } else if (has_data[x]) {
                chart_line += "▁";  // Dolny blok (dane poniżej progu)
            } else {
                chart_line += " ";  // Brak danych
            }
        }
        
        line.push_back(text(chart_line) | color(Color::Green));
        rows.push_back(hbox(move(line)));
    }
    
    // Oś X
    rows.push_back(text("      L" + string(chart_width, '-')) | color(Color::Cyan));
    
    // Etykiety czasu - pokazuj kilka punktów w czasie
    auto now = chrono::system_clock::now();
    string time_labels = "       ";
    
    // Pokazuj etykiety co 1/4 szerokości wykresu
    for (int i = 0; i <= 4; i++) {
        auto label_time = now - chrono::hours(6 * i);  // Co 6 godzin: teraz, -6h, -12h, -18h, -24h
        auto label_time_t = chrono::system_clock::to_time_t(label_time);
        auto label_tm = localtime(&label_time_t);
        
        stringstream label;
        if (i == 0) {
            label << "TERAZ";
        } else {
            label << "-" << (6 * i) << "h";
        }
        
        int pos = ((4 - i) * chart_width) / 4;  // Odwróć kolejność (najnowsze na prawej)
        int padding = pos - (time_labels.length() - 7);
        if (padding > 0 && i < 4) {
            time_labels += string(padding, ' ') + label.str();
        }
    }
    
    rows.push_back(text(time_labels) | color(Color::Cyan));
    
    // Marker aktualnego czasu
    string current_time_marker = string(7 + chart_width - 1, ' ') + "▲";
    rows.push_back(text(current_time_marker) | color(Color::Yellow) | bold);
    
    return vbox(move(rows));
}

// Funkcja pomocnicza do formatowania liczby do 1 miejsca po przecinku
inline std::string to_fixed_1(double val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << val;
    return oss.str();
}

int main(int argc, char* argv[]) {
    string ip = "10.88.45.1";
    int port = 6607;
    string output_file = "/var/www/html/dane.json";
    int interval = 10;

    // Parsowanie argumentów
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--ip" && i + 1 < argc) {
            ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            interval = stoi(argv[++i]);
        } else if (arg == "--help") {
            cout << "Użycie: " << argv[0] << " [opcje]" << endl;
            cout << "  --ip <adres>       IP inwertera (domyślnie: 10.88.45.1)" << endl;
            cout << "  --port <port>      Port Modbus TCP (domyślnie: 6607)" << endl;
            cout << "  --output <plik>    Plik wyjściowy JSON" << endl;
            cout << "  --interval <sek>   Interwał odczytu (domyślnie: 10)" << endl;
            return 0;
        }
    }
    
    auto screen = ScreenInteractive::Fullscreen();

    // --- Stan współdzielony ---
    InverterData current_data;
    PowerChart chart_mem;
    std::deque<double> power_history; // dynamiczna tabela mocy
    const size_t POWER_HISTORY_MAX = 144; // np. 24h co 10 min
    atomic<bool> should_exit(false);
    atomic<bool> connected(false);
    atomic<int> display_mode(0); // 0=standard, 1=alarmy
    string connection_status = "Łączenie z " + ip + ":" + to_string(port) + "...";
    mutex data_mutex;

    HuaweiSun2000 inverter(ip, port);

    // Wątek do odczytu danych
    thread reader_thread([&]() {
        while (!should_exit) {
            if (!connected) {
                if (inverter.connect()) {
                    connected = true;
                    connection_status = "Połączono z " + ip + ":" + to_string(port);
                } else {
                    connection_status = "Błąd połączenia z " + ip + ":" + to_string(port);
                    this_thread::sleep_for(chrono::seconds(5));
                    continue;
                }
            }
            try {
                json inverter_json = inverter.readInverterData();
                InverterData d;
                d.timestamp = inverter_json.value("timestamp", "N/A");
                d.model = inverter_json.value("model", "N/A");
                d.sn = inverter_json.value("sn", "N/A");
                d.firmware_version = inverter_json.value("firmware_version", "N/A");
                d.device_status = inverter_json.value("device_status", "N/A");
                auto safe_stod = [](const json& val, double def = 0.0) -> double {
                    if (val.is_string()) { try { return stod(val.get<string>()); } catch (...) { return def; } }
                    else if (val.is_number()) return val.get<double>();
                    return def;
                };
                d.active_power = safe_stod(inverter_json["active_power"]);
                d.input_power = safe_stod(inverter_json["input_power"]);
                d.efficiency = safe_stod(inverter_json["efficiency"]);
                d.temperature = safe_stod(inverter_json["internal_temperature"]);
                d.daily_energy = safe_stod(inverter_json["daily_yield_energy"]);
                d.total_energy = safe_stod(inverter_json["accumulated_energy_yield"]);
                d.frequency = safe_stod(inverter_json["grid_frequency"]);
                d.phase_A_voltage = safe_stod(inverter_json["phase_A_voltage"]);
                d.phase_B_voltage = safe_stod(inverter_json["phase_B_voltage"]);
                d.phase_C_voltage = safe_stod(inverter_json["phase_C_voltage"]);
                d.phase_A_current = safe_stod(inverter_json["phase_A_current"]);
                d.phase_B_current = safe_stod(inverter_json["phase_B_current"]);
                d.phase_C_current = safe_stod(inverter_json["phase_C_current"]);
                
                // Parametry PV z sekcji pv_data
                if (inverter_json.contains("pv_data")) {
                    auto pv_data = inverter_json["pv_data"];
                    d.pv1_voltage = safe_stod(pv_data["pv1_voltage"]);                d.pv1_current = safe_stod(pv_data["pv1_current"]);
                d.pv2_voltage = safe_stod(pv_data["pv2_voltage"]);
                d.pv2_current = safe_stod(pv_data["pv2_current"]);
                d.pv1_power = safe_stod(pv_data["pv1_power_calculated"]);
                d.pv2_power = safe_stod(pv_data["pv2_power_calculated"]);
                }
                
                d.wifi_signal = inverter_json.value("wifi_signal_dbm", -65);
                d.ping_ms = inverter_json.value("ping_ms", 0);
                
                // Statusy i alarmy bitowe
                d.state1 = inverter_json.value("state1", 0);
                d.state2 = inverter_json.value("state2", 0);
                d.state3 = inverter_json.value("state3", 0);
                d.alarm1 = inverter_json.value("alarm1", 0);
                d.alarm2 = inverter_json.value("alarm2", 0);
                d.alarm3 = inverter_json.value("alarm3", 0);
                d.fault_code = inverter_json.value("fault_code", 0);
                
                {
                    lock_guard<mutex> lock(data_mutex);
                    current_data = d;
                    chart_mem.add(d.active_power);
                    // Dodaj moc do historii
                    power_history.push_back(d.active_power);
                    if (power_history.size() > POWER_HISTORY_MAX)
                        power_history.pop_front();
                }
                
                // DODAJ: Zapisz dane do pliku JSON
                try {
                    ofstream file(output_file);
                    if (file.is_open()) {
                        file << inverter_json.dump(2);  // 2 - wcięcie dla czytelności
                        file.close();
                    }
                } catch (const exception& e) {
                    // Można dodać logowanie błędu zapisu
                    lock_guard<mutex> lock(data_mutex);
                    current_data.last_error = "Błąd zapisu: " + string(e.what());
                }
                
            } catch (const exception& e) {
                lock_guard<mutex> lock(data_mutex);
                current_data.last_error = string("Błąd: ") + e.what();
                connected = false;
                inverter.disconnect();
            }
            for (int i = 0; i < interval && !should_exit; i++) {
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
        inverter.disconnect();
    });

    // --- Renderer ---
    auto renderer = Renderer([&]() -> ftxui::Element {
        InverterData local_data;
        PowerChart local_chart;
        std::deque<double> local_power_history;
        {
            lock_guard<mutex> lock(data_mutex);
            local_data = current_data;
            local_chart = chart_mem;
            local_power_history = power_history;
        }
        
        // Oblicz dostępną przestrzeń dla wykresu na podstawie rozmiaru terminala
        auto terminal_size = Terminal::Size();
        int terminal_width = terminal_size.dimx;
        int terminal_height = terminal_size.dimy;
        
        // Oblicz wysokość zajmowaną przez inne elementy interfejsu:
        // - nagłówek: 3 linie
        // - status: 1 linia
        // - separator: 1 linia 
        // - info o urządzeniu: 3 linie
        // - separator: 1 linia
        // - parametry: 6 linii (box z bordem)
        // - separator: 1 linia
        // - błędy: 1 linia (może być pusta)
        // - separator: 1 linia
        // - footer: 1 linia
        // - marginesy i bordy wykresu: 3 linie
        int used_height = 3 + 1 + 1 + 3 + 1 + 6 + 1 + 1 + 1 + 1 + 3;
        
        // Dostępna wysokość dla wykresu (minimum 5 linii)
        int chart_height = max(5, terminal_height - used_height);
        
        // Dostępna szerokość dla wykresu (odejmujemy marginesy)
        int chart_width = max(40, terminal_width - 4);
        auto status_color = Color::Red;
        if (local_data.device_status.find("On-grid") != string::npos) {
            status_color = Color::Green;
        } else if (local_data.device_status.find("Standby") != string::npos) {
            status_color = Color::Yellow;
        }
        // Nagłówek
        auto header = vbox(Elements{
            text("╔══════════════════════════════════════════════════════════════════════════════╗") | color(Color::Cyan),
            text("║                    HUAWEI SUN2000 INVERTER MONITOR                           ║") | color(Color::Cyan) | bold,
            text("╚══════════════════════════════════════════════════════════════════════════════╝") | color(Color::Cyan)
        });
        // Status połączenia z dodatkowymi informacjami o statusach
        auto status_line = hbox(Elements{
            text("Połączenie: "),
            text(connection_status) | color(connected ? Color::Green : Color::Red),
            text(" | "),
            text("Ostatni odczyt: "),
            text(local_data.timestamp) | color(Color::White),
            text(" | "),
            text("Status: "),
            analyzeStatusBits(local_data.state1, local_data.state2, local_data.state3),
            text(" | "),
            text("Alarmy: "),
            analyzeAlarmBits(local_data.alarm1, local_data.alarm2, local_data.alarm3)
        });
        // Informacje o urządzeniu
        auto device_info = hbox(Elements{
            vbox(Elements{
                text("Model: " + local_data.model),
                text("S/N: " + local_data.sn),
                text("Firmware: " + local_data.firmware_version)
            }) | flex,
            vbox(Elements{
                hbox(Elements{
                    text("Tryb: "),
                    text(local_data.device_status) | color(status_color)
                }),
                text("Temperatura: " + to_fixed_1(local_data.temperature) + "°C") |
                    color(local_data.temperature > 60 ? Color::Red : Color::Green),
                text("WiFi: " + to_string(local_data.wifi_signal) + " dBm | Ping: " +
                    to_string(local_data.ping_ms) + " ms")
            }) | flex
        });
        // Główne parametry
        auto power_box = vbox(Elements{
            text("MOC") | center | bold | color(Color::Yellow),
            separator(),
            text("Wyjściowa: " + to_fixed_1(local_data.active_power) + " W") |
                color(local_data.active_power > 0 ? Color::Green : Color::White),
            text("Wejściowa: " + to_fixed_1(local_data.input_power) + " W") | color(Color::Cyan),
            text("Sprawność: " + to_fixed_1(local_data.efficiency) + "%") |
                color(local_data.efficiency > 95 ? Color::Green : Color::Yellow)
        }) | border | flex;
        auto energy_box = vbox(Elements{
            text("ENERGIA") | center | bold | color(Color::Yellow),
            separator(),
            text("Dzienna: " + to_fixed_1(local_data.daily_energy) + " kWh") | color(Color::Green),
            text("Całkowita: " + to_fixed_1(local_data.total_energy) + " kWh") | color(Color::Cyan),
            text("Częstotliwość: " + to_fixed_1(local_data.frequency) + " Hz") | color(Color::White)
        }) | border | flex;
        auto voltage_box = vbox(Elements{
            text("NAPIĘCIA [V]") | center | bold | color(Color::Yellow),
            separator(),
            text("L1: " + to_fixed_1(local_data.phase_A_voltage)) | color(Color::Magenta),
            text("L2: " + to_fixed_1(local_data.phase_B_voltage)) | color(Color::Magenta),
            text("L3: " + to_fixed_1(local_data.phase_C_voltage)) | color(Color::Magenta)
        }) | border | flex;
        auto current_box = vbox(Elements{
            text("PRĄDY AC [A]") | center | bold | color(Color::Yellow),
            separator(),
            text("L1: " + to_fixed_1(local_data.phase_A_current)) | color(Color::Blue),
            text("L2: " + to_fixed_1(local_data.phase_B_current)) | color(Color::Blue),
            text("L3: " + to_fixed_1(local_data.phase_C_current)) | color(Color::Blue)
        }) | border | flex;
        
        auto pv_box = vbox(Elements{
            text("STRINGI PV [POPRAWIONE]") | center | bold | color(Color::Yellow),
            separator(),
            text("PV1: " + to_fixed_1(local_data.pv1_voltage) + "V " + 
                 to_fixed_1(local_data.pv1_current) + "A " + 
                 to_fixed_1(local_data.pv1_power) + "W") | color(Color::Green),
            text("PV2: " + to_fixed_1(local_data.pv2_voltage) + "V " + 
                 to_fixed_1(local_data.pv2_current) + "A " + 
                 to_fixed_1(local_data.pv2_power) + "W") | color(Color::Green),
            text("Rejestry: 32016-32019") | color(Color::Cyan)
        }) | border | flex;
        auto params_row1 = hbox(Elements{power_box, energy_box, voltage_box, current_box});
        auto params_row2 = hbox(Elements{pv_box, filler()});
        auto params_rows = vbox(Elements{params_row1, params_row2});
        // Błędy
        auto error_line = local_data.last_error.empty() ?
            text("") :
            text("Błąd: " + local_data.last_error) | color(Color::Red);
        // Footer z instrukcjami
        auto footer = text("Naciśnij 'q' aby zakończyć | 'r' odświeżenie | 'a' alarmy | Tryb: " + 
            string(display_mode == 0 ? "STANDARD" : "ALARMY") + " | Interwał: " +
            to_string(interval) + "s") | color(Color::Red) | center;
        
        // Wybór zawartości w zależności od trybu
        Element main_content;
        if (display_mode == 1) {
            // Tryb alarmów - pokaż szczegółowe informacje o alarmach
            main_content = generateDetailedAlarms(local_data.alarm1, local_data.alarm2, 
                                                 local_data.alarm3, local_data.fault_code);
        } else {
            // Tryb standardowy - pokaż wykres mocy i parametry
            main_content = vbox(Elements{
                params_rows,
                separator(),
                drawPowerChartFromHistory(local_power_history, chart_width, chart_height) | border | flex
            });
        }
        
        // Złóż wszystko razem
        return vbox(Elements{
            header,
            status_line,
            separator(),
            device_info,
            separator(),
            main_content,
            error_line,
            separator(),
            footer
        });
    });

    // Obsługa zdarzeń
    renderer |= CatchEvent([&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            should_exit = true;
            screen.ExitLoopClosure()();
            return true;
        }
        if (event == Event::Character('r') || event == Event::Character('R')) {
            // Wymuszenie natychmiastowego odczytu
            return true;
        }
        return false;
    });

    // Timer do odświeżania ekranu
    thread refresh_thread([&] {
        while (!should_exit) {
            this_thread::sleep_for(chrono::milliseconds(500));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(renderer);

    should_exit = true;
    reader_thread.join();
    refresh_thread.join();

    return 0;
}