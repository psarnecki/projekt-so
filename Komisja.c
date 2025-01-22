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

#define NUM_GRADES 6

#define SEM_PRACTICAL_EXAM 2
#define SEM_COMMISSION_A 3
#define SEM_EXAM_A 4

#define SEM_THEORETICAL_EXAM 5
#define SEM_COMMISSION_B 6
#define SEM_EXAM_B 7

#define SEM_ACTIVE_PROCESS 8

#define STUDENT_TO_COMMISSION_A 1
#define STUDENT_TO_COMMISSION_B 2
#define COMMISSION_TO_DEAN 4
#define COMMISSION_PID 5

int shm_data_id, msg_id, msg_dean_id, sem_id, wait_status, exit_status;
float grades[] = {5.0, 4.5, 4.0, 3.5, 3.0, 2.0};
int commission_pids[2];

struct message {
    long msg_type;
    int pid;        
    float grade_A;      
    float grade_B;
    int practical_passed;  // Informacja o zaliczonej części praktycznej
};

typedef struct {
    int total_in_major;   // Liczba studentów na ogłoszonym kierunku
    int total_passed_practical;   // Liczba studentów z pozytywną oceną po egzaminie praktycznym
    int commission_A_done;   // Flaga informująca o końcu pracy komisji A
    int commission_B_done;   // Flaga informująca o końcu pracy komisji B
} Exam_data;

Exam_data *shared_data;

void sem_p(int sem_id, int sem_num);
void sem_v(int sem_id, int sem_num);
void handle_signal(int sig);
void cleanup();

// Funkcja wątku: symulacja pracy członka komisji
void* commission_member() {}

