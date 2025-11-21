#include "barrera.h"

// Inicializacion de la barrera
int barrera_init(barrera_t *b, int N) {
    if (N <= 0) return -1;

    b->N = N;
    b->count = 0;
    b->etapa = 0;

    if (pthread_mutex_init(&b->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(&b->cond, NULL) != 0) {
        pthread_mutex_destroy(&b->mutex);
        return -1;
    }

    return 0;
}

// Destruccion de la barrera
int barrera_destroy(barrera_t *b) {
    int r1 = pthread_mutex_destroy(&b->mutex);
    int r2 = pthread_cond_destroy(&b->cond);
    return (r1 != 0) ? r1 : r2;
}

// Implementacion de wait() de la barrera reutilizable
void barrera_wait(barrera_t *b) {
    pthread_mutex_lock(&b->mutex);

    int mi_etapa = b->etapa;   // capturar etapa actual

    b->count++;   // esta hebra llego

    if (b->count == b->N) {
        // Ultima hebra en llegar → resetear y avanzar etapa
        b->etapa++;
        b->count = 0;
        pthread_cond_broadcast(&b->cond);  // despertar a todas
    } else {
        // Esperar mientras la etapa no cambie
        while (b->etapa == mi_etapa) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    }

    pthread_mutex_unlock(&b->mutex);
}