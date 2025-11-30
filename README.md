# SO-tarea2
comandos para ejecutar sim.c
-
gcc -O2 -std=c11 -o sim sim.c

./sim 2 4096 --verbose trace.txt   //trace.txt es el nombre del archivo .txt a usar

comandos para ejecutar main.c:

gcc -o test main.c barrera.c -pthread

/test N E   //N es el numero de hebras a crear y E el numero de etapas, si se dejan en blanco se usaran N = 5 y E = 4
