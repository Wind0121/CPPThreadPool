main: main.o
	g++ -std=c++11 -Wall -o main main.o -lpthread

main.o: main.cc
	g++ -std=c++11 -Wall -g -c main.cc

clean:
	rm main *.o

test: main
	./main
