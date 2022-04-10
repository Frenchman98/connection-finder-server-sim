

.DEFAULT_GOAL := all

clean:
	rm -f *.o clientA clientB central serverP serverS serverT


all: clients servers


# Servers
servers: central.o serverP.o serverS.o serverT.o
	gcc -o central -g central.o
	gcc -o serverP -g serverP.o
	gcc -o serverS -g serverS.o
	gcc -o serverT -g serverT.o

central.o: central.c
	gcc -g -c -Wall central.c

serverP.o: serverP.c
	gcc -g -c -Wall serverP.c

serverS.o: serverS.c
	gcc -g -c -Wall serverS.c

serverT.o: serverT.c
	gcc -g -c -Wall serverT.c


#  Clients
clients: clientA.o clientB.o
	gcc -o clientA -g clientA.o
	gcc -o clientB -g clientB.o

clientA.o: clientA.c
	gcc -g -c -Wall clientA.c

clientB.o: clientB.c
	gcc -g -c -Wall clientB.c
	