#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h> 
#include <sys/sem.h> 
#include <pthread.h> 
#include <sys/msg.h> 
#include <signal.h> 
#include <errno.h>

#define LICZBA_OCEN 6

#define SEM_EGZAMIN_PRAKTYCZNY 2
#define SEM_KOMISJA_A 3
#define SEM_EGZAMIN_A 4
#define SEM_ILE_STUDENTOW_1 5
#define SEM_ILE_STUDENTOW_2 6

#define SEM_EGZAMIN_TEORETYCZNY 7
#define SEM_KOMISJA_B 8
#define SEM_EGZAMIN_B 9

#define STUDENT_TO_COMMISSION_A 1
#define STUDENT_TO_COMMISSION_B 2
#define COMMISSION_TO_DEAN 4
#define COMMISSION_PID 5

int shm_info_id, msg_id, msg_dean_id, sem_id, w, x;
float oceny[] = {5.0, 4.5, 4.0, 3.5, 3.0, 2.0};
int commission_pids[2];

struct message {
    long msg_type;  
    int pid;        
    float ocena_A;      
    float ocena_B;
    int zaliczenie;  // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
    int komisja_B_koniec;   // Flaga informująca o końcu pracy komisji B
} Student_info;

Student_info *shared_info;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void handle_signal(int sig);
void cleanup();

// Funkcja wątku: symulacja pracy członka komisji
void* czlonek_komisji() {}

// Funkcja procesu przewodniczącego komisji
void* komisja_A() {
    printf("Komisja A rozpoczęła przyjmować studentów!\n");

    int ile_studentow, ile_ocen = 0, ile_zdane = 0;

    srand(time(NULL) ^ (unsigned int)pthread_self()); // Inicjalizacja liczb pseudolosowych o unikatowym seedzie

    while (1) {
        // Odczytanie ilości studentów na ogłoszonym kierunku
        sem_p(sem_id, SEM_ILE_STUDENTOW_2);
        ile_studentow = shared_info->ile_kierunek;
        sem_v(sem_id, SEM_ILE_STUDENTOW_1);

        sem_p(sem_id, SEM_KOMISJA_A); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDów procesów studentów
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_A, 0) == -1) {
            perror("msgrcv Komisja A");
            continue;
        }

        if (msg.zaliczenie == 1) {  // Obsłużenie studentów mających zaliczoną część praktyczną egzaminu
            msg.ocena_A = oceny[rand() % (LICZBA_OCEN - 1)]; // Losowanie z puli pozytywnych ocen
            msg.msg_type = COMMISSION_TO_DEAN;

            printf("Student %d przekazuje informacje, że ma już zaliczony egzamin praktyczny na ocenę: %.1f\n", msg.pid, msg.ocena_A);
        } else { // Wystawienie oceny za egzamin praktyczny
            sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań

            // Przypisanie losowej oceny do PIDu studenta
            if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
                msg.ocena_A = oceny[rand() % (LICZBA_OCEN - 1)];  // Pula pozytywnych ocen
            } else {
                msg.ocena_A = oceny[5];  // 5% szans na ocenę 2.0
            }

            printf("Komisja A wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_A, msg.pid);
        }

        // Wysłanie oceny z egzaminu A do dziekana
        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }

        // Wysyłanie oceny z egzaminu A do studenta
        msg.msg_type = msg.pid;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }

        if (msg.ocena_A >= 3.0) { // Licznik pozytywnych ocen
            ile_zdane++;
        }

        ile_ocen++;

        if (ile_ocen == ile_studentow) {
            printf("Komisja A: Wszyscy studenci z kierunku podeszli do egzaminu praktycznego. Komisja kończy wystawianie ocen.\n");

            shared_info->ile_studentow = ile_zdane;
            shared_info->komisja_A_koniec = 1;
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

void* komisja_B() {
    printf("Komisja B rozpoczęła przyjmować studentów!\n");

    int ile_studentow, ile_ocen = 0, czy_koniec = 0;

    sleep(1); // Dodanie opóźnienia, aby zapewnić większe rozbieżności wartości czasu dla seedów między komisjami
    srand(time(NULL) ^ (unsigned int)pthread_self());
    
    while (1) {
        sem_p(sem_id, SEM_KOMISJA_B); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDów procesów studentów
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_B, 0) == -1) {
            perror("msgrcv Komisja B");
            continue;
        }      

        sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań

        // Przypisanie losowej oceny do PIDu studenta
        if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
            msg.ocena_B = oceny[rand() % (LICZBA_OCEN - 1)];  // Pula pozytywnych ocen
        } else {
            msg.ocena_B = oceny[5];  // 5% szans na ocenę 2.0
        }

        // Wysłanie oceny z egzaminu B do dziekana
        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        } else {
            printf("Komisja B wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_B, msg.pid);
        }

        // Wysyłanie oceny z egzaminu B do studenta
        msg.msg_type = msg.pid;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }

        ile_ocen++; // Liczba wystawionych ocen z egzaminu teoretycznego

        ile_studentow = shared_info->ile_studentow;
        czy_koniec = shared_info->komisja_A_koniec;

        if (ile_studentow == ile_ocen && czy_koniec == 1) { // Sprawdzenie czy komisja A zakończyła pracę i czy obsłużono każdego studenta
            shared_info->komisja_B_koniec = 1;
            printf("Komisja B: Wszyscy studenci z kierunku podeszli do egzaminu teoretycznego. Komisja kończy wystawianie ocen.\n");
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

void stworz_komisja_A() {
    pthread_t przewodniczacy, czlonek_1, czlonek_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&przewodniczacy, NULL, komisja_A, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&czlonek_1, NULL, czlonek_komisji, NULL);
    pthread_create(&czlonek_2, NULL, czlonek_komisji, NULL);

    // Oczekiwanie na zakończenie pracy pozostałych wątków
    pthread_join(przewodniczacy, NULL);
    pthread_join(czlonek_1, NULL);
    pthread_join(czlonek_2, NULL);
}

