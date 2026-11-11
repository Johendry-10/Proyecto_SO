#pragma once
#include <iostream>
#include <string>
#include <queue>
#include <vector>
#include <pthread.h>

// Definición de los estados del proceso
enum ProcessState {
    NUEVO,
    LISTO,
    EJECUCION,
    PAUSADO_BLOQUEADO,
    FINALIZADO
};

// Estructura del Process Control Block (PCB)
struct PCB {
    int pid;                  
    std::string name;        
    ProcessState state;      
    int priority;             
    int arrival_time;         
    int initial_burst;       
    int remaining_burst;      
    int waiting_time;         
    int turnaround_time;      
    int response_time;        
    
    bool is_first_response;   
};

//ESTRUCTURAS DE DATOS COMPARTIDAS 

// Cola de procesos LISTOS
extern std::queue<PCB> ready_queue;

// Lista global de todos los procesos (útil para los comandos 'ps' y 'pcb')
extern std::vector<PCB> process_list; 

// Variable global para el reloj simulado discreto (Clock Ticks)
extern int clock_ticks;

// Cerrojo de exclusión mutua para proteger las colas y variables globales
extern pthread_mutex_t system_mutex;

// Variables para el control de la CPU y Planificación
extern std::string current_algo;
extern int current_quantum;
extern int speed_ms;
extern bool is_cpu_running;

struct GanttEvent {
    int pid;
    std::string name;
    int start;
    int end;
};

extern std::vector<GanttEvent> gantt_log;

void gantt_record(int pid, const std::string& name, int tick);