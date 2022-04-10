
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

#define min(x, y) (((x) < (y)) ? (x) : (y))
#define strtok_r __strtok_r

// -------------------------------------


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


char* get_topology(char *name_a, char *name_b, int fd, struct sockaddr_in *t_addrp, socklen_t *len_t_addrp) {
    char msg[MAX_LEN];

    // Create message for server T
    memset(msg, 0, MAX_LEN);
    strcpy(msg, name_a);
    strcat(msg, ";");
    strcat(msg, name_b);

    // Send to server T
    send_to(msg, fd, t_addrp, *len_t_addrp);

    // Update terminal
    printf("The Central server sent a request to Backend-Server T.\n");

    // Receive response
    char *out = receive(fd, t_addrp, len_t_addrp);
    
    // Update terminal and return
    printf("The Central server received information from Backend-Server T using UDP over port %d.\n", central_udp_port);
    return out;
}


char* get_scores(char *msg, int fd, struct sockaddr_in *s_addrp, socklen_t *len_s_addrp) {

    // Send to server S
    send_to(msg, fd, s_addrp, *len_s_addrp);

    // Update terminal
    printf("The Central server sent a request to Backend-Server S.\n");

    // Receive response
    char *out = receive(fd, s_addrp, len_s_addrp);
    
    // Update terminal and return
    printf("The Central server received information from Backend-Server S using UDP over port %d.\n", central_udp_port);
    return out;
}


char* get_results(char *msg, int fd, struct sockaddr_in *p_addrp, socklen_t *len_p_addrp) {

    // Send to server P
    send_to(msg, fd, p_addrp, *len_p_addrp);

    // Update terminal
    printf("The Central server sent a processing request to Backend-Server P.\n");

    // Receive response
    char *out = receive(fd, p_addrp, len_p_addrp);
    
    // Update terminal and return
    printf("The Central server received the results from backend server P.\n");
    return out;
}


