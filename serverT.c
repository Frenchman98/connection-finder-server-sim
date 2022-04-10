
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/queue.h>

// --- Constants -----------------------

#define localhost          "127.0.0.1"
#define MAX_LEN            100000
#define MSG_FORMAT         "Length: %d %s"
#define UDP_LIM            65506

#define xxx                372
#define server_t_udp_port  21000 + xxx
#define server_s_udp_port  22000 + xxx
#define server_p_udp_port  23000 + xxx
#define central_udp_port   24000 + xxx
#define central_tcp_a_port 25000 + xxx
#define central_tcp_b_port 26000 + xxx

// -------------------------------------

// Used in queue
struct ch {
    char *val;
    TAILQ_ENTRY(ch) eentries;
    TAILQ_ENTRY(ch) qentries;
};

char* receive(int fd, struct sockaddr_in *addrp, socklen_t *len_addrp) {
    char buf[UDP_LIM], *tmp = NULL, *out = calloc(3, sizeof(char));
    int tot = 0, header_len = 0, num_sections = 0, i = 0;

    // Receive chunks
    do {
        memset(buf, 0, UDP_LIM);
        int r = recvfrom(fd, buf, UDP_LIM, MSG_WAITALL, (struct sockaddr *) addrp, len_addrp);
        if (r == -1) {
            fprintf(stderr, "Problem occurred while getting required data from socket.\n");
            free(out);
            return NULL;
        }
        // printf("Received chunk: %s\n", buf);

        if (!tot) {
            tmp = strtok(buf, " "); // The header
            header_len += strlen(tmp) + 1;
            tmp = strtok(NULL, " "); // The content size
            header_len += strlen(tmp) + 1;

            char *endptr;
            tot = (int) strtol(tmp, &endptr, 10); // The size of the message as integer
            tot += header_len;

            if (!tot) {
                fprintf(stderr, "Error: A message of length 0 was received.\n");
                free(out);
                return NULL;
            }

            num_sections = 1 + tot / UDP_LIM;

            // Get rest of contents and make a copy
            tmp = strtok(NULL, " ");
            tmp = strcpy(calloc(strlen(tmp) + 5, sizeof(char)), tmp);

            memset(buf, 0, UDP_LIM);
            strncpy(buf, tmp, UDP_LIM);
            free(tmp);
        }
        
        // add buffer to out
        tmp = strcpy(calloc(strlen(out) + strlen(buf) + 5, sizeof(char)), out);
        free(out);
        out = strcat(tmp, buf);

        i++;
    } while (i < num_sections);

    return out;
}


void send_to(char* msg, int fd, struct sockaddr_in *addrp, socklen_t len_addr) {

    // Format the message
    char buf[UDP_LIM], *out = calloc(strlen(msg) + 20, sizeof(char));
    snprintf(out, (size_t) strlen(msg) + 20, MSG_FORMAT, (int) strlen(msg), msg);

    // Count nuber of chunks to send
    int num = 1 + (int) strlen(out) / UDP_LIM, i;

    // Break message into the chunks and send in order
    for (i = 0; i < num; i++) {
        memset(buf, 0, UDP_LIM);
        strncpy(buf, out + i*UDP_LIM, UDP_LIM);
        int r = sendto(fd, (const char *) buf, strlen(buf), MSG_CONFIRM, (const struct sockaddr *) addrp, len_addr);
        if (r == -1) {
            fprintf(stderr, "There was a problem sending information through a socket.\n");
            break;
        }
        // printf("Sent chunk: %s\n", buf);
    };

    free(out);
}