// Funkcja procesu przewodniczącego komisji
void* commission_A() {
    printf("Komisja A rozpoczęła przyjmować studentów!\n");

    int total_students, graded_students = 0, total_passed = 0;

    srand(time(NULL) ^ (unsigned int)pthread_self()); // Inicjalizacja liczb pseudolosowych o unikatowym seedzie

    sem_p(sem_id, SEM_COMMISSION_A); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

    total_students = shared_data->total_in_major; // Odczytanie ilości studentów na ogłoszonym kierunku

    while (1) {
        struct message msg;

        // Odbieranie PIDów procesów studentów
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_A, 0) == -1) {
            perror("Błąd odbierania komunikatu!\n");
            continue;
        }

        if (msg.practical_passed == 1) {  // Obsłużenie studentów mających zaliczoną część praktyczną egzaminu
            msg.grade_A = grades[rand() % (NUM_GRADES - 1)]; // Losowanie z puli pozytywnych ocen
            msg.msg_type = COMMISSION_TO_DEAN;

            printf("Student %d przekazuje informacje, że ma już zaliczony egzamin praktyczny na ocenę: %.1f\n", msg.pid, msg.grade_A);
        } else { // Wystawienie oceny za egzamin praktyczny
            sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań

            // Przypisanie losowej oceny do PIDu studenta
            if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
                msg.grade_A = grades[rand() % (NUM_GRADES - 1)];  // Pula pozytywnych ocen
            } else {
                msg.grade_A = grades[5];  // 5% szans na ocenę 2.0
            }

            printf("Komisja A wystawiła ocenę: %.1f dla PID: %d\n", msg.grade_A, msg.pid);
        }

        // Wysyłanie oceny z egzaminu A do dziekana
        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            exit(EXIT_FAILURE);
        }

        // Wysyłanie oceny z egzaminu A do studenta
        msg.msg_type = msg.pid;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            exit(EXIT_FAILURE);
        }

        if (msg.grade_A >= 3.0) { // Licznik pozytywnych ocen
            total_passed++;
        }

        graded_students++;

        if (graded_students == total_students) {
            printf("Komisja A: Wszyscy studenci z kierunku podeszli do egzaminu praktycznego. Komisja kończy wystawianie ocen.\n");

            shared_data->total_passed_practical = total_passed;
            shared_data->commission_A_done = 1;
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

void* commission_B() {
    printf("Komisja B rozpoczęła przyjmować studentów!\n");

    int total_passed_exam_A, graded_students = 0, is_commission_A_finished = 0;

    sleep(1); // Dodanie opóźnienia, aby zapewnić większe rozbieżności wartości czasu dla seedów między komisjami
    srand(time(NULL) ^ (unsigned int)pthread_self());
    
    while (1) {
        sem_p(sem_id, SEM_COMMISSION_B); // Rozpoczęcie zadawania pytań oraz oceny odpowiedzi

        struct message msg;

        // Odbieranie PIDów procesów studentów
        if (msgrcv(msg_id, &msg, sizeof(msg) - sizeof(long), STUDENT_TO_COMMISSION_B, 0) == -1) {
            perror("Błąd odbierania komunikatu!\n");
            continue;
        }      

        sleep(rand() % 3 + 2);  // Symulacja czasu do przygotowania pytań

        // Przypisanie losowej oceny do PIDu studenta
        if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
            msg.grade_B = grades[rand() % (NUM_GRADES - 1)];  // Pula pozytywnych ocen
        } else {
            msg.grade_B = grades[5];  // 5% szans na ocenę 2.0
        }

        // Wysyłanie oceny z egzaminu B do dziekana
        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            exit(EXIT_FAILURE);
        } else {
            printf("Komisja B wystawiła ocenę: %.1f dla PID: %d\n", msg.grade_B, msg.pid);
        }

        // Wysyłanie oceny z egzaminu B do studenta
        msg.msg_type = msg.pid;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            exit(EXIT_FAILURE);
        }

        graded_students++; // Liczba wystawionych ocen z egzaminu teoretycznego

        total_passed_exam_A = shared_data->total_passed_practical;
        is_commission_A_finished = shared_data->commission_A_done;

        if (total_passed_exam_A == graded_students && is_commission_A_finished == 1) { // Sprawdzenie czy komisja A zakończyła pracę i czy obsłużono każdego studenta
            shared_data->commission_B_done = 1;
            printf("Komisja B: Wszyscy studenci z kierunku podeszli do egzaminu teoretycznego. Komisja kończy wystawianie ocen.\n");
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

void create_commission_A() {
    pthread_t chairman, member_1, member_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&chairman, NULL, commission_A, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&member_1, NULL, commission_member, NULL);
    pthread_create(&member_2, NULL, commission_member, NULL);

    // Oczekiwanie na zakończenie pracy pozostałych wątków
    pthread_join(chairman, NULL);
    pthread_join(member_1, NULL);
    pthread_join(member_2, NULL);
}

void create_commission_B() {
    pthread_t chairman, member_1, member_2;

    // Tworzenie wątku przewodniczącego
    pthread_create(&chairman, NULL, commission_B, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&member_1, NULL, commission_member, NULL);
    pthread_create(&member_2, NULL, commission_member, NULL);

    // Oczekiwanie na zakończenie pracy pozostałych wątków
    pthread_join(chairman, NULL);
    pthread_join(member_1, NULL);
    pthread_join(member_2, NULL);
}

