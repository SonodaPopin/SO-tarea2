#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "barrera.h"

//Estructura con los argumentos que recibe cada hebra
typedef struct {
    int tid;
    int etapas;
    barrera_t *barrera;
} thread_arg_t;

//Funcion que simula trabajo para cada hebra, imprimiendo mensajes en el proceso
void *trabajo_hebra(void *arg) {
    thread_arg_t *info = (thread_arg_t *)arg;

    for (int e = 0; e < info->etapas; e++) {
        int delay = (rand() % 100) * 1000;
        usleep(delay);

        printf("[T%d] esperando en etapa %d\n", info->tid, e);
        fflush(stdout);

        //Garantia de que llegaron todas las hebras antes de proseguir
        barrera_wait(info->barrera);

        printf("[T%d] paso barrera en etapa %d\n", info->tid, e);
        fflush(stdout);
    }

    return NULL;
}

//Funcion principal de prueba que crea la barrera y las hebras, hace las pruebas con los parametros dados y luego destruye la barrera
int main(int argc, char *argv[]) {
    //Parametros predeterminados N: hebras, E: etapas
    int N = 5;
    int E = 4;

    if (argc >= 2) N = atoi(argv[1]);
    if (argc >= 3) E = atoi(argv[2]);

    printf("Creando %d hebras, %d etapas\n", N, E);

    pthread_t threads[N];
    thread_arg_t args[N];
    barrera_t barrera;

    srand(time(NULL));

    if (barrera_init(&barrera, N) != 0) {
        fprintf(stderr, "Error inicializando barrera\n");
        return 1;
    }

    for (int i = 0; i < N; i++) {
        args[i].tid = i;
        args[i].etapas = E;
        args[i].barrera = &barrera;

        if (pthread_create(&threads[i], NULL, trabajo_hebra, &args[i]) != 0) {
            fprintf(stderr, "Error creando hebra %d\n", i);
            return 1;
        }
    }

    //Garantia de que todas las hebras terminaron antes de destruir la barrera
    for (int i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }

    barrera_destroy(&barrera);

    printf("Todas las hebras terminaron correctamente.\n");
    return 0;
}

