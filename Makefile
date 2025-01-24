all: student dziekan komisja

student: Student.c
	gcc -o student Student.c

dziekan: Dziekan.c
	gcc -o dziekan Dziekan.c

komisja: Komisja.c
	gcc -o komisja Komisja.c -lpthread

clean:
	rm -f student dziekan komisja