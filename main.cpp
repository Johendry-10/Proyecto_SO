#include "simulador.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <atomic>

// Definición de variables globales declaradas en simulador.h
std::queue<PCB> ready_queue;
std::vector<PCB> process_list;
int clock_ticks = 0;
// EL MUTEX, Se inicializa al arrancar el programa.
pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;
int next_pid = 1;
std::atomic<bool> is_running(true); 
std::string current_algo = "RR"; 
int current_quantum = 2;         
int speed_ms = 1000;             
bool is_cpu_running = false;     
std::vector<GanttEvent> gantt_log;

void gantt_record(int pid, const std::string& name, int tick) {
    if (!gantt_log.empty() && gantt_log.back().pid == pid && gantt_log.back().end == tick) {
        gantt_log.back().end = tick + 1;
    } else {
        gantt_log.push_back({pid, name, tick, tick + 1});
    }
}

// Función auxiliar para verificar si un string es un número
bool is_number(const std::string& s) {
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

// Función auxiliar para convertir el estado a String
std::string state_to_string(ProcessState state) {
    switch(state) {
        case NUEVO: return "NEW";
        case LISTO: return "READY";
        case EJECUCION: return "RUNNING";
        case PAUSADO_BLOQUEADO: return "BLOCKED";
        case FINALIZADO: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

// Hilo 1: Interfaz de Consola (CLI) 
void* cli_thread_func(void* arg) {
    std::cout << "[CLI] Hilo de consola iniciado. Esperando comandos...\n";
    std::string input_line;

    while (is_running) {
        std::cout << "> ";
        if (!std::getline(std::cin, input_line)) break;
        if (input_line.empty()) continue;

        std::istringstream iss(input_line);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        std::string command = tokens[0];

        if (command == "exit") {
            is_running = false;
            break;
        }
        else if (command == "create") {
            if (tokens.size() < 3 || tokens.size() > 4) {
                std::cout << "[ERROR DE SINTAXIS]: Parámetros insuficientes o en exceso.\n"
                          << "Uso correcto: create <nombre> <burst_time> [prioridad]\n"; 
                continue;
            }

            std::string name = tokens[1];
            std::string burst_str = tokens[2];
            
            if (!is_number(burst_str)) {
                std::cout << "[ERROR DE TIPO]: El burst_time debe ser un número entero positivo.\n";
                continue;
            }
            int burst = std::stoi(burst_str);
            if (burst <= 0) {
                 std::cout << "[ERROR DE RANGO]: El burst_time debe ser mayor a 0.\n";
                 continue;
            }

            int priority = 3;
            if (tokens.size() == 4) {
                std::string prio_str = tokens[3];
                if (!is_number(prio_str)) {
                    std::cout << "[ERROR DE TIPO]: La prioridad debe ser un número entero.\n";
                    continue;
                }
                priority = std::stoi(prio_str);
                if (priority < 1 || priority > 5) {
                    std::cout << "[ERROR DE RANGO]: La prioridad debe ser un valor entre 1 y 5.\n";
                    continue;
                }
            }

            // --- SECCIÓN CRÍTICA ---
            pthread_mutex_lock(&system_mutex);//hace lock para que se puede escribir o leer
            
            PCB new_process;
            new_process.pid = next_pid++;
            new_process.name = name;     
            new_process.state = NUEVO;  
            new_process.priority = priority;
            new_process.arrival_time = clock_ticks; 
            new_process.initial_burst = burst;
            new_process.remaining_burst = burst;
            new_process.waiting_time = 0;
            new_process.turnaround_time = 0;
            new_process.response_time = 0;
            new_process.is_first_response = false;

            process_list.push_back(new_process);
            
            new_process.state = LISTO; 
            ready_queue.push(new_process);

            std::cout << "Proceso '" << name << "' creado exitosamente con PID " << new_process.pid 
                      << " (Burst: " << burst << ", Prioridad: " << priority << ").\n";

            pthread_mutex_unlock(&system_mutex);//hace unlock para seguir corriendo

        }
        else if (command == "ps") {
            if (tokens.size() > 1) {
                std::cout << "[ERROR DE SINTAXIS]: El comando 'ps' no recibe argumentos.\n";
                continue;
            }

            pthread_mutex_lock(&system_mutex);//hacer lock para imprimir datos
            std::cout << std::left 
                      << std::setw(5)  << "PID" << " | "
                      << std::setw(12) << "NOMBRE" << " | "
                      << std::setw(10) << "ESTADO" << " | "
                      << std::setw(15) << "BURST RESTANTE" << " | "
                      << "PRIORIDAD\n";
            std::cout << std::string(60, '-') << "\n";
            
            for (const auto& p : process_list) {
                std::cout << std::left 
                          << std::setw(5)  << p.pid << " | "
                          << std::setw(12) << p.name << " | "
                          << std::setw(10) << state_to_string(p.state) << " | "
                          << std::setw(14) << (std::to_string(p.remaining_burst) + "s") << " | "
                          << p.priority << "\n";
            }
            pthread_mutex_unlock(&system_mutex);
        }
        else if (command == "pcb") {
            if (tokens.size() != 2) {
                std::cout << "[ERROR DE SINTAXIS]: Uso correcto: pcb <PID>\n";
                continue;
            }
            
            if (!is_number(tokens[1])) {
                std::cout << "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n";
                continue;
            }
            
            int pid_target = std::stoi(tokens[1]);
            bool found = false;
            
            pthread_mutex_lock(&system_mutex);
            for (const auto& p : process_list) {
                if (p.pid == pid_target) {
                    found = true;
                    std::cout << "=================================\n";
                    std::cout << "CONTROL BLOCK DE PROCESO (PCB)\n";
                    std::cout << "PID: " << p.pid << "\n";
                    std::cout << "=================================\n";
                    std::cout << "Nombre del Proceso : " << p.name << "\n";
                    std::cout << "Estado Actual      : " << state_to_string(p.state) << "\n";
                    std::cout << "Prioridad          : " << p.priority << "\n";
                    std::cout << "Tiempo de Llegada  : " << p.arrival_time << " ticks\n";
                    std::cout << "Ráfaga Inicial     : " << p.initial_burst << " ticks\n";
                    std::cout << "Ráfaga Restante    : " << p.remaining_burst << " ticks\n";
                    std::cout << "Tiempo de Espera   : " << p.waiting_time << " ticks\n";
                    
                    if (p.state == FINALIZADO) {
                        std::cout << "Tiempo de Retorno  : " << p.turnaround_time << " ticks\n";
                    } else {
                        std::cout << "Tiempo de Retorno  : N/A (En ejecución o listo)\n";
                    }
                    std::cout << "Tiempo de Respuesta: " << p.response_time << " ticks\n";
                    break;
                }
            }
            pthread_mutex_unlock(&system_mutex);
            
            if (!found) {
                std::cout << "[ERROR]: El PID " << pid_target << " no existe en el sistema.\n";
            }
        }
        else if (command == "load") {
            if (tokens.size() != 2) {
                std::cout << "[ERROR DE SINTAXIS]: Uso correcto: load <archivo>\n";
                continue;
            }

            std::string filename = tokens[1];
            std::ifstream file(filename);

            if (!file.is_open()) {
                std::cout << "[ERROR]: No se pudo abrir o encontrar el archivo '" << filename << "'.\n";
                continue;
            }

            std::string line;
            int loaded_count = 0;

            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                
                if (line.empty() || line[0] == '#') continue; 
                std::stringstream lineStream(line);
                std::string cell;
                std::vector<std::string> row;
                
                while (std::getline(lineStream, cell, ',')) {
                    cell.erase(0, cell.find_first_not_of(" \t"));
                    cell.erase(cell.find_last_not_of(" \t") + 1);
                    row.push_back(cell);
                }
                if (row.size() != 4) {
                    std::cout << "[ADVERTENCIA]: Formato incorrecto omitido en la línea: " << line << "\n";
                    continue;
                }

                std::string name = row[0];
                int burst = std::stoi(row[1]);
                int priority = std::stoi(row[2]);
                int arrival = std::stoi(row[3]);

                // --- SECCIÓN CRÍTICA ---
                pthread_mutex_lock(&system_mutex);
                
                PCB new_process;
                new_process.pid = next_pid++;
                new_process.name = name;
                new_process.state = NUEVO;
                new_process.priority = priority;
                new_process.arrival_time = arrival; 
                new_process.initial_burst = burst;
                new_process.remaining_burst = burst;
                new_process.waiting_time = 0;
                new_process.turnaround_time = 0;
                new_process.response_time = 0;
                new_process.is_first_response = false;

                process_list.push_back(new_process);
                if (arrival <= clock_ticks) {
                    new_process.state = LISTO;
                    ready_queue.push(new_process);
                }
                
                pthread_mutex_unlock(&system_mutex);
                
                loaded_count++;
            }
            file.close();
            std::cout << "Carga masiva finalizada. Se insertaron " << loaded_count << " procesos en el sistema.\n";
        }
        else if (command == "set_sched") {
            if (tokens.size() < 2 || tokens.size() > 3) {
                std::cout << "[ERROR DE SINTAXIS]: Uso correcto: set_sched <algoritmo> [quantum]\n";
                continue;
            }

            std::string algo = tokens[1];
            if (algo != "RR" && algo != "SRTF") {
                std::cout << "[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos válidos: RR, SRTF.\n";
                continue;
            }

            int quantum = 2; 
            if (algo == "RR" && tokens.size() == 3) {
                if (!is_number(tokens[2])) {
                    std::cout << "[ERROR DE TIPO]: El quantum debe ser un número entero positivo.\n";
                    continue;
                }
                quantum = std::stoi(tokens[2]);
                if (quantum <= 0) {
                    std::cout << "[ERROR DE RANGO]: El quantum debe ser mayor a 0.\n";
                    continue;
                }
            } else if (algo == "SRTF" && tokens.size() == 3) {
                std::cout << "[ADVERTENCIA]: SRTF no utiliza quantum. Se ignorará el valor ingresado.\n";
            }

            pthread_mutex_lock(&system_mutex);
            current_algo = algo;
            current_quantum = quantum;
            pthread_mutex_unlock(&system_mutex);

            std::cout << "Algoritmo de planificación actualizado a: " << current_algo;
            if (current_algo == "RR") std::cout << " (Quantum: " << current_quantum << ")";
            std::cout << "\n";
        }
        else if (command == "set_speed") {
            if (tokens.size() != 2) {
                std::cout << "[ERROR DE SINTAXIS]: Uso correcto: set_speed <ms_por_tick>\n";
                continue;
            }
            if (!is_number(tokens[1])) {
                std::cout << "[ERROR DE TIPO]: La velocidad debe ser un valor numérico válido.\n";
                continue;
            }
            
            int speed = std::stoi(tokens[1]);
            if (speed < 50 || speed > 5000) {
                std::cout << "[ERROR DE RANGO]: La velocidad debe estar entre 50 y 5000 ms.\n";
                continue;
            }

            pthread_mutex_lock(&system_mutex);
            speed_ms = speed;
            pthread_mutex_unlock(&system_mutex);

            std::cout << "Velocidad del reloj del sistema configurada a " << speed_ms << " ms por tick.\n";
        }
        else if (command == "start" || command == "pause") {
            if (tokens.size() > 1) {
                std::cout << "[ERROR DE SINTAXIS]: El comando '" << command << "' no recibe argumentos.\n";
                continue;
            }

            bool target = (command == "start");
            pthread_mutex_lock(&system_mutex);
            bool already = (is_cpu_running == target);
            is_cpu_running = target;
            pthread_mutex_unlock(&system_mutex);

            if (already) {
                std::cout << (target ? "La simulación ya está en ejecución.\n" : "La simulación ya está pausada.\n");
            } else {
                std::cout << (target ? "Simulación iniciada.\n" : "Simulación pausada.\n");
            }
        }
        else if (command == "gantt") {
            if (tokens.size() > 1) {
                std::cout << "[ERROR DE SINTAXIS]: El comando 'gantt' no recibe argumentos.\n";
                continue;
            }

            pthread_mutex_lock(&system_mutex);
            if (gantt_log.empty()) {
                std::cout << "El historial de ejecución está vacío.\n";
            } else {
                for (size_t i = 0; i < gantt_log.size(); i++) {
                    const GanttEvent& e = gantt_log[i];
                    if (i > 0) std::cout << " -> ";
                    std::cout << "[" << e.start << "-" << e.end << "s: "
                              << (e.pid == 0 ? "IDLE" : e.name) << "]";
                }
                std::cout << "\n";
            }
            pthread_mutex_unlock(&system_mutex);
        }
        else if (command == "stats") {
            if (tokens.size() > 1) {
                std::cout << "[ERROR DE SINTAXIS]: El comando 'stats' no recibe argumentos.\n";
                continue;
            }

            pthread_mutex_lock(&system_mutex);
            int total = process_list.size();
            int finished = 0;
            double sum_wt = 0, sum_tat = 0;
            for (const auto& p : process_list) {
                if (p.state == FINALIZADO) {
                    finished++;
                    sum_wt += p.waiting_time;
                    sum_tat += p.turnaround_time;
                }
            }
            int busy = 0, elapsed = 0;
            for (const auto& e : gantt_log) {
                elapsed += e.end - e.start;
                if (e.pid != 0) busy += e.end - e.start;
            }
            pthread_mutex_unlock(&system_mutex);

            double avg_wt = finished ? sum_wt / finished : 0.0;
            double avg_tat = finished ? sum_tat / finished : 0.0;
            double usage = elapsed ? 100.0 * busy / elapsed : 0.0;

            std::cout << std::fixed << std::setprecision(2);
            std::cout << "Procesos finalizados : " << finished << " / " << total << "\n";
            std::cout << "Tiempo Promedio de Espera  : " << avg_wt << " ticks\n";
            std::cout << "Tiempo Promedio de Retorno : " << avg_tat << " ticks\n";
            std::cout << "Uso de CPU : " << usage << "%\n";
        }
        else if (command == "help") {
            if (tokens.size() > 1) {
                std::cout << "[ERROR DE SINTAXIS]: El comando 'help' no recibe argumentos.\n";
                continue;
            }
            std::cout << "=== MANUAL DE USUARIO CLI ===\n"
                      << "create <nombre> <burst> [prioridad] : Crea un nuevo proceso.\n"
                      << "load <archivo>                      : Carga procesos desde un archivo de texto.\n"
                      << "ps                                  : Muestra el estado de todos los procesos.\n"
                      << "pcb <PID>                           : Muestra el detalle del PCB de un proceso.\n"
                      << "set_sched <algoritmo> [quantum]     : Cambia algoritmo activo (RR o SRTF).\n"
                      << "set_speed <ms_por_tick>             : Ajusta retardo real del reloj simulado.\n"
                      << "start                               : Inicia o reanuda la simulación de CPU.\n"
                      << "pause                               : Pausa la simulación de CPU.\n"
                      << "gantt                               : Muestra diagrama de Gantt.\n"
                      << "stats                               : Muestra estadísticas del sistema.\n"
                      << "exit                                : Finaliza la ejecución de forma limpia.\n";
        }
        else {
             std::cout << "[ERROR]: Comando no reconocido.\n";
        }
    }
    
    return nullptr;
}

// Hilo 2: Motor de Planificación y Algoritmos de CPU
static int running_pid = -1;   // PID 
static int quantum_used = 0;   // Ticks 

// proceso por PID en la lista global
static PCB* find_process(int pid) {
    for (auto& p : process_list) {
        if (p.pid == pid) return &p;
    }
    return nullptr;
}

static bool in_ready_queue(int pid) {
    bool found = false;
    size_t n = ready_queue.size();
    for (size_t i = 0; i < n; i++) {
        PCB p = ready_queue.front();
        ready_queue.pop();
        if (p.pid == pid) found = true;
        ready_queue.push(p);   
    }
    return found;
}

// Quita de la cola de listos la entrada de un PID 
static void remove_from_ready_queue(int pid) {
    size_t n = ready_queue.size();
    for (size_t i = 0; i < n; i++) {
        PCB p = ready_queue.front();
        ready_queue.pop();
        if (p.pid != pid) ready_queue.push(p);
    }
}

// Elimina de la cola entradas de procesos que ya no están LISTOS
static void purge_ready_queue() {
    size_t n = ready_queue.size();
    for (size_t i = 0; i < n; i++) {
        PCB p = ready_queue.front();
        ready_queue.pop();
        PCB* real = find_process(p.pid);
        if (real && real->state == LISTO && p.pid != running_pid) ready_queue.push(p);
    }
}

// Gestión de llegadas: los procesos NUEVOS cuyo arrival_time ya se cumplió
static void admit_arrivals() {
    for (auto& p : process_list) {
        if (p.state == NUEVO && p.arrival_time <= clock_ticks) {
            p.state = LISTO;
            if (!in_ready_queue(p.pid)) ready_queue.push(p);
        }
    }
}

// Devuelve el proceso que debe usar la CPU en este tick según el algoritmo
static PCB* pick_next_process() {
    PCB* current = (running_pid != -1) ? find_process(running_pid) : nullptr;
    if (current && current->state != EJECUCION) {
        current = nullptr;
        running_pid = -1;
        quantum_used = 0;
    }

    if (current_algo == "SRTF") {
        PCB* best = nullptr;
        size_t n = ready_queue.size();
        for (size_t i = 0; i < n; i++) {
            PCB entry = ready_queue.front();
            ready_queue.pop();
            ready_queue.push(entry);
            PCB* cand = find_process(entry.pid);
            if (cand && cand->state == LISTO &&
                (!best || cand->remaining_burst < best->remaining_burst)) {
                best = cand;
            }
        }

        if (current && (!best || current->remaining_burst <= best->remaining_burst)) {
            return current;
        }
        if (current) {
            current->state = LISTO;          
            ready_queue.push(*current);
        }
        if (best) {
            remove_from_ready_queue(best->pid);
            best->state = EJECUCION;
            running_pid = best->pid;
            quantum_used = 0;
        } else {
            running_pid = -1;
        }
        return best;
    }

    // --- Round Robin ---
    if (current && quantum_used < current_quantum) {
        return current;                    
    }
    if (current) {
        current->state = LISTO;
        ready_queue.push(*current);
        running_pid = -1;
        quantum_used = 0;
    }
    while (!ready_queue.empty()) {
        PCB entry = ready_queue.front();
        ready_queue.pop();
        PCB* next = find_process(entry.pid);
        if (next && next->state == LISTO) {
            next->state = EJECUCION;
            running_pid = next->pid;
            quantum_used = 0;
            return next;
        }
    }
    return nullptr;
}

// Ejecuta un tick completo del planificador
static void scheduler_tick() {
    admit_arrivals();
    purge_ready_queue();

    PCB* proc = pick_next_process();
    int t = clock_ticks;

    if (proc) {
        if (!proc->is_first_response) {
            proc->response_time = t - proc->arrival_time;
            proc->is_first_response = true;
        }
        proc->remaining_burst--;
        quantum_used++;
        gantt_record(proc->pid, proc->name, t);
    } else {
        bool pending = false;
        for (const auto& p : process_list) {
            if (p.state == NUEVO || p.state == LISTO) { pending = true; break; }
        }
        if (pending) gantt_record(0, "IDLE", t);
    }

    for (auto& p : process_list) {
        if (p.state == LISTO) p.waiting_time++;
    }

    clock_ticks++;

    if (proc) {
        if (proc->remaining_burst <= 0) {
            proc->remaining_burst = 0;
            proc->state = FINALIZADO;
            proc->turnaround_time = clock_ticks - proc->arrival_time;
            running_pid = -1;
            quantum_used = 0;
        } else if (current_algo == "RR" && quantum_used >= current_quantum) {
            admit_arrivals();
            proc->state = LISTO;
            ready_queue.push(*proc);
            running_pid = -1;
            quantum_used = 0;
        }
    }
}

// Hilo 2: Motor de Scheduling (CPU) 
void* cpu_thread_func(void* arg) {
    std::cout << "[CPU] Hilo del motor de scheduling iniciado.\n";
    while (is_running) {
        pthread_mutex_lock(&system_mutex);
        bool running = is_cpu_running;
        int delay = speed_ms;
        pthread_mutex_unlock(&system_mutex);

        if (!running) {
            usleep(100000);  
            continue;
        }

        // SECCIÓN CRÍTICA DE LA CPU: 
        // Durante la ejecución de un "tick", el hilo CLI no podrá mostrar resultados
        pthread_mutex_lock(&system_mutex);
        if (is_cpu_running) scheduler_tick();
        pthread_mutex_unlock(&system_mutex);
        usleep(delay * 1000);
    }
    return nullptr;
}

int main() {
    pthread_t cli_thread;
    pthread_t cpu_thread;

    std::cout << "=== Simulador de Kernel y Gestor de Procesos ===\n";

    if (pthread_create(&cpu_thread, nullptr, cpu_thread_func, nullptr) != 0) {
        std::cerr << "Error al crear el hilo CPU.\n";
        return 1;
    }

    if (pthread_create(&cli_thread, nullptr, cli_thread_func, nullptr) != 0) {
        std::cerr << "Error al crear el hilo CLI.\n";
        return 1;
    }

    pthread_join(cli_thread, nullptr);
    pthread_join(cpu_thread, nullptr);

    pthread_mutex_destroy(&system_mutex);

    std::cout << "Simulación cerrada correctamente.\n";
    return 0;
}
