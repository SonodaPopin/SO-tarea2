#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

//Estructuras para tabla de páginas (hash simple con encadenamiento)
typedef struct PTNode {
    uint64_t npv;             /* número de página virtual */
    int frame;                /* índice de marco físico asignado, -1 si no asignado */
    struct PTNode *next;
} PTNode;

#define PT_HASHSZ 10007  /* prime-ish hash size; para trazas pequeñas/medianas */

static PTNode *pt_hash[PT_HASHSZ];

static unsigned pt_hash_fn(uint64_t k) {
    /* simple 64-bit mix -> mod table size */
    k ^= (k >> 33);
    k *= 0xff51afd7ed558ccdULL;
    k ^= (k >> 33);
    return (unsigned)(k % PT_HASHSZ);
}

static PTNode *pt_find(uint64_t npv) {
    unsigned h = pt_hash_fn(npv);
    PTNode *p = pt_hash[h];
    while (p) {
        if (p->npv == npv) return p;
        p = p->next;
    }
    return NULL;
}

static PTNode *pt_insert(uint64_t npv, int frame) {
    unsigned h = pt_hash_fn(npv);
    PTNode *p = malloc(sizeof(PTNode));
    if (!p) { perror("malloc"); exit(1); }
    p->npv = npv;
    p->frame = frame;
    p->next = pt_hash[h];
    pt_hash[h] = p;
    return p;
}

static void pt_remove(uint64_t npv) {
    unsigned h = pt_hash_fn(npv);
    PTNode *p = pt_hash[h], *prev = NULL;
    while (p) {
        if (p->npv == npv) {
            if (prev) prev->next = p->next;
            else pt_hash[h] = p->next;
            free(p);
            return;
        }
        prev = p;
        p = p->next;
    }
}

//Estructuras para marcos y reloj

typedef struct {
    int occupied;      /* 0 libre, 1 ocupado */
    uint64_t npv;      /* página virtual mapeada en el marco (solo válida si occupied==1) */
    int ref;           /* bit de referencia (R) para Clock */
} Frame;

static Frame *frames = NULL;
static int Nframes = 0;
static int clock_ptr = 0; /* puntero del reloj (índice de frames) */

//Utilidades

static void usage_and_exit(const char *prog) {
    fprintf(stderr, "Uso: %s Nmarcos tamañomarco [--verbose] traza.txt\n", prog);
    fprintf(stderr, "Ej: %s 8 4096 --verbose trace1.txt\n", prog);
    exit(1);
}

static int is_power_of_two(uint64_t x) {
    return x && ((x & (x-1)) == 0);
}

/* Convertir string que puede ser decimal o 0xHEX a uint64_t */
static int parse_address(const char *s, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    if (strlen(s) > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        *out = strtoull(s, &end, 16);
    } else {
        *out = strtoull(s, &end, 10);
    }
    if (errno != 0 || end == s) return 0;
    return 1;
}

/* Buscar marco libre; devuelve índice o -1 si no hay */
static int find_free_frame(void) {
    for (int i = 0; i < Nframes; ++i) {
        if (!frames[i].occupied) return i;
    }
    return -1;
}

/* Algoritmo Clock: devuelve índice de marco víctima (y actualiza clock_ptr) */
static int clock_choose_victim(void) {
    while (1) {
        if (!frames[clock_ptr].occupied) {
            int victim = clock_ptr;
            clock_ptr = (clock_ptr + 1) % Nframes;
            return victim;
        }
        if (frames[clock_ptr].ref == 0) {
            int victim = clock_ptr;
            clock_ptr = (clock_ptr + 1) % Nframes;
            return victim;
        } else {
            /* dar segunda oportunidad: limpiar bit de referencia y avanzar */
            frames[clock_ptr].ref = 0;
            clock_ptr = (clock_ptr + 1) % Nframes;
        }
    }
}

// Simulador