int main(int argc, char *argv[]) {

    if (argc != 1) {
        fprintf(stderr, "Invalid number of arguments used (0 expected).\n");
        return 1;
    }

    // Make the socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (!fd) {
        fprintf(stderr, "The socket wasn't able to open properly.\n");
        return 1;
    }

    // Make the address object
    struct sockaddr_in addr, central_addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(localhost);
    addr.sin_port = htons(server_t_udp_port);
    memset(&central_addr, 0, sizeof(central_addr));

    socklen_t len_addr = (socklen_t) sizeof(addr),
              len_central_addr = (socklen_t) sizeof(central_addr);

    // Bind the socket to the address
    if (bind(fd, (struct sockaddr *) &addr, len_addr)) {
        fprintf(stderr, "Unable to bind to the required port.\n");
        return 1;
    }

    // Load edges from file
    FILE* edge_f = fopen("./edgelist.txt", "r");

    size_t len;
    char *edges = 0;
    getdelim(&edges, &len, '\0', edge_f);

    fclose(edge_f);

    char msg[strlen(edges)*2], seen[strlen(edges)*2]; // Not taking any risks with overflowing

    // Create linked list of edges
    char *str = strtok(edges, "\n");
    TAILQ_HEAD(edgelist, ch) ehead = TAILQ_HEAD_INITIALIZER(ehead);
    TAILQ_INIT(&ehead);

    do {
        struct ch *cur = (struct ch *) malloc(sizeof(struct ch));
        cur->val = malloc(sizeof(str));
        strcpy(cur->val, str);
        TAILQ_INSERT_TAIL(&ehead, cur, eentries);

        str = strtok(NULL, "\n");
    } while (str);

    // Print update
    printf("The ServerT is up and running using UDP on port %d.\n", server_t_udp_port);

    while (1) {

        // Get info from the central server
        char *in = receive(fd, &central_addr, &len_central_addr);

        printf("The ServerT received a request from Central to get the topology.\n");

        // Get the names and clear the msg & seen
        char *name_a = strtok(in, ";"), 
             *name_b = strtok(NULL, ";");
        msg[0] = '\0';
        seen[0] = '\0';

        // Initialize loop variables
        struct ch *ecur, *qcur, *tmp;
        char *tmp_a, *tmp_b, tmp_c[MAX_LEN];

        // ACreate list and add name_a and name_b to the queue
        TAILQ_HEAD(q, ch) qhead = TAILQ_HEAD_INITIALIZER(qhead);
        TAILQ_INIT(&qhead);

        qcur = (struct ch *) malloc(sizeof(struct ch));
        qcur->val = name_a;
        TAILQ_INSERT_TAIL(&qhead, qcur, qentries);

        qcur = (struct ch *) malloc(sizeof(struct ch));
        qcur->val = name_b;
        TAILQ_INSERT_TAIL(&qhead, qcur, qentries);

        // NOTE: msg will be structured as
        //    node:conn1,conn2,conn3;conn1:conn4,conn5;...

        // Now iterate through the queue and edge list and add the 
        // entire trees stemming from the two given names
        // Super inefficient, but doesn't matter
        TAILQ_FOREACH(qcur, &qhead, qentries) {
            strcat(msg, qcur->val);
            strcat(msg, ":");

            TAILQ_FOREACH(ecur, &ehead, eentries) {

                // Split edge into the two names and check for match
                tmp_a = strtok(strcpy(tmp_c, ecur->val), " ");
                tmp_b = strtok(NULL, " ");

                int is_a = !strcmp(tmp_a, qcur->val);

                if (is_a || !strcmp(tmp_b, qcur->val)) {
                    // Add this connection to msg
                    strcat(msg, is_a ? tmp_b : tmp_a);
                    strcat(msg, ",");

                    // Add the other name to the queue if not already in it
                    struct ch *x;
                    int present = 0;
                    TAILQ_FOREACH(x, &qhead, qentries)
                        if (!strcmp(x->val, is_a ? tmp_b : tmp_a)) {
                            present = 1;
                            break;
                        }

                    if (present) continue;

                    tmp = (struct ch *) malloc(sizeof(struct ch));
                    tmp->val = calloc(strlen(name_a) + 5, sizeof(char));
                    strcpy(tmp->val, is_a ? tmp_b : tmp_a);
                    TAILQ_INSERT_TAIL(&qhead, tmp, qentries);
                }
            }

            // Replace last comma with semicolon (According to piazza, guaranteed at least 1 edge)
            char last = msg[strlen(msg)-1];
            if (last == ',')
                msg[strlen(msg)-1] = ';';
            
            // Add to seen
            strcat(seen, qcur->val);
            strcat(seen, ";");  // use semicolon to avoid things like "timothy" and "moth" interfering
        }

        qcur = TAILQ_FIRST(&qhead);
        while (qcur != NULL) {
                tmp = TAILQ_NEXT(qcur, qentries);
                free(qcur);
                qcur = tmp;
        }

        // Send info to central
        send_to(msg, fd, &central_addr, len_central_addr);
        free(in);

        // Send update to the terminal
        printf("The ServerT finished sending the topology to Central.\n");
    }
}