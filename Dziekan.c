#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // do obsługi pamięci dzielonej
#include <sys/sem.h> // do obsługi semaforów
#include <errno.h>

#define SEM_SIZE sizeof(int)
#define SEM_STUDENT 0
#define SEM_DZIEKAN 1

int sem_id, shm_id;
int *shared_mem = NULL;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
    if (key == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    if (shared_mem == (int *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 2, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    semctl(sem_id, SEM_DZIEKAN, SETVAL, 1);
	semctl(sem_id, SEM_STUDENT, SETVAL, 0);

    int kierunek = rand() % 10 + 1; // Kierunki od 1 do 10
    printf("Dziekan ogłasza: Kierunek %d pisze egzamin.\n", kierunek);

    sem_p(sem_id, SEM_DZIEKAN);
    *shared_mem = kierunek; // Zapisanie numeru kierunku do pamięci współdzielonej
    sem_v(sem_id, SEM_STUDENT);

    printf("Dziekan zakończył działanie.\n");

    //cleanup();
    return 0;
}

// Zmniejszenie wartości semafora - zamknięcie
void sem_p(int sem_id, int sem_num) {
    int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = -1;
    bufor_sem.sem_flg = 0;
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        } else {
        perror("(Student) Nie mogłem zamknąć semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

// Zwiększenie wartości semafora - otwarcie
void sem_v(int sem_id, int sem_num) {
	  int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0; // flaga 0 (zamiast SEM_UNDO) żeby po wyczerpaniu short inta nie wyrzuciło błędu
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        perror("Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup()
{
    //printf("Została wywołana funkcja czyszcząca!\n");
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Dziekan!");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Dziekan!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Dziekan!");
    }
}