int main(int argc, char **argv) {
    if (argc < 4) usage_and_exit(argv[0]);

    int argi = 1;
    Nframes = atoi(argv[argi++]);
    if (Nframes <= 0) { fprintf(stderr, "Nmarcos debe ser > 0\n"); return 1; }

    uint64_t page_size = strtoull(argv[argi++], NULL, 10);
    if (page_size == 0) { fprintf(stderr, "tamañomarco inválido\n"); return 1; }
    if (!is_power_of_two(page_size)) {
        fprintf(stderr, "tamañomarco debe ser potencia de 2 (ej: 4096)\n");
        return 1;
    }

    int verbose = 0;
    const char *tracefile = NULL;
    if (argc - argi == 1) {
        tracefile = argv[argi];
    } else if (argc - argi == 2) {
        if (strcmp(argv[argi], "--verbose") == 0) {
            verbose = 1;
            tracefile = argv[argi+1];
        } else {
            usage_and_exit(argv[0]);
        }
    } else {
        usage_and_exit(argv[0]);
    }

    FILE *f = fopen(tracefile, "r");
    if (!f) { perror("fopen"); return 1; }

    /* pre-cálculos */
    unsigned b = 0;
    uint64_t tmp = page_size;
    while ((1ULL << b) < page_size) ++b;
    uint64_t mask = page_size - 1;

    /* inicializar marcos */
    frames = calloc(Nframes, sizeof(Frame));
    if (!frames) { perror("calloc"); return 1; }
    for (int i = 0; i < Nframes; ++i) {
        frames[i].occupied = 0;
        frames[i].npv = 0;
        frames[i].ref = 0;
    }

    /* estadísticas */
    uint64_t total_refs = 0;
    uint64_t page_faults = 0;

    /* limpiar tabla de páginas (hash) */
    for (int i = 0; i < PT_HASHSZ; ++i) pt_hash[i] = NULL;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* trim */
        char *s = line;
        while (*s && isspace((unsigned char)*s)) ++s;
        if (*s == '\0' || *s == '#') continue; /* blank o comentario */
        /* cut trailing whitespace/newline */
        char *e = s + strlen(s) - 1;
        while (e >= s && isspace((unsigned char)*e)) { *e = '\0'; --e; }

        uint64_t va;
        if (!parse_address(s, &va)) {
            fprintf(stderr, "Advertencia: no pude parsear dirección '%s' — se omite\n", s);
            continue;
        }

        total_refs++;

        uint64_t offset = va & mask;
        uint64_t npv = va >> b;

        PTNode *entry = pt_find(npv);
        if (entry && entry->frame >= 0) {
            int fidx = entry->frame;
            frames[fidx].ref = 1; /* marcar referencia */
            uint64_t df = (((uint64_t)fidx) << b) | offset;
            if (verbose) {
                printf("VA=%#llx npv=%llu offset=%#llx HIT frame=%d DF=%#llx\n",
                       (unsigned long long)va,
                       (unsigned long long)npv,
                       (unsigned long long)offset,
                       fidx,
                       (unsigned long long)df);
            }
        } else {
            /* FALLO de página */
            page_faults++;
            int fidx = find_free_frame();
            if (fidx >= 0) {
                /* asignar libre */
                frames[fidx].occupied = 1;
                frames[fidx].npv = npv;
                frames[fidx].ref = 1;
                /* crear/actualizar entrada en tabla de páginas */
                if (entry) {
                    entry->frame = fidx;
                } else {
                    pt_insert(npv, fidx);
                }
                uint64_t df = (((uint64_t)fidx) << b) | offset;
                if (verbose) {
                    printf("VA=%#llx npv=%llu offset=%#llx FALLO asignar-libre frame=%d DF=%#llx\n",
                           (unsigned long long)va,
                           (unsigned long long)npv,
                           (unsigned long long)offset,
                           fidx,
                           (unsigned long long)df);
                }
            } else {
                /* elegir víctima con Clock */
                int victim = clock_choose_victim();
                uint64_t victim_npv = frames[victim].npv;
                /* invalidar entrada de tabla de páginas para la página expulsada */
                pt_remove(victim_npv);
                /* mapear nuevo npv en ese marco */
                frames[victim].npv = npv;
                frames[victim].ref = 1;
                /* actualizar tabla de páginas: insertar mapping npv a victim */
                pt_insert(npv, victim);
                uint64_t df = (((uint64_t)victim) << b) | offset;
                if (verbose) {
                    printf("VA=%#llx npv=%llu offset=%#llx FALLO reemplazar victim_frame=%d (evict npv=%llu) DF=%#llx\n",
                           (unsigned long long)va,
                           (unsigned long long)npv,
                           (unsigned long long)offset,
                           victim,
                           (unsigned long long)victim_npv,
                           (unsigned long long)df);
                }
            }
        }
    }

    fclose(f);

    double tasa = (total_refs == 0) ? 0.0 : ((double)page_faults / (double)total_refs);

    printf("Totales: Referencias=%llu, Fallos de pagina=%llu, Tasa=%.4f\n",
           (unsigned long long)total_refs,
           (unsigned long long)page_faults,
           tasa);

    /* liberar memoria */
    for (int i = 0; i < PT_HASHSZ; ++i) {
        PTNode *p = pt_hash[i];
        while (p) {
            PTNode *tmp = p->next;
            free(p);
            p = tmp;
        }
    }
    free(frames);

    return 0;
}
