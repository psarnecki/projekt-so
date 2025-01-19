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
#include <signal.h> // do obsługi sygnałów
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
    long msg_type;  // Typ komunikatu
    int pid;        // PID studenta
    float ocena_A;      // Ocena (dla odpowiedzi)
    float ocena_B;
    int zaliczenie;
};

typedef struct {
    int ile_kierunek;   // Liczba studentów na ogłoszonym kierunku
    int ile_studentow;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int komisja_A_koniec;   // Flaga informująca o końcu pracy komisji A
    int komisja_B_koniec;
} Student_info;

Student_info *shared_info;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void handle_signal(int sig);
void cleanup();

// Funkcja wątku: symulacja pracy członka komisji
void* czlonek_komisji() {}

// Funkcja procesu komisji
void* komisja_A() {
    printf("Komisja A rozpoczęła przyjmować studentów!\n");

    int ile_studentow, ile_ocen = 0, ile_zdane = 0;

    srand(time(NULL) ^ (unsigned int)pthread_self());

    while (1) {
        sem_p(sem_id, SEM_ILE_STUDENTOW_2);
        ile_studentow = shared_info->ile_kierunek;
        sem_v(sem_id, SEM_ILE_STUDENTOW_1);

        //printf("Liczba studentów na kierunku: %d\n", ile_studentow);

        sem_p(sem_id, SEM_KOMISJA_A); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDu studenta
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_A, 0) == -1) {
            perror("msgrcv Komisja A");
            continue;
        } else {
            //printf("Odebrałem pid studetna %d\n", msg.pid);
        }

        if (msg.zaliczenie == 1) {
            msg.ocena_A = oceny[rand() % (LICZBA_OCEN - 1)];
            msg.msg_type = COMMISSION_TO_DEAN;

            printf("Student %d przekazuje informacje, że ma już zaliczony egzamin praktyczny na ocenę: %.1f\n", msg.pid, msg.ocena_A);
        } else {
            ///printf("Komisja A: Przygotowuję pytania.\n");
            sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań
            ///printf("Komisja A: Pytania gotowe.\n");

            // Tutaj ewentualnie można dodać semafor który będzie odbierał informacje o gotowych pytaniach 

            // Przypisanie losowej oceny do PIDu studenta
            if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
                msg.ocena_A = oceny[rand() % (LICZBA_OCEN - 1)];  // Oceny 5.0, 4.5, 4.0, 3.5, 3.0
            } else {
                msg.ocena_A = oceny[5];  // 5% szans na ocenę 2.0
            }

            printf("Komisja A wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_A, msg.pid);
        }

        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        }

        msg.msg_type = msg.pid;

        // Wysyłanie oceny za odpowiedź do studenta
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        } else {
            //printf("Komisja wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_A, msg.pid);
        }

        if (msg.ocena_A >= 3.0) {
            ile_zdane++;
            //printf("Aktualna liczba studentów z pozytywną oceną: %d\n", ile_zdane);
        }

        ile_ocen++;
        //printf("Aktualna liczba studentów z oceną A: %d\n", ile_ocen);

        if (ile_ocen == ile_studentow) {
            //printf("!!! Liczba studentów z pozytywną oceną: %d !!!\n", ile_zdane);
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

    sleep(1); // Dodanie opóźnienia, aby zapewnić różne wartości czasu dla seedów
    srand(time(NULL) ^ (unsigned int)pthread_self());
    
    while (1) {
        sem_p(sem_id, SEM_KOMISJA_B); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDu studenta
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_B, 0) == -1) {
            perror("msgrcv Komisja B");
            continue;
        } else {
            //printf("Odebrałem pid studetna %d\n", msg.pid);
        }        

        ///printf("Komisja B: Przygotowuję pytania.\n");
        sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań
        ///printf("Komisja B: Pytania gotowe.\n");

        // Tutaj ewentualnie można dodać semafor który będzie odbierał informacje o gotowych pytaniach 

        // Przypisanie losowej oceny do PIDu studenta
        if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
            msg.ocena_B = oceny[rand() % (LICZBA_OCEN - 1)];  // Oceny 5.0, 4.5, 4.0, 3.5, 3.0
        } else {
            msg.ocena_B = oceny[5];  // 5% szans na ocenę 2.0
        }

        msg.msg_type = COMMISSION_TO_DEAN;

        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        } else {
            printf("Komisja B wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_B, msg.pid);
        }

        msg.msg_type = msg.pid;

        // Wysyłanie oceny za odpowiedź do studenta
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            exit(EXIT_FAILURE);
        } else {
            //printf("Komisja B wystawiła ocenę: %.1f dla PID: %d\n", msg.ocena_B, msg.pid);
        }

        ile_ocen++;
        //printf("Aktualna liczba studentów z oceną B: %d\n", ile_ocen);

        ile_studentow = shared_info->ile_studentow;
        czy_koniec = shared_info->komisja_A_koniec;

        if (ile_studentow == ile_ocen && czy_koniec == 1) {
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

    pthread_join(przewodniczacy, NULL);
    pthread_join(czlonek_1, NULL);
    pthread_join(czlonek_2, NULL);

    //printf("Komisja A zakończyła swoją pracę.\n");
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

    //printf("Komisja B zakończyła swoją pracę.\n");
}

