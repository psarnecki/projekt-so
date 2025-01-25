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
#define NUM_MEMBERS 3

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

pthread_mutex_t mutex_A = PTHREAD_MUTEX_INITIALIZER;            // Synchronizacja w komisji A
pthread_cond_t cond_A = PTHREAD_COND_INITIALIZER;               // Synchronizacja oczekiwania na rozpoczęcie losowania pytań oraz oceny odpowiedzi 
pthread_cond_t start_cond_A = PTHREAD_COND_INITIALIZER;         // Sygnał rozpoczęcia losowania pytań i oceny odpowiedzi w komisji A
pthread_mutex_t ready_count_mutex_A = PTHREAD_MUTEX_INITIALIZER; // Mutex do synchronizacji liczby członków komisji A, którzy zakończyli swoje zadanie

float member_scores_A[NUM_MEMBERS] = {0.0, 0.0, 0.0};
int start_A = 0;
int ready_count_A = 0;
int members_end_A = 0;

pthread_mutex_t mutex_B = PTHREAD_MUTEX_INITIALIZER;            // Synchronizacja w komisji B
pthread_cond_t cond_B = PTHREAD_COND_INITIALIZER;               // Synchronizacja oczekiwania na rozpoczęcie losowania pytań oraz oceny odpowiedzi 
pthread_cond_t start_cond_B = PTHREAD_COND_INITIALIZER;         // Sygnał rozpoczęcia losowania pytań i oceny odpowiedzi w komisji B
pthread_mutex_t ready_count_mutex_B = PTHREAD_MUTEX_INITIALIZER; // Mutex do synchronizacji liczby członków komisji B, którzy zakończyli swoje zadanie

float member_scores_B[NUM_MEMBERS] = {0.0, 0.0, 0.0};
int start_B = 0;
int ready_count_B = 0;  
int members_end_B = 0; 

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

float round_grade(float average) {
    if (average > 4.5) {
        return 5.0;
    } else if (average > 4.0) {
        return 4.5;
    } else if (average > 3.5) {
        return 4.0;
    } else if (average > 3.0) {
        return 3.5;
    } else if (average > 2.5) {
        return 3.0;
    } else {
        return 2.0;
    }
}

