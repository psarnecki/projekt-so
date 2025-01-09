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

#define SEM_KOMISJA 0
#define SEM_EGZAMIN 1
#define SEM_BUDYNEK 2

int sem_komisja_id, shm_komisja_id, w, x;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

typedef struct {
    int student_pid;
    int ocena;
} Ocena;

Ocena *shared_ocena;

// Funkcja wątku: symulacja pracy członka komisji
void* czlonek_komisji() {}

// Funkcja procesu komisji
void* komisja_A() {
    while (1) {
        sem_p(sem_komisja_id, SEM_KOMISJA); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        // Sprawdzenie, czy są jeszcze studenci do egzaminu
        int studenci_w_budynku = semctl(sem_komisja_id, SEM_BUDYNEK, GETVAL);
        if (studenci_w_budynku == 3) {
            printf("Brak studentów w budynku. Komisja kończy pracę.\n");
            break;  // Zakończenie pracy komisji
        }

        //printf("Komisja przygotowuje pytania!\n");
        //sleep(rand() % 3 + 2);  // Symulacja czasu przygotowania pytań
        //printf("Pytania gotowe!\n");

        // Zapisywanie losowej oceny do pamięci dzielonej
        int ocena = rand() % 5 + 1;  // Ocena od 1 do 5
        shared_ocena->ocena = ocena;

        printf("Komisja wystawiła ocenę: %d dla PID: %d\n", ocena, shared_ocena->student_pid);

    }
}

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

void cleanup();

int main() {
    srand(time(NULL));  // Inicjalizacja generatora liczb losowych

    key_t key_komisja = ftok(".", 'D');
    if (key_komisja == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(-1);
    }

    shm_komisja_id = shmget(key_komisja, sizeof(Ocena), IPC_CREAT | 0666);
    if (shm_komisja_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(-1);
    }
    
    shared_ocena = (Ocena *)shmat(shm_komisja_id, NULL, 0);
    if (shared_ocena == (Ocena *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(-1);
    }

    // Tworzenie semafora
    sem_komisja_id = semget(key_komisja, 3, IPC_CREAT | 0666);
    if (sem_komisja_id == -1) {
        perror("Błąd tworzenia semaforów!");
        exit(EXIT_FAILURE);
    }

    semctl(sem_komisja_id, SEM_BUDYNEK, SETVAL, 3);
    semctl(sem_komisja_id, SEM_KOMISJA, SETVAL, 0); 
    semctl(sem_komisja_id, SEM_EGZAMIN, SETVAL, 1);

    // Tworzenie procesu komisji A
    if (fork() == 0) {
        stworz_komisja_A();  // Uruchomienie funkcji dla komisji A
        exit(0);  // Zakończenie procesu komisji A
    }

    // Tworzenie procesu komisji B
    if (fork() == 0) {
        stworz_komisja_B();  // Uruchomienie funkcji dla komisji B
        exit(0);  // Zakończenie procesu komisji B
    }

    // Proces główny czeka na zakończenie wszystkich procesów
        while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!");
            exit(3);
        }
        //printf("Zakonczyl sie proces potomny o PID=%d i statusie = %d\n", w, x);
    }

    printf("Wszystkie procesy komisji zakończyły działanie.\n");

    cleanup();

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
        printf("Nie mogłem zamknąć semafora.\n");
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
        printf("Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    printf("Została wywołana funkcja czyszcząca!");
    if (shared_ocena != NULL && shmdt(shared_ocena) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Komisja!");
    }
    if (shmctl(shm_komisja_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Komisja!");
    }
    if (semctl(sem_komisja_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Komisja!");
    }
}
