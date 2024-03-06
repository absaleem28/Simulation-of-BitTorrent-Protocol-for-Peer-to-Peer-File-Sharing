build:
	mpicc -o bittorent_sim bittorent_sim.c -pthread -Wall

clean:
	rm -rf bittorent_sim
