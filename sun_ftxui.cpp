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

// FTXUI includes
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"



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
    int wifi_signal = -65;
    int ping_ms = 0;
    string last_error = "";
};

// Funkcja pomocnicza do formatowania liczby do 1 miejsca po przecinku
inline std::string to_fixed_1(double val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << val;
    return oss.str();
}

// Funkcja pomocnicza do formatowania liczby do 2 miejsc po przecinku
inline std::string to_fixed_2(double val) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << val;
    return oss.str();
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
            // Timestamp
            auto now = chrono::system_clock::now();
            auto time_t = chrono::system_clock::to_time_t(now);
            stringstream ss;
            ss << put_time(localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            data["timestamp"] = ss.str();
            
            // Model
            data["model"] = "SUN2000-8KTL-M1";
            
            // Device status
            uint16_t state = readHoldingRegister(32089);
            string device_status = "Nieznany (" + to_string(state) + ")";
            
            switch(state) {
                case 0x0000: device_status = "Standby: inicjalizacja"; break;
                case 0x0200: device_status = "On-grid"; break;
                case 0x0201: device_status = "On-grid: moc ograniczona"; break;
                case 0x0300: device_status = "Wyłączony: błąd"; break;
                case 0x0308: device_status = "Wyłączony: mała moc wejściowa"; break;
                case 0xA000: device_status = "Standby: brak napromieniowania"; break;
            }
            data["device_status"] = device_status;
            
            // Podstawowe parametry
            uint16_t temperature = readHoldingRegister(32087);
            data["internal_temperature"] = to_fixed_1(temperature * 0.1);
            
            uint32_t daily_energy = readHoldingRegister32(32114);
            data["daily_yield_energy"] = to_fixed_2(daily_energy * 0.01);
            
            uint32_t total_energy = readHoldingRegister32(32106);
            data["accumulated_energy_yield"] = to_fixed_2(total_energy * 0.01);
            
            uint32_t active_power = readHoldingRegister32(32080);
            data["active_power"] = to_fixed_1((double)active_power);
            
            uint32_t input_power = readHoldingRegister32(32064);
            data["input_power"] = to_fixed_1((double)input_power);
            
            uint16_t efficiency_reg = readHoldingRegister(32086);
            data["efficiency"] = to_fixed_2(efficiency_reg * 0.01);
            
            // Częstotliwość sieci
            uint16_t grid_frequency = readHoldingRegister(32085);
            if (grid_frequency == 0) {
                grid_frequency = readHoldingRegister(37118);
            }
            
            double frequency_hz = grid_frequency / 100.0;
            if (frequency_hz < 45.0 || frequency_hz > 65.0) {
                data["grid_frequency"] = "0.00";
            } else {
                data["grid_frequency"] = to_fixed_2(frequency_hz);
            }
            
            // Napięcia fazowe
            uint16_t ac_voltage_a = readHoldingRegister(32069);
            data["phase_A_voltage"] = to_fixed_1(ac_voltage_a * 0.1);
            
            uint16_t ac_voltage_b = readHoldingRegister(32070);
            data["phase_B_voltage"] = to_fixed_1(ac_voltage_b * 0.1);
            
            uint16_t ac_voltage_c = readHoldingRegister(32071);
            data["phase_C_voltage"] = to_fixed_1(ac_voltage_c * 0.1);
            
            // Prądy fazowe
            int32_t ac_current_a = (int32_t)readHoldingRegister32(32072);
            data["phase_A_current"] = to_fixed_2(ac_current_a / 1000.0);
            
            int32_t ac_current_b = (int32_t)readHoldingRegister32(32074);
            data["phase_B_current"] = to_fixed_2(ac_current_b / 1000.0);
            
            int32_t ac_current_c = (int32_t)readHoldingRegister32(32076);
            data["phase_C_current"] = to_fixed_2(ac_current_c / 1000.0);
            
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

// Funkcja do zapisu danych JSON do pliku
void saveJsonToFile(const json& data, const string& filename) {
    try {
        ofstream file(filename);
        if (file.is_open()) {
            file << data.dump(2); // 2 spacje wcięcia dla czytelności
            file.close();
        }
    } catch (const exception& e) {
        // W przypadku błędu, kontynuuj działanie
        cerr << "Błąd zapisu JSON: " << e.what() << endl;
    }
}

// Funkcja wykresu dostosowująca się do rozmiaru okna z blokami Unicode
ftxui::Element drawPowerChartFromHistory(const std::deque<double>& history) {
    if (history.empty()) {
        return text("Brak danych historycznych") | center | color(Color::Yellow);
    }
    
    // Pobierz rozmiar terminala
    auto screen_size = Terminal::Size();
    int terminal_width = screen_size.dimx;
    int terminal_height = screen_size.dimy;
    
    // Oblicz rozmiar wykresu (pozostaw miejsce na UI)
    int chart_width = max(20, terminal_width - 15);  // 15 znaków na etykiety Y i marginesy
    int chart_height = max(8, terminal_height / 3);   // 1/3 wysokości terminala
    
    double max_power = *max_element(history.begin(), history.end());
    if (max_power <= 0) max_power = 1.0; // Unikaj dzielenia przez 0
    
    vector<Element> rows;
    
    // Tytuł
    auto title_row = hbox(vector<Element>{
        text("WYKRES MOCY") | bold | color(Color::Yellow),
        text(" | Max: ") | color(Color::White),
        text(to_fixed_1(max_power) + "W") | color(Color::Red) | bold,
        text(" | Punktów: ") | color(Color::White),
        text(to_string(history.size())) | color(Color::Cyan),
    });
    rows.push_back(title_row | center);
    
    // Przygotuj dane do wyświetlenia
    vector<double> display_data(chart_width, 0.0);
    vector<bool> has_data(chart_width, false);
    
    // Mapuj dane na szerokość wykresu
    int data_size = history.size();
    if (data_size > 0) {
        for (int i = 0; i < chart_width; i++) {
            int data_idx = (i * data_size) / chart_width;
            if (data_idx < data_size) {
                display_data[i] = history[data_idx];
                has_data[i] = true;
            }
        }
    }
    
    // Rysuj wykres używając bloków Unicode
    for (int y = chart_height - 1; y >= 0; y--) {
        double threshold = (double(y + 1) / chart_height) * max_power;
        
        vector<Element> line;
        
        // Etykieta osi Y
        stringstream ylabel;
        ylabel << setw(6) << fixed << setprecision(0) << threshold << "W";
        line.push_back(text(ylabel.str()) | color(Color::Cyan));
        line.push_back(text("│") | color(Color::Cyan));
        
        // Punkty wykresu z blokami Unicode
        string chart_line;
        for (int x = 0; x < chart_width; x++) {
            if (has_data[x]) {
                double value = display_data[x];
                double ratio = value / max_power;
                double y_pos = (double(y) / chart_height);
                
                if (ratio >= y_pos + (1.0 / chart_height)) {
                    // Pełny blok
                    chart_line += "█";
                } else if (ratio >= y_pos + (0.75 / chart_height)) {
                    // 3/4 bloku
                    chart_line += "▊";
                } else if (ratio >= y_pos + (0.5 / chart_height)) {
                    // Połowa bloku
                    chart_line += "▌";
                } else if (ratio >= y_pos + (0.25 / chart_height)) {
                    // 1/4 bloku
                    chart_line += "▎";
                } else if (ratio > y_pos) {
                    // Cienka linia
                    chart_line += "▏";
                } else {
                    chart_line += " ";
                }
            } else {
                chart_line += " ";
            }
        }
        
        auto chart_color = Color::Green;
        if (max_power > 5000) chart_color = Color::Red;      // Wysokie obciążenie
        else if (max_power > 3000) chart_color = Color::Yellow; // Średnie obciążenie
        
        line.push_back(text(chart_line) | color(chart_color));
        rows.push_back(hbox(move(line)));
    }
    
    // Oś X
    string bottom_line = "      └" + string(chart_width, '-') + "┘";
    rows.push_back(text(bottom_line) | color(Color::Cyan));
    
    // Etykiety czasu
    auto now = chrono::system_clock::now();
    vector<Element> time_labels;
    time_labels.push_back(text("        ") | color(Color::Cyan)); // Offset dla etykiet Y
    
    for (int i = 0; i <= 4; i++) {
        auto label_time = now - chrono::hours(6 * (4-i)); // Odwróć kolejność
        auto label_time_t = chrono::system_clock::to_time_t(label_time);
        auto label_tm = localtime(&label_time_t);
        
        stringstream label;
        if (i == 4) {
            label << "TERAZ";
        } else {
            label << "-" << (6 * (4-i)) << "h";
        }
        
        int pos = (i * chart_width) / 4;
        if (i > 0) {
            int spacing = (chart_width / 4) - 4; // -4 dla długości etykiety
            time_labels.push_back(text(string(max(1, spacing), ' ')) | color(Color::Cyan));
        }
        time_labels.push_back(text(label.str()) | color(Color::Cyan));
    }
    
    rows.push_back(hbox(move(time_labels)));
    
    return vbox(move(rows));
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
    std::deque<double> power_history; // dynamiczna tabela mocy
    const size_t POWER_HISTORY_MAX = 144; // np. 24h co 10 min
    atomic<bool> should_exit(false);
    atomic<bool> connected(false);
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
                
                // Zapisz dane do pliku JSON
                saveJsonToFile(inverter_json, output_file);
                
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
                d.wifi_signal = inverter_json.value("wifi_signal_dbm", -65);
                d.ping_ms = inverter_json.value("ping_ms", 0);
                {
                    lock_guard<mutex> lock(data_mutex);
                    current_data = d;
                    // Dodaj moc do historii
                    power_history.push_back(d.active_power);
                    if (power_history.size() > POWER_HISTORY_MAX)
                        power_history.pop_front();
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
        std::deque<double> local_power_history;
        {
            lock_guard<mutex> lock(data_mutex);
            local_data = current_data;
            local_power_history = power_history;
        }
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
        // Status połączenia
        auto status_line = hbox(Elements{
            text("Połączenie: "),
            text(connection_status) | color(connected ? Color::Green : Color::Red),
            text(" | "),
            text("Ostatni odczyt: "),
            text(local_data.timestamp) | color(Color::White)
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
                    text("Status: "),
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
            text("PRĄDY [A]") | center | bold | color(Color::Yellow),
            separator(),
            text("L1: " + to_fixed_1(local_data.phase_A_current)) | color(Color::Blue),
            text("L2: " + to_fixed_1(local_data.phase_B_current)) | color(Color::Blue),
            text("L3: " + to_fixed_1(local_data.phase_C_current)) | color(Color::Blue)
        }) | border | flex;
        auto params_row = hbox(Elements{power_box, energy_box, voltage_box, current_box});
        // Błędy
        auto error_line = local_data.last_error.empty() ?
            text("") :
            text("Błąd: " + local_data.last_error) | color(Color::Red);
        // Footer
        auto footer = text("Naciśnij 'q' aby zakończyć | 'r' aby wymusić odświeżenie | Interwał: " +
            to_string(interval) + "s") | color(Color::Red) | center;
        // Złóż wszystko razem
        return vbox(Elements{
            header,
            status_line,
            separator(),
            device_info,
            separator(),
            params_row,
            separator(),
            drawPowerChartFromHistory(local_power_history) | border | flex,
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