void stworz_komisja_B() {
    pthread_t przewodniczacy, czlonek_1, czlonek_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&przewodniczacy, NULL, komisja_B, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&czlonek_1, NULL, czlonek_komisji, NULL);
    pthread_create(&czlonek_2, NULL, czlonek_komisji, NULL);

    // Oczekiwanie na zakończenie pracy pozostałych wątków
    pthread_join(przewodniczacy, NULL);
    pthread_join(czlonek_1, NULL);
    pthread_join(czlonek_2, NULL);
}

int main() {
    key_t key = ftok(".", 'S');
    key_t key_info = ftok(".", 'I');
    key_t key_msg = ftok(".", 'M');
    key_t key_dean_msg = ftok(".", 'D');
    if (key == -1 || key_info == -1 || key_msg == -1 || key_dean_msg == -1) {
        perror("Błąd tworzenia klucza!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_info_id = shmget(key_info, sizeof(Student_info), IPC_CREAT | 0666);
    if (shm_info_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    shared_info = (Student_info *)shmat(shm_info_id, NULL, 0);
    if (shared_info == (Student_info *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msg_dean_id = msgget(key_dean_msg, 0666 | IPC_CREAT);
    msg_id = msgget(key_msg, 0666 | IPC_CREAT);
    if (msg_id == -1 || msg_dean_id == -1) {
        perror("Błąd tworzenia kolejki komunikatów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 10, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Przypisywanie wartości semaforów Komisja A
    semctl(sem_id, SEM_EGZAMIN_PRAKTYCZNY, SETVAL, 3);
    semctl(sem_id, SEM_KOMISJA_A, SETVAL, 0); 
    semctl(sem_id, SEM_EGZAMIN_A, SETVAL, 1);
    semctl(sem_id, SEM_ILE_STUDENTOW_1, SETVAL, 1);
    semctl(sem_id, SEM_ILE_STUDENTOW_2, SETVAL, 0);

    // Przypisywanie wartości semaforów Komisja B
    semctl(sem_id, SEM_EGZAMIN_TEORETYCZNY, SETVAL, 3);
    semctl(sem_id, SEM_KOMISJA_B, SETVAL, 0); 
    semctl(sem_id, SEM_EGZAMIN_B, SETVAL, 1);


    signal(SIGUSR1, handle_signal);

    struct message msg;
    msg.msg_type = COMMISSION_PID;

    // Tworzenie procesu komisji A
    if (fork() == 0) {
        commission_pids[0] = getpid();

        // Wysłanie PIDu procesu komisji A do dziekana
        msg.pid = commission_pids[0];
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        }

        stworz_komisja_A();  // Uruchomienie funkcji tworzącej komisje A
        exit(0);
    }

    // Tworzenie procesu komisji B
    if (fork() == 0) {
        commission_pids[1] = getpid();

        // Wysłanie PIDu procesu komisji B do dziekana
        msg.pid = commission_pids[1];
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        }

        stworz_komisja_B();  // Uruchomienie funkcji tworzącej komisje B
        exit(0);
    } 

    // Czekanie na zakończenie wszystkich procesów potomnych komisji
    while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!");
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

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
        perror("Nie mogłem zamknąć semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

void sem_v(int sem_id, int sem_num) {
	int zmien_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0;
    zmien_sem=semop(sem_id, &bufor_sem, 1);
    if (zmien_sem == -1){
        perror("Nie mogłem otworzyć semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    if (shared_info != NULL && shmdt(shared_info) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_info!");
    }
    if (shmctl(shm_info_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_info_id!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów sem_id!");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_id!");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_dean_id!");
    }
}

void handle_signal(int sig) {
    exit(0);
}