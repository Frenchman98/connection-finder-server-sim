## Server Simulation for Finding Connections in a Database

A set of scripts written in C that simulate client-server and server-server interactions over both TCP and UDP on the localhost.

Specifically, there are 2 client programs that send their names to a central server using TCP, and the central server communicates with three designated backservers over UDP to find the shortest connection between the names given by both clients. At the end, the central server sends the result of the calculations to the clients for display in their terminals as well.

### To Run

To compile the code, simply open a terminal in this directory and enter `make`.

The programs should then be launched in the following order:

- Central server
- Backservers (order doesn't matter)
- Clients (order doesn't matter)

Then, enter a name found in the `scores.txt` file in Client A, then a different name also found in `scores.txt` into Client B, and the servers will start processing and give the shortest path between people if one exists.

Developed and tested on a Ubuntu 16.04 distribution.
