#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // Do obsługi pamięci dzielonej
#include <sys/sem.h> // Do obsługi semaforów
#include <pthread.h> // Do obługi wątków
#include <errno.h>

#define SEM_BUDYNEK 3

int sem_komisja_id;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

void* czlonek_komisji() {}

void* komisja_A() {}

void* komisja_B() {}

void stworz_komisja_A() {
    pthread_t przewodniczacy, czlonek_1, czlonek_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&przewodniczacy, NULL, komisja_A, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&czlonek_1, NULL, czlonek_komisji, NULL);
    pthread_create(&czlonek_2, NULL, czlonek_komisji, NULL);

    pthread_join(przewodniczacy, NULL);
    pthread_join(czlonek_1, NULL);
    pthread_join(czlonek_2, NULL);

    printf("Komisja A zakończyła swoją pracę.\n");
}

void stworz_komisja_B() {
    pthread_t przewodniczacy, czlonek_1, czlonek_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&przewodniczacy, NULL, komisja_B, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&czlonek_1, NULL, czlonek_komisji, NULL);
    pthread_create(&czlonek_2, NULL, czlonek_komisji, NULL);

    pthread_join(przewodniczacy, NULL);
    pthread_join(czlonek_1, NULL);
    pthread_join(czlonek_2, NULL);

    printf("Komisja B zakończyła swoją pracę.\n");
}

int main() {
    srand(time(NULL));  // Inicjalizacja generatora liczb losowych

    key_t key_komisja = ftok(".", 'D');

    // Tworzenie semafora
    sem_komisja_id = semget(key_komisja, 4, IPC_CREAT | 0666);
    if (sem_komisja_id == -1) {
        perror("Błąd tworzenia semafora");
        exit(EXIT_FAILURE);
    }

    semctl(sem_komisja_id, SEM_BUDYNEK, SETVAL, 3);

    // Tworzenie procesu komisji A
    if (fork() == 0) {
        stworz_komisja_A(); 
        exit(0); 
    }

    // Tworzenie procesu komisji B
    if (fork() == 0) {
        stworz_komisja_B(); 
        exit(0);  
    }

    // Proces główny czeka na zakończenie wszystkich procesów
    for (int i = 0; i < 2; i++) {
        wait(NULL);
    }

    printf("Wszystkie procesy komisji zakończyły działanie.\n");

    return 0;
}

// zmniejszenie wartości semafora - zamknięcie
void sem_p(int sem_id, int sem_num)
{
    int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = -1;
    bufor_sem.sem_flg = 0;
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        }
        else
        {
        printf("Nie moglem zamknac semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

// zwiekszenie wartości semafora - otwarcie
void sem_v(int sem_id, int sem_num)
{
	int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0; // flaga 0 (zamiast SEM_UNDO) żeby po wyczerpaniu short inta nie wyrzuciło błędu
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        printf("Nie moglem otworzyc semafora.\n");
        exit(EXIT_FAILURE);
    }
}