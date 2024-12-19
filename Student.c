#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define MIN_STUDENTS 80
#define MAX_STUDENTS 160

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

    pthread_exit(NULL);
}

int main() {
    srand(time(NULL)); // Inicjalizacja generatora liczb pseudolosowych

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

    // Czekanie na zakończenie wszystkich wątków
    for (int i = 0; i < liczba_kierunkow; i++) {
        pthread_join(watki[i], NULL);
    }

    printf("\nSymulacja zakończona.\n");
    
    return 0;
}
