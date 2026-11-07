#pragma once
#include <iostream>
#include <string>
#include <queue>
#include <vector>
#include <pthread.h>

// Definición de los estados del proceso exigidos
enum ProcessState {
    NUEVO,
    LISTO,
    EJECUCION,
    PAUSADO_BLOQUEADO,
    FINALIZADO
};

// Estructura del Process Control Block (PCB)
struct PCB {
    int pid;                  // PID único autoincremental
    std::string name;         // Nombre sin espacios
    ProcessState state;       // Estado actual del proceso
    int priority;             // Prioridad (1 a 5, por defecto 3)
    
    // Tiempos para simulación y métricas
    int arrival_time;         // Tiempo de llegada (0 para interactivos, o t para lotes)
    int initial_burst;        // Ráfaga inicial
    int remaining_burst;      // Ráfaga restante
    
    // Estadísticas
    int waiting_time;         // Tiempo de Espera (WT)
    int turnaround_time;      // Tiempo de Retorno (TAT)
    int response_time;        // Tiempo de Respuesta (RT)
    
    // Bandera para saber si ya se le midió el tiempo de respuesta
    bool is_first_response;   
};

// --- ESTRUCTURAS DE DATOS COMPARTIDAS ---

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