int main(int argc, char *argv[]) {

    if (argc != 1) {
        fprintf(stderr, "Invalid number of arguments used (0 expected).\n");
        return 1;
    }

    // We need 3 sockets: one for each client and one for own udp socket
    int ca_fd = socket(AF_INET, SOCK_STREAM, 0),  // SOCK_STREAM for TCP with Clients
        cb_fd = socket(AF_INET, SOCK_STREAM, 0),
        udp_fd = socket(AF_INET, SOCK_DGRAM, 0);  // SOCK_DGRAM for UDP with Back servers

    if (!ca_fd || !cb_fd || !udp_fd) {
        fprintf(stderr, "A required socket wasn't able to open properly.\n");
        return 1;
    }

    // Set all sockets to be reusable
    int opt = 1;
    setsockopt(ca_fd, SOL_SOCKET, SO_REUSEADDR || SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(cb_fd, SOL_SOCKET, SO_REUSEADDR || SO_REUSEPORT, &opt, sizeof(opt));
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR || SO_REUSEPORT, &opt, sizeof(opt));

    // Make the address objects for all devices/sockets
    struct sockaddr_in ca_addr, cb_addr, t_addr, s_addr, p_addr, udp_addr;
    
    ca_addr.sin_family = AF_INET;
    cb_addr.sin_family = AF_INET;
    t_addr.sin_family = AF_INET;
    s_addr.sin_family = AF_INET;
    p_addr.sin_family = AF_INET;
    udp_addr.sin_family = AF_INET;

    ca_addr.sin_addr.s_addr = inet_addr(localhost);
    cb_addr.sin_addr.s_addr = inet_addr(localhost);
    t_addr.sin_addr.s_addr = inet_addr(localhost);
    s_addr.sin_addr.s_addr = inet_addr(localhost);
    p_addr.sin_addr.s_addr = inet_addr(localhost);
    udp_addr.sin_addr.s_addr = inet_addr(localhost);

    ca_addr.sin_port = htons(central_tcp_a_port);
    cb_addr.sin_port = htons(central_tcp_b_port);
    t_addr.sin_port = htons(server_t_udp_port);
    s_addr.sin_port = htons(server_s_udp_port);
    p_addr.sin_port = htons(server_p_udp_port);
    udp_addr.sin_port = htons(central_udp_port);

    socklen_t len_ca_addr = (socklen_t) sizeof(ca_addr),
              len_cb_addr = (socklen_t) sizeof(cb_addr),
              len_t_addr = (socklen_t) sizeof(t_addr),
              len_s_addr = (socklen_t) sizeof(s_addr),
              len_p_addr = (socklen_t) sizeof(p_addr),
              len_udp_addr = (socklen_t) sizeof(udp_addr);

    // Bind each socket to their respective addresses
    if (bind(ca_fd, (struct sockaddr *) &ca_addr, len_ca_addr) ||
        bind(cb_fd, (struct sockaddr *) &cb_addr, len_cb_addr) ||
        bind(udp_fd, (struct sockaddr *) &udp_addr, len_udp_addr)) {
        
        fprintf(stderr, "Unable to bind to one of the client ports.\n");
        return 1;
    }

    // Then listen on each client port. Allow SOMAXCONN queued connections on each
    if (listen(ca_fd, SOMAXCONN) || listen(cb_fd, SOMAXCONN)) {
        fprintf(stderr, "Unable to listen on one of the client sockets.\n");
        return 1;
    }

    // Print status
    printf("The Central server is up and running.\n");

    // Initialize variables used in the loop
    int a_fd, b_fd;
    char name_a[MAX_LEN], name_b1[MAX_LEN], name_b2[MAX_LEN], buf[MAX_LEN];
    
    // Start the central while loop
    while (1) {

        // Wait for clientA to contact us
        a_fd = accept(ca_fd, (struct sockaddr *) &ca_addr, &len_ca_addr);
        if (a_fd < 0) {
            fprintf(stderr, "Error trying to establish a connection with client A.\n");
            continue;
        }

        // Get the input from the connection, print what is received
        memset(buf, 0, MAX_LEN);
        read(a_fd, buf, MAX_LEN - 1);
        printf("The Central server received input=\"%s\" from the client using TCP over port %d.\n", buf, central_tcp_a_port);

        // Store in a variable
        strcpy(name_a, buf);
        
        // Then wait for clientB to contact us
        b_fd = accept(cb_fd, (struct sockaddr *) &cb_addr, &len_cb_addr);
        if (b_fd < 0) {
            fprintf(stderr, "Error trying to establish a connection with client B.\n");
            continue;
        }

        // Get the input from the connection, print what is received
        memset(buf, 0, MAX_LEN);
        read(b_fd, buf, MAX_LEN - 1);
        printf("The Central server received input=\"%s\" from the client using TCP over port %d.\n", buf, central_tcp_b_port);

        // Check if there are 1 or 2 names and store in variables
        int n = 0, i = -1;
        name_b2[0] = '\0';
        do {
            i++;

            // Detect space
            char c = buf[i];
            if (c == ';') {
                name_b1[i] = '\0';
                n = i + 1;
                continue;
            }

            // Store in variables
            if (!n)
                name_b1[i] = c;
            else
                name_b2[i-n] = c;
            
        } while (buf[i] != '\0');

        // Get the topology for each
        char *t1 = get_topology(name_a, name_b1, udp_fd, &t_addr, &len_t_addr), 
             *t2 = (name_b2[0]) ? get_topology(name_a, name_b2, udp_fd, &t_addr, &len_t_addr) : NULL;

        // Get the unique names from the topologies
        char *tmp = calloc(strlen(t1) + 5, sizeof(char)),
            *msg1 = calloc(strlen(t1) + 5, sizeof(char)),
            *conn1 = calloc(strlen(t1) + 5, sizeof(char)), 
            *conn2, *msg2, *tok;

        strcpy(tmp, t1);
        tok = strtok(tmp, ":;");
        do {
            strcat(msg1, tok);
            strcat(msg1, ";");

            strcat(conn1, strtok(NULL, ":;"));  // Store the connections
            strcat(conn1, ";");
        } while ((tok = strtok(NULL, ":;")));
        
        if (t2) {
            free(tmp);
            msg2 = calloc(strlen(t2) + 5, sizeof(char));
            conn2 = calloc(strlen(t2) + 5, sizeof(char));
            tmp = calloc(strlen(t2) + 5, sizeof(char));

            strcpy(tmp, t2);
            tok = strtok(tmp, ":;");
            do {
                strcat(msg2, tok);
                strcat(msg2, ";");

                strcat(conn2, strtok(NULL, ":;"));  // Store the connections
                strcat(conn2, ";");
            } while ((tok = strtok(NULL, ":;")));
        }

        // Ask the scores server for the scores
        char *s1 = get_scores(msg1, udp_fd, &s_addr, &len_s_addr),
             *s2 = (t2) ? get_scores(msg2, udp_fd, &s_addr, &len_s_addr) : NULL;
        
        // Now construct the message(s) to send to server P
        free(tmp);
        free(msg1);
        if (t2) free(msg2);

        msg1 = calloc(strlen(s1) + strlen(conn1) + 10, sizeof(char));
        char *s1c = strcpy(calloc(strlen(s1) + 10, sizeof(char)), s1);

        char *save_node, *save_edge, *etok = strtok_r(conn1, ";", &save_edge);
        tok = strtok_r(s1c, ";", &save_node);

        do {
            // Each entry will look like "name:score>edge_list;"
            strcat(msg1, tok);
            strcat(msg1, ">");
            strcat(msg1, etok);
            strcat(msg1, ";");

            etok = strtok_r(NULL, ";", &save_edge);
            tok = strtok_r(NULL, ";", &save_node);
        } while (etok && tok);

        if (t2) {
            save_edge = save_node = NULL;
            msg2 = calloc(strlen(s2) + strlen(conn2) + 5, sizeof(char));
            etok = strtok_r(conn2, ";", &save_edge);
            tok = strtok_r(s2, ";", &save_node);

            do {
                // Each entry will look like "name:score>edge_list;"
                strcat(msg2, tok);
                strcat(msg2, ">");
                strcat(msg2, etok);
                strcat(msg2, ";");

                etok = strtok_r(NULL, ";", &save_edge);
                tok = strtok_r(NULL, ";", &save_node);
            } while (etok && tok);
        }

        char *results1 = get_results(msg1, udp_fd, &p_addr, &len_p_addr),
             *results2 = (t2) ? get_results(msg2, udp_fd, &p_addr, &len_p_addr) : NULL;

        // Then just concatenate the results if applicable
        if (t2) {
            tmp = strcpy(calloc(strlen(results1) + strlen(results2) + 10, sizeof(char)), results1);
            strcat(tmp, ">");
            strcat(tmp, results2);
            free(results1);

            results1 = tmp;
        }

        // Now send the results to them and update terminal
        if (send(a_fd, results1, strlen(results1), 0) < 0)
            fprintf(stderr, "Could not send results back to client A.\n");
        else
            printf("The Central server sent the results to client A.\n");
        
        if (send(b_fd, results1, strlen(results1), 0) < 0)
            fprintf(stderr, "Could not send results back to client B.\n");
        else
            printf("The Central server sent the results to client B.\n");
        
        // Cleanup
        free(results1);
        free(conn1);
        free(msg1);
        free(t1);
        free(s1);
        if (t2) {
            free(results2);
            free(conn2);
            free(msg2);
            free(t2);
            free(s2);
        }
    }
}