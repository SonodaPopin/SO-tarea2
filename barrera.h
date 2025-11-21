#ifndef BARRERA_H
#define BARRERA_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;   // exclusion mutua
    pthread_cond_t cond;     // condicion para esperar
    int count;               // cuántas hebras han llegado en etapa actual
    int N;                   // numero total de hebras a sincronizar
    int etapa;               // identificador de etapa actual
} barrera_t;

// Inicializa la barrera para N hebras
int barrera_init(barrera_t *b, int N);

// Libera recursos de la barrera
int barrera_destroy(barrera_t *b);

// Operacion wait() de la barrera reutilizable
void barrera_wait(barrera_t *b);

#endif