int main() {
    key_t key = ftok(".", 'S');
    key_t key_info = ftok(".", 'I');
    key_t key_msg = ftok(".", 'M');
    key_t key_dean_msg = ftok(".", 'D');
    if (key == -1 || key_info == -1 || key_msg == -1 || key_dean_msg == -1) {
        perror("Błąd tworzenia klucza!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    shm_data_id = shmget(key_info, sizeof(Exam_data), IPC_CREAT | 0600);
    if (shm_data_id == -1) {
        perror("Błąd tworzenia segmentu pamięci dzielonej!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    shared_data = (Exam_data *)shmat(shm_data_id, NULL, 0);
    if (shared_data == (Exam_data *)(-1)) {
        perror("Błąd przyłączenia pamięci dzielonej!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    msg_dean_id = msgget(key_dean_msg, 0600 | IPC_CREAT);
    msg_id = msgget(key_msg, 0600 | IPC_CREAT);
    if (msg_id == -1 || msg_dean_id == -1) {
        perror("Błąd tworzenia kolejki komunikatów!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    sem_id = semget(key, 9, IPC_CREAT | 0600);
    if (sem_id == -1) {
        perror("Błąd tworzenia semaforów!\n");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Przypisywanie wartości semaforów - Komisja A
    semctl(sem_id, SEM_PRACTICAL_EXAM, SETVAL, 3);
    semctl(sem_id, SEM_COMMISSION_A, SETVAL, 0); 
    semctl(sem_id, SEM_EXAM_A, SETVAL, 1);

    // Przypisywanie wartości semaforów - Komisja B
    semctl(sem_id, SEM_THEORETICAL_EXAM, SETVAL, 3);
    semctl(sem_id, SEM_COMMISSION_B, SETVAL, 0); 
    semctl(sem_id, SEM_EXAM_B, SETVAL, 1);

    signal(SIGUSR1, handle_signal); // Obsługa sygnału SIGUSR1

    struct message msg;
    msg.msg_type = COMMISSION_PID;

    // Tworzenie procesu komisji A
    if (fork() == 0) {
        commission_pids[0] = getpid();

        // Wysłanie PIDu procesu komisji A do dziekana
        msg.pid = commission_pids[0];
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }

        create_commission_A();  // Uruchomienie funkcji tworzącej komisje A
        exit(0);
    }

    // Tworzenie procesu komisji B
    if (fork() == 0) {
        commission_pids[1] = getpid();

        // Wysłanie PIDu procesu komisji B do dziekana
        msg.pid = commission_pids[1];
        if (msgsnd(msg_dean_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }

        create_commission_B();  // Uruchomienie funkcji tworzącej komisje B
        exit(0);
    } 

    // Czekanie na zakończenie wszystkich procesów potomnych komisji
    while ((wait_status = wait(&exit_status)) > 0) {
        if (wait_status == -1) {
            perror("Błąd oczekiwania na zakończenie procesu potomnego!\n");
            cleanup();
            exit(EXIT_FAILURE);
        }
    }

    sem_p(sem_id, SEM_ACTIVE_PROCESS);

    return 0;
}

void sem_p(int sem_id, int sem_num) {
    int change_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = -1;
    bufor_sem.sem_flg = 0;
    change_sem=semop(sem_id, &bufor_sem, 1);
    if (change_sem == -1){
        if(errno == EINTR){
        sem_p(sem_id, sem_num);
        } else {
        perror("Błąd zamykania semafora.\n");
        exit(EXIT_FAILURE);
        }
    }
}

void sem_v(int sem_id, int sem_num) {
	int change_sem;
    struct sembuf bufor_sem;
    bufor_sem.sem_num = sem_num;
    bufor_sem.sem_op = 1;
    bufor_sem.sem_flg = 0;
    change_sem=semop(sem_id, &bufor_sem, 1);
    if (change_sem == -1){
        perror("Błąd otwierania semafora.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup() {
    if (shared_data != NULL && shmdt(shared_data) == -1) {
        perror("Błąd odłączania pamięci dzielonej shared_data!\n");
    }
    if (shmctl(shm_data_id, IPC_RMID, NULL) == -1) {
        perror("Błąd usuwania segmentu pamięci dzielonej shm_data_id!\n");
    }
    if (semctl(sem_id, 0, IPC_RMID) == -1) {
        perror("Błąd usuwania semaforów sem_id!\n");
    }
    if(msgctl(msg_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_id!\n");
    }
    if(msgctl(msg_dean_id, IPC_RMID, NULL) == -1){
        perror("Błąd usuwania kolejki komunikatów msg_dean_id!\n");
    }
}

void handle_signal(int sig) {
    exit(0);
}