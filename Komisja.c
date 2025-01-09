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
#include <sys/msg.h> // Do obsługi kolejki komunikatów
#include <errno.h>

#define SEM_EGZAMIN_PRAKTYCZNY 0
#define SEM_KOMISJA 1
#define SEM_EGZAMIN 2
#define SEM_ILE_STUDENTOW_1 3
#define SEM_ILE_STUDENTOW_2 4
#define STUDENT_TO_COMMISSION 1

int sem_komisja_id, shm_komisja_id, msgid, w, x;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);

struct message {
    long msg_type;  // Typ komunikatu
    int pid;        // PID studenta
    int ocena;      // Ocena (dla odpowiedzi)
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
} Ile_studentow_info;

Ile_studentow_info *shared_info;

// Funkcja wątku: symulacja pracy członka komisji
void* czlonek_komisji() {}

// Funkcja procesu komisji
void* komisja_A() {
    printf("Komisja A: Wątek przewodniczącego uruchomiony.\n");

    int ile_studentow, ile_ocen = 0;
    
    while (1) {
        sem_p(sem_komisja_id, SEM_ILE_STUDENTOW_2);
        ile_studentow = shared_info->ile_kierunek;
        sem_v(sem_komisja_id, SEM_ILE_STUDENTOW_1);

        printf("Liczba studentów na kierunku: %d\n", ile_studentow);

        sem_p(sem_komisja_id, SEM_KOMISJA); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDu studenta
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION, 0) == -1) {
            perror("msgrcv");
            continue;
        } else {
            //printf("Odebrałem pid studetna %d\n", msg.pid);
        }        

        printf("Komisja A: Przygotowuję pytania.\n");
        //sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań
        printf("Komisja A: Pytania gotowe.\n");

        // Tutaj ewentualnie można dodać semafor który będzie odbierał informacje o gotowych pytaniach 

        // Przypisanie losowej oceny do PIDu studenta
        msg.ocena = rand() % 5 + 1;
        msg.msg_type = msg.pid;

        // Wysyłanie oceny za odpowiedź do studenta
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
        } else {
            printf("Komisja wystawiła ocenę: %d dla PID: %d\n", msg.ocena, msg.pid);
        }

        ile_ocen++;
        printf("Aktualna liczba studentów z oceną: %d\n", ile_ocen);

        if (ile_ocen == ile_studentow) {
            printf("Komisja A: Wszyscy studenci z kierunku podeszli do egzaminu praktycznego. Komisja kończy pracę.\n");
            break;  // Zakończenie pracy komisji
        }
    }

    pthread_exit(NULL);
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
    key_t key_msg_A = ftok(".", 'A'); // Tworzenie klucza

    if (key_komisja == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(-1);
    }

    shm_komisja_id = shmget(key_komisja, sizeof(Ile_studentow_info), IPC_CREAT | 0666);
    if (shm_komisja_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(-1);
    }
    
    shared_info = (Ile_studentow_info *)shmat(shm_komisja_id, NULL, 0);
    if (shared_info == (Ile_studentow_info *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(-1);
    }

    msgid = msgget(key_msg_A, 0666 | IPC_CREAT);


    // Tworzenie semafora
    sem_komisja_id = semget(key_komisja, 5, IPC_CREAT | 0666);
    if (sem_komisja_id == -1) {
        perror("Błąd tworzenia semaforów!");
        exit(EXIT_FAILURE);
    }

    semctl(sem_komisja_id, SEM_KOMISJA, SETVAL, 0); 
    semctl(sem_komisja_id, SEM_EGZAMIN, SETVAL, 1);
    semctl(sem_komisja_id, SEM_EGZAMIN_PRAKTYCZNY, SETVAL, 3);
    semctl(sem_komisja_id, SEM_ILE_STUDENTOW_1, SETVAL, 1);
    semctl(sem_komisja_id, SEM_ILE_STUDENTOW_2, SETVAL, 0);

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
        printf("(Komisja) Nie mogłem zamknąć semafora.\n");
        exit(EXIT_FAILURE);
        }
    } else {
        //printf("(Komisja) Zamknięto semafor!\n");
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
    } else {
        //printf("(Komisja) Otwarto semafor!\n");
    }
}

void cleanup() {
    printf("Została wywołana funkcja czyszcząca!\n");
    if (shared_info != NULL && shmdt(shared_info) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Komisja!");
    }
    if (shmctl(shm_komisja_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Komisja!");
    }
    if (semctl(sem_komisja_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Komisja!");
    }
    if(msgctl(msgid, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów!");
    }
}