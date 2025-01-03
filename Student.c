#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> // Do obsługi pamięci dzielonej
#include <sys/sem.h> // Do obsługi semaforów
#include <errno.h>

#define MIN_STUDENTS 80
#define MAX_STUDENTS 160
#define SEM_SIZE sizeof(int)
#define SEM_DZIEKAN 1
#define SEM_STUDENT 0
#define SEM_BUDYNEK 3

int sem_id, sem_komisja_id, shm_id, w, x;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

// Argument procesu studenta przekazywany w strukturze
typedef struct {
    int kierunek;  // Numer kierunku
    pid_t pid;     // PID procesu studenta jako identyfikator studenta
} Student;

// Funkcja symulująca przybycie studenta do kolejki przed budynkiem
void symuluj_przybycie(Student* dane) {
    printf("Student o PID %d z kierunku %d przybył do kolejki.\n", dane->pid, dane->kierunek);

    // Oczekiwanie na ogłoszenie od dziekana
    while (1) {
        sem_p(sem_id, SEM_STUDENT); // Oczekiwanie na dostęp do pamięci dzielonej
        ogloszony_kierunek = *shared_mem;
        if (ogloszony_kierunek != 0) {
            sem_v(sem_id, SEM_STUDENT); // Zwolnienie semafora
            break;
        }
        sem_v(sem_id, SEM_STUDENT); // Zwolnienie semafora
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem ogłoszenia
    }

    // Reakcja na ogłoszenie dziekana
    if (dane->kierunek == ogloszony_kierunek) {
        printf("Student o PID %d z kierunku %d próbuje wejść na egzamin.\n", dane->pid, dane->kierunek);

        // Czekanie na dostępne miejsce w budynku
        sem_p(sem_komisja_id, SEM_BUDYNEK);
        printf("Student %d z kierunku %d wchodzi na egzamin.\n", dane->pid, dane->kierunek);

        // Oczekiwanie na zadanie pytań
        printf("Student %d z kierunku %d otrzymuje pytania.\n", dane->pid, dane->kierunek);
        sleep(rand() % 3 + 1);  // Symulacja czasu oczekiwania na pytania

        // Czas przygotowania odpowiedzi
        int czas_przygotowania = rand() % 5 + 3;
        printf("Student %d z kierunku %d przygotowuje odpowiedź przez %d sekund.\n", dane->pid, dane->kierunek, czas_przygotowania);
        sleep(czas_przygotowania);  // Symulacja czasu przygotowania odpowiedzi

        // Wraca do komisji z odpowiedzią
        printf("Student %d z kierunku %d wraca do komisji z odpowiedzią.\n", dane->pid, dane->kierunek);

        // Otrzymanie oceny
        printf("Komisja ocenia odpowiedź studenta %d z kierunku %d.\n", dane->pid, dane->kierunek);

        // Zakończenie egzaminu
        printf("Student %d z kierunku %d kończy egzamin.\n", dane->pid, dane->kierunek);

        // Zwolnienie miejsca w budynku
        sem_v(sem_komisja_id, SEM_BUDYNEK);  // Zwolnienie miejsca w budynku
    } else {
        printf("Student o PID %d z kierunku %d wraca do domu.\n", dane->pid, dane->kierunek);
    }
}

void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
    key_t key_komisja = ftok(".", 'D');
    if (key == -1 || key_komisja == -1) {
        perror("Blad tworzenia klucza!");
        cleanup();
        exit(-1);
    }

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Blad tworzenia segmentu pamieci dzielonej!");
        cleanup();
        exit(-1);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
    if (shared_mem == (int *)(-1)) {
        perror("Blad przylaczenia pamieci dzielonej!");
        cleanup();
        exit(-1);
    }

    sem_id = semget(key, 2, IPC_CREAT | 0666);
    sem_komisja_id = semget(key_komisja, 4, IPC_CREAT | 0666);
    if (sem_id == -1 || sem_komisja_id == -1) {
        perror("Blad tworzenia semaforow!");
        cleanup();
        exit(-1);
    }

    semctl(sem_komisja_id, SEM_BUDYNEK, SETVAL, 3);

    int liczba_kierunkow = rand() % 11 + 5; // Losowanie liczby kierunków
    printf("Liczba kierunkow: %d\n", liczba_kierunkow);

    int liczba_studentow[liczba_kierunkow]; // Tworzenie tablicy przechowującej ilość studentów na danym kierunku

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        liczba_studentow[i] = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, liczba_studentow[i]);
    }

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie procesów dla studentów
    for (int i = 0; i < liczba_kierunkow; i++) {
        for (int j = 1; j <= liczba_studentow[i]; j++) {
            usleep(rand() % 50000);
            pid_t pid = fork();  // Tworzenie procesu dla studenta

            if (pid == 0) {  // Jeśli to proces studenta
                Student* student = (Student*)malloc(sizeof(Student)); // Alokacja pamięci dla studenta
                student->kierunek = i + 1;
                student->pid = getpid(); // Ustawienie PID jako ID studenta

                symuluj_przybycie(student);

                // Sprawdzenie, czy proces jest procesem wybranego kierunku
                if (i + 1 != ogloszony_kierunek) {
                    free(student); // Zwolnienie pamięci po zakończeniu pracy studenta
                    exit(0);  // Zakończenie procesu studenta, jeśli nie jest to wybrany kierunek
                } else {
                    /* 
                        Miejsce na przyszły kod dla procesu wybranego kierunku
                    */
                    sleep(5);
                    free(student); // Zwolnienie pamięci po zakończeniu pracy studenta
                    exit(0);
                }
            }
        }
    }

    // Czekanie na zakończenie wszystkich procesów potomnych
    while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Blad oczekiwania na zakonczenie procesu potomnego!");
            exit(3);
        }
        //printf("Zakonczyl sie proces potomny o PID=%d i statusie = %d\n", w, x);
    }

    cleanup();

    printf("\nSymulacja zakończona.\n");

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

void cleanup() {
    printf("Zostala wywolana funkcja czyszczaca!");
    if (shared_mem != NULL && shmdt(shared_mem) == -1) {
        perror("Blad odlaczania pamieci dzielonej");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Blad usuwania segmentu pamieci dzielonej");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Blad usuwania semaforow");
    }
}