int main() {
    //srand(time(NULL));  // Inicjalizacja generatora liczb losowych

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

    // Tworzenie semafora
    sem_id = semget(key, 10, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Komisja A
    semctl(sem_id, SEM_EGZAMIN_PRAKTYCZNY, SETVAL, 3);
    semctl(sem_id, SEM_KOMISJA_A, SETVAL, 0); 
    semctl(sem_id, SEM_EGZAMIN_A, SETVAL, 1);
    semctl(sem_id, SEM_ILE_STUDENTOW_1, SETVAL, 1);
    semctl(sem_id, SEM_ILE_STUDENTOW_2, SETVAL, 0);

    //Komisja B
    semctl(sem_id, SEM_EGZAMIN_TEORETYCZNY, SETVAL, 3);
    semctl(sem_id, SEM_KOMISJA_B, SETVAL, 0); 
    semctl(sem_id, SEM_EGZAMIN_B, SETVAL, 1);


    signal(SIGUSR1, handle_signal);

    struct message msg;
    msg.msg_type = COMMISSION_PID;

    // Tworzenie procesu komisji A
    if (fork() == 0) {
        commission_pids[0] = getpid();
        //printf("Pid procesu %d to: %d.\n", 1, commission_pids[0]);
        msg.pid = commission_pids[0]; // PID studenta
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            //printf("Wysłano pid %d.\n", msg.pid);
        }
        stworz_komisja_A();  // Uruchomienie funkcji dla komisji A
        exit(0);  // Zakończenie procesu komisji A
    }

    // Tworzenie procesu komisji B
    if (fork() == 0) {
        commission_pids[1] = getpid();
        //printf("Pid procesu %d to: %d.\n", 2, commission_pids[1]);
        msg.pid = commission_pids[1]; // PID studenta
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            cleanup();
            exit(EXIT_FAILURE);
        } else {
            //printf("Wysłano pid %d.\n", msg.pid);
        }
        stworz_komisja_B();  // Uruchomienie funkcji dla komisji B
        exit(0);  // Zakończenie procesu komisji B
    } 

    // Proces główny czeka na zakończenie wszystkich procesów
        while ((w = wait(&x)) > 0) {
        if (w == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!");
            cleanup();
            exit(EXIT_FAILURE);
        }
        //printf("Zakonczyl sie proces potomny o PID=%d i statusie = %d\n", w, x);
    }

    //printf("Komisje zakończyły wystawianie ocen!.\n");

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
        perror("(Komisja) Nie mogłem zamknąć semafora.\n");
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

void cleanup() {
    //printf("Została wywołana funkcja czyszcząca!\n");
    if (shared_info != NULL && shmdt(shared_info) == -1) {
        perror("Błąd odłączania pamięci dzielonej Student-Komisja!");
    }
    if (shmctl(shm_info_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej Student-Komisja!");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów Student-Komisja A!");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów!");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów!");
    }
}

void handle_signal(int sig) {
    //printf("Otrzymano sygnał %d. Zabijam procesy komisji.\n", sig);
    exit(0);
}