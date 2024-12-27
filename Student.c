#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
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

int sem_id, shm_id;
int ogloszony_kierunek = 0;
int *shared_mem = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

// Argument wątku przekazywany do funkcji wątku przy rozpoczęciu jego wykonywania (struktura)
typedef struct {
    int kierunek;  // Numer kierunku
    int liczba_studentow; // Liczba studentów
} Kierunek;

// Funkcja symulująca przybycie studentów dla danego kierunku (funkcja wątku)
void* symuluj_przybycie(void* arg) {
    Kierunek* dane = (Kierunek*)arg;

    for (int i = 1; i <= dane->liczba_studentow; i++) {
        int czas_przybycia = rand() % 2 + 1; // Losowy czas przybycia (1-2 sekundy)
        sleep(czas_przybycia); // Symulacja opóźnienia
        printf("Student %d z kierunku %d przybył do kolejki.\n", i, dane->kierunek);
    }

    // Oczekiwanie na informację od dziekana
    while (1) {
        pthread_mutex_lock(&mutex); // Zablokowanie mutexu, aby uzyskać wyłączny dostęp do zmiennej
        if (ogloszony_kierunek != 0) {
            pthread_mutex_unlock(&mutex); // Odblokowanie mutexu i wyjście z pętli, jeśli dziekan ogłosił kierunek
            break;
        }
        pthread_mutex_unlock(&mutex); // Odblokowanie mutexu, jeśli dziekan nie ogłosił kierunku aby inne wątki kierunków mogły kontuunować działanie
        sleep(1); // 1 sekunda przerwy przed ponownym sprawdzeniem ogłoszenia w pętli 
    }

    // Sprawdzenie ogłoszonego kierunku
    if (dane->kierunek == ogloszony_kierunek) {
        printf("Studenci z kierunku %d wchodzą na egzamin.\n", dane->kierunek);
    } else {
        printf("Studenci z kierunku %d wracają do domu.\n", dane->kierunek);
    }

    pthread_exit(NULL);
}

void cleanup();

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

    key_t key = ftok(".", 'S');
	if (key == -1){
		perror("Blad tworzenia klucza!");
		
		exit(-1);
	} else
		//printf("key: %d\n", key);

    shm_id = shmget(key, SEM_SIZE, IPC_CREAT | 0666);
	if(shm_id == -1){
		perror("Blad tworzenia segmentu pamieci dzielonej!");
		
		exit(-1);
	} else
		//printf("shm_id: %d\n", shm_id);

    shared_mem = (int *) shmat(shm_id, NULL, 0);
	if(shared_mem == (int *)(-1)){
		perror("Blad przylaczenia pamieci dzielonej!");
		
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

    Kierunek kierunki[liczba_kierunkow]; // Tworzenie tablicy struktur kierunków
    pthread_t watki[liczba_kierunkow]; // Tworzenie tablicy wątków o ilości kierunków

    printf("\nLosowanie liczby studentów na kierunkach...\n");
    for (int i = 0; i < liczba_kierunkow; i++) {
        kierunki[i].kierunek = i + 1;
        kierunki[i].liczba_studentow = rand() % (MAX_STUDENTS - MIN_STUDENTS + 1) + MIN_STUDENTS;
        printf("Kierunek %d: %d studentów\n", i + 1, kierunki[i].liczba_studentow);
    }

    printf("\nRozpoczynanie symulacji przybycia studentów...\n");

    // Tworzenie wątków dla każdego kierunku
    for (int i = 0; i < liczba_kierunkow; i++) {
        if (pthread_create(&watki[i], NULL, symuluj_przybycie, (void*)&kierunki[i]) != 0) {
            perror("Błąd tworzenia wątku");
            exit(EXIT_FAILURE);
        }
    }

    sem_p(sem_id, SEM_STUDENT);
    ogloszony_kierunek = *shared_mem;
    printf("\nOdebrany ogłoszony kierunek: %d\n", ogloszony_kierunek);
    sem_v(sem_id, SEM_DZIEKAN);

    cleanup();

    // Czekanie na zakończenie wszystkich wątków
    for (int i = 0; i < liczba_kierunkow; i++) {
        pthread_join(watki[i], NULL);
    }

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