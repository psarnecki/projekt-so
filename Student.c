#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <pthread.h> // Do obsługi wątków   
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

int sem_id, shm_id, w, x;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

// Argument wątku przekazywany do funkcji wątku przy rozpoczęciu jego wykonywania (struktura)
typedef struct {
    int kierunek;  // Numer kierunku
    int student_id; // Liczba studentów
} Student;

// Funkcja symulująca przybycie studentów dla danego kierunku (funkcja wątku)
void* symuluj_przybycie(void* arg) {
    Student* dane = (Student*)arg;

    int czas_przybycia = rand() % 5 + 1; // Losowy czas przybycia (1-5 sekundy)
    sleep(czas_przybycia); // Symulacja opóźnienia
    printf("Student %d z kierunku %d przybył do kolejki.\n", dane->student_id, dane->kierunek);

    // Oczekiwanie na informację od dziekana
    while (1) {
        pthread_mutex_lock(&mutex); // Zablokowanie mutexu, aby uzyskać wyłączny dostęp do zmiennej
        ogloszony_kierunek = *shared_mem;
        //printf("Student %d z kierunku %d sprawdza ogłoszenie: %d oraz pamiec dzielona: %d\n", dane->student_id, dane->kierunek, ogloszony_kierunek, *shared_mem);
        if (ogloszony_kierunek != 0) {
            pthread_mutex_unlock(&mutex); // Odblokowanie mutexu i wyjście z pętli, jeśli dziekan ogłosił kierunek
            break;
        } else {
        pthread_mutex_unlock(&mutex); // Odblokowanie mutexu, jeśli dziekan nie ogłosił kierunku aby inne wątki kierunków mogły kontuunować działanie
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem ogłoszenia w pętli 
        }
    }

    // Reakcja na ogłoszenie dziekana
    if (dane->kierunek == ogloszony_kierunek) {
        printf("Student %d z kierunku %d wchodzi na egzamin.\n", dane->student_id, dane->kierunek);
    } else {
        printf("Student %d z kierunku %d wraca do domu.\n", dane->student_id, dane->kierunek);
        pthread_exit(NULL);
    }

    //pthread_exit(NULL);
}

void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
	if (key == -1){
		perror("Blad tworzenia klucza!");
		cleanup();
		exit(-1);
	} else {
		//printf("key: %d\n", key);
    }

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
	if(shm_id == -1){
		perror("Blad tworzenia segmentu pamieci dzielonej!");
		cleanup();
		exit(-1);
	} else {
		//printf("shm_id: %d\n", shm_id);
    }

    shared_mem = (int *) shmat(shm_id, NULL, 0);
	if(shared_mem == (int *)(-1)){
		perror("Blad przylaczenia pamieci dzielonej!");
		cleanup();
		exit(-1);
	} else {
    	//printf("Pamiec dzielona przypisana poprawnie!\n");
    }

    sem_id = semget(key, 2, IPC_CREAT | 0666);
	if(sem_id == -1) {
		perror("Blad tworzenia semaforow!");
		cleanup();
		exit(-1);
	} else {
		//printf("Zbior semaforow: %d\n", sem_id);
    }
    
    int liczba_kierunkow = rand() % 11 + 5; // Losowanie liczby kierunków z zakresu 5-15
    printf("Liczba kierunków: %d\n", liczba_kierunkow);

    int liczba_studentow[liczba_kierunkow]; // Tworzenie tablicy przechowującej ilość studentów na danym kierunku // Tworzenie tablicy struktur kierunków
    pthread_t watki[liczba_kierunkow][MAX_STUDENTS]; // Tworzenie tablicy wątków o ilości kierunków

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        liczba_studentow[i] = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, liczba_studentow[i]);
    }

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie procesów dla każdego kierunku
    for (int i = 0; i < liczba_kierunkow; i++) {
        pid_t pid = fork();  // Tworzenie procesu dla danego kierunku
        if (pid == 0) {  // Jeśli to dziecko (proces kierunku)
            printf("Proces kierunku %d utworzony, liczba studentów: %d\n", i + 1, liczba_studentow[i]);

            // Tworzenie wątków dla studentów w danym kierunku
            for (int j = 1; j <= liczba_studentow[i]; j++) {
                Student* student = (Student*)malloc(sizeof(Student)); // Dynamiczne alokowanie pamięci dla struktury Student i zwrócenie do niej wskaźnika
                student->kierunek = i + 1;
                student->student_id = j;

                // Tworzenie wątku dla studenta
                pthread_create(&watki[i][j-1], NULL, symuluj_przybycie, (void*)student);
            }

            // Czekanie na zakończenie wątków studentów
            for (int j = 0; j < liczba_studentow[i]; j++) {
                pthread_join(watki[i][j], NULL);
            }

            // Sprawdzenie, czy proces jest procesem wybranego kierunku
            if (i + 1 != ogloszony_kierunek) {
                exit(0);  // Zakończenie procesu kierunku, jeśli nie jest to wybrany kierunek
            } else {
                /* 
                    Miejsce na przyszły kod dla procesu wybranego kierunku
                */
                exit(0);
            }
        } else {
            printf("Proces główny: utworzono proces dla kierunku %d.\n", i + 1);
        }
    }

    sem_p(sem_id, SEM_STUDENT);
    ogloszony_kierunek = *shared_mem;
    printf("\nOdebrany ogłoszony kierunek: %d\n", ogloszony_kierunek);
    sem_v(sem_id, SEM_DZIEKAN);

    // Czekanie na zakończenie wszystkich procesów potomnych
    while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Blad oczekiwania na zakonczenie procesu potomnego!");
            exit(3);
        }
        printf("Zakonczyl sie proces potomny o PID=%d i statusie = %d\n", w, x);
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
    if (zmien_sem == -1)
      {
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        }
        else
        {
        printf("Nie moglem zamknac semafora.\n");
        exit(EXIT_FAILURE);
        }
      }
    else
      {
        printf("Semafor zostal zamkniety.\n");
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
    if (zmien_sem == -1)
      {
        printf("Nie moglem otworzyc semafora.\n");
        exit(EXIT_FAILURE);
      }
    else
      {
        printf("Semafor zostal otwarty.\n");
      }
}

void cleanup()
{
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