// Funkcja wątku przewodniczącego komisji A
void* chairman_A() {
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

            printf("\033[32mStudent %d przekazuje informacje, że ma już zaliczony egzamin praktyczny na ocenę: %.1f\033[0m\n", msg.pid, msg.grade_A);
        } else { // Wystawienie oceny za egzamin praktyczny

            // Synchronizacja - czekanie na pozostałych dwóch członków komisji
            pthread_mutex_lock(&mutex_A);
            start_A = 1;  // Ustawienie flagi informującej, że można rozpocząć losowanie
            pthread_cond_broadcast(&start_cond_A);  // Powiadomienie wszystkich pozostałych członków komisji, aby rozpoczęli pracę
            pthread_mutex_unlock(&mutex_A);

            sleep(rand() % 3 + 2); // Symulacja czasu na przygotowanie pytania

            pthread_mutex_lock(&mutex_A);
            while (ready_count_A < 2) {  // Oczekiwanie, aż obaj członkowie zakończą zadanie
                pthread_cond_wait(&cond_A, &mutex_A);
            }

            if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
                member_scores_A[2] = grades[rand() % (NUM_GRADES - 1)];  // Pula pozytywnych ocen
            } else {
                member_scores_A[2] = grades[5];  // 5% szans na ocenę 2.0
            }

            pthread_mutex_unlock(&mutex_A);

            if (member_scores_A[2] == 2.0) {  // Dla oceny 2.0 wystawionej przez przewodniczącego jest przypisywana ta ocena, aby zapewnić zdawalność na poziomie 5%
                msg.grade_A = member_scores_A[2];
            } else {
                float total_score = 0.0;
                for (int i = 0; i < 3; i++) {  // Suma ocen wszystkich członków komisji
                    total_score += member_scores_A[i];
                }
                msg.grade_A = round_grade(total_score / 3.0); // Oblicznie średniej ocen i jej zaokrąglenie 
            }

            ready_count_A = 0;  // Reset licznika gotowości dla członków komisji
            printf("\033[33mKomisja A wystawiła ocenę: %.1f dla PID: %d\033[0m\n", msg.grade_A, msg.pid);
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

        if (msg.grade_A >= 3.0) { // Licznik wystawionych pozytywnych ocen
            total_passed++;
        }

        graded_students++;

        if (graded_students == total_students) {
            printf("\033[31mKomisja A: Wszyscy studenci z kierunku podeszli do egzaminu praktycznego. Komisja kończy wystawianie ocen.\033[0m\n");

            // Celem tych dwóch linii jest obsłużenie skrajnego przypadku, gdy wszyscy studenci powtarzają egzamin i mają już ocenę z egzaminu praktycznego
            // Nie przeszkadzają one w normalnej pracy programu, ale są zbędne ponieważ projekt z założenia nie pozwala na dojście do tak skrajnego przypadku
            start_A = 1;
            pthread_cond_broadcast(&start_cond_A);

            pthread_mutex_lock(&mutex_A);
            members_end_A = 1;  // Ustawienie flagi zakończenia pracy komisji
            pthread_mutex_unlock(&mutex_A);

            shared_data->total_passed_practical = total_passed;
            shared_data->commission_A_done = 1;
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

// Funkcja wątku członka komisji A
void* commission_member_A(void* arg) {
    int member_id = *((int*)arg);

    while (1) {
        srand(time(NULL) ^ (unsigned int)pthread_self());

        pthread_mutex_lock(&mutex_A);
        if (members_end_A == 1) {
            pthread_mutex_unlock(&mutex_A);
            break;  // Zakończenie pracy wątku
        }
        pthread_mutex_unlock(&mutex_A);

        // Oczekiwanie na sygnał od przewodniczącego, by rozpocząć losowanie pytania
        pthread_mutex_lock(&mutex_A);
        while (!start_A) {
            pthread_cond_wait(&start_cond_A, &mutex_A);  
        }
        pthread_mutex_unlock(&mutex_A);

        sleep(rand() % 3 + 2); // Symulacja czasu na przygotowanie pytania

        member_scores_A[member_id] = grades[rand() % (NUM_GRADES)]; // Wystawienie oceny przez członka komisji

        pthread_mutex_lock(&ready_count_mutex_A);
        ready_count_A++; // Flaga informująca o zakończeniu zadania
        pthread_mutex_unlock(&ready_count_mutex_A);

        pthread_cond_signal(&cond_A); // Powiadomienie przewodniczącego o wystawieniu oceny
    }

    pthread_exit(NULL);
}

// Funkcja wątku przewodniczącego komisji B
void* chairman_B() {
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

        // Synchronizacja - czekanie na pozostałych dwóch członków komisji
        pthread_mutex_lock(&mutex_B);
        start_B = 1;  // Ustawienie flagi informującej, że można rozpocząć losowanie
        pthread_cond_broadcast(&start_cond_B);  // Powiadomienie wszystkich pozostałych członków komisji, aby rozpoczęli pracę
        pthread_mutex_unlock(&mutex_B);

        sleep(rand() % 3 + 2); // Symulacja czasu na przygotowanie pytania

        pthread_mutex_lock(&mutex_B);
        while (ready_count_B < 2) {  // Oczekiwanie, aż obaj członkowie zakończą zadanie
            pthread_cond_wait(&cond_B, &mutex_B);
        }

        if (rand() % 100 < 95) {  // 95% szans na ocenę pozytywną
            member_scores_B[2] = grades[rand() % (NUM_GRADES - 1)];  // Pula pozytywnych ocen
        } else {
            member_scores_B[2] = grades[5];  // 5% szans na ocenę 2.0
        }

        pthread_mutex_unlock(&mutex_B);

        if (member_scores_B[2] == 2.0) {  
            msg.grade_B = member_scores_B[2]; // Dla oceny 2.0 wystawionej przez przewodniczącego jest przypisywana ta ocena, aby zapewnić zdawalność na poziomie 5%
        } else {
            float total_score = 0.0;
            for (int i = 0; i < 3; i++) {  // Suma ocen wszystkich członków komisji
                total_score += member_scores_B[i];
            }
            msg.grade_B = round_grade(total_score / 3.0); // Oblicznie średniej ocen i jej zaokrąglenie 
        }

        ready_count_B = 0;  // Reset licznika gotowości dla członków komisji

        // Wysyłanie oceny z egzaminu B do dziekana
        msg.msg_type = COMMISSION_TO_DEAN;
        if (msgsnd(msg_id, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("Błąd wysyłania komunikatu!\n");
            exit(EXIT_FAILURE);
        } else {
            printf("\033[94mKomisja B wystawiła ocenę: %.1f dla PID: %d\033[0m\n", msg.grade_B, msg.pid);
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
            pthread_mutex_lock(&mutex_B);
            members_end_B = 1;  // Ustawienie flagi zakończenia pracy komisji
            pthread_mutex_unlock(&mutex_B);

            shared_data->commission_B_done = 1;
            printf("\033[31mKomisja B: Wszyscy studenci z kierunku podeszli do egzaminu teoretycznego. Komisja kończy wystawianie ocen.\033[0m\n");
            break;  // Zakończenie pracy komisji
        }
    }
    pthread_exit(NULL);
}

// Funkcja wątku członka komisji B
void* commission_member_B(void* arg) {
    int member_id = *((int*)arg);

    while (1) {
        srand(time(NULL) ^ (unsigned int)pthread_self());

        pthread_mutex_lock(&mutex_B);
        if (members_end_B == 1) {
            pthread_mutex_unlock(&mutex_B);
            break;  // Zakończenie pracy wątku
        }
        pthread_mutex_unlock(&mutex_B);

        // Oczekiwanie na sygnał od przewodniczącego, by rozpocząć losowanie pytania
        pthread_mutex_lock(&mutex_B);
        while (!start_B) {
            pthread_cond_wait(&start_cond_B, &mutex_B);
        }
        pthread_mutex_unlock(&mutex_B);

        sleep(rand() % 3 + 2); // Symulacja czasu na przygotowanie pytania

        member_scores_B[member_id] = grades[rand() % (NUM_GRADES)]; // Wystawienie oceny przez członka komisji

        pthread_mutex_lock(&ready_count_mutex_B);
        ready_count_B++; // Flaga informująca o zakończeniu zadania
        pthread_mutex_unlock(&ready_count_mutex_B);

        pthread_cond_signal(&cond_B); // Powiadomienie przewodniczącego o wystawieniu oceny
    }

    pthread_exit(NULL);
}

void create_commission_A() {
    pthread_t chairman, member_1, member_2;
    int member_1_id = 0, member_2_id = 1;

    // Tworzenie wątku przewodniczącego
    pthread_create(&chairman, NULL, chairman_A, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji z identyfikatorami
    pthread_create(&member_1, NULL, commission_member_A, &member_1_id);
    pthread_create(&member_2, NULL, commission_member_A, &member_2_id);

    // Oczekiwanie na zakończenie pracy pozostałych wątków
    pthread_join(chairman, NULL);
    pthread_join(member_1, NULL);
    pthread_join(member_2, NULL);
}

void create_commission_B() {
    pthread_t chairman, member_1, member_2;
    int member_1_id = 0, member_2_id = 1;

    // Tworzenie wątku przewodniczącego
    pthread_create(&chairman, NULL, chairman_B, NULL);

    // Tworzenie wątków dla dwóch pozostałych członków komisji
    pthread_create(&member_1, NULL, commission_member_B, &member_1_id);
    pthread_create(&member_2, NULL, commission_member_B, &member_2_id);

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

    sem_p(sem_id, SEM_ACTIVE_PROCESS); // Informacja, dla programu Dziekan o zakończonej pracy

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