
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

#define xxx                372
#define server_t_udp_port  21000 + xxx
#define server_s_udp_port  22000 + xxx
#define server_p_udp_port  23000 + xxx
#define central_udp_port   24000 + xxx
#define central_tcp_a_port 25000 + xxx
#define central_tcp_b_port 26000 + xxx

// -------------------------------------

// Processes the result from the server for printing
void process(char *_result) {
    // Flip the results order since it is backwards for this client
    char *gapstr = strtok(_result, ";"), *tok = strtok(NULL, ";"), 
         *result = calloc(2, sizeof(char)), *tmp;

    do {
        tmp = strcpy(calloc(strlen(result) + strlen(tok) + 5, sizeof(char)), tok);
        strcat(tmp, ";");
        strcat(tmp, result);
        free(result);
        result = tmp;
    } while ((tok = strtok(NULL, ";")));

    // Then prepend the gap value again
    tmp = strcpy(calloc(strlen(result) + strlen(gapstr) + 5, sizeof(char)), gapstr);
    strcat(tmp, ";");
    strcat(tmp, result);
    free(result);
    result = tmp;

    char *endptr, *name_a, *name_b;
    tok = strtok(result, ";");

    if (!strcmp(tok, "None")) {
        // No compatibility
        name_a = strtok(NULL, ";");
        name_b = strtok(NULL, ";");

        printf("Found no compatibility for %s and %s\n", name_a, name_b);
        return;
    }

    // Init variables
    float gap = strtof(tok, &endptr);
    char *path = calloc(2, sizeof(char)), *ptok;
    ptok = name_a = strtok(NULL, ";");
    tok = strtok(NULL, ";");

    do {
        // Copy path and free it, then append name and hyphens
        tmp = strcpy(calloc(strlen(path) + strlen(ptok) + 10, sizeof(char)), path);
        free(path);
        path = strcat(tmp, ptok);
        strcat(path, " --- ");
    } while ((ptok = tok) && (tok = strtok(NULL, ";")));

    // Then add the last person
    name_b = ptok;
    tmp = strcpy(calloc(strlen(path) + strlen(ptok) + 10, sizeof(char)), path);
    free(path);
    path = strcat(tmp, ptok);

    // Now output everything
    printf("Found compatibility for %s and %s:\n", name_a, name_b);
    printf("%s\n", path);
    printf("Compatibility score: %.2f\n", gap);

    free(path);
    free(result);
}


int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments used.\n");
        return 1;
    }

    // Load name into message
    char msg[MAX_LEN], res[MAX_LEN];
    memset(msg, 0, MAX_LEN);
    memset(res, 0, MAX_LEN);
    strcpy(msg, argv[1]);

    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);  // SOCK_STREAM for TCP
    if (!fd) {
        fprintf(stderr, "There was a problem creating the socket.\n");
        return 1;
    }

    // Make the server address object
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(localhost);
    addr.sin_port = htons(central_tcp_a_port);

    socklen_t len_addr = (socklen_t) sizeof(addr);

    // Send update to terminal
    printf("The client is up and running.\n");

    // Then connect to central server
    if (connect(fd, (struct sockaddr *) &addr, len_addr)) {
        fprintf(stderr, "There was a problem connecting to the central server.\n");
        return 1;
    }

    // Send the name
    if (send(fd, msg, strlen(msg), 0) < 0) {
        fprintf(stderr, "There was a problem sending the entered name to the central server.\n");
        return 1;
    }

    // Send update to terminal
    printf("The client sent %s to the Central server.\n", msg);

    // Wait for client B to send name(s) too and for the server to respond
    if (read(fd, res, MAX_LEN - 1) < 0) {
        fprintf(stderr, "There was a problem receiving info from the central server.\n");
        return 1;
    }

    // Split the received response into the 2 responses
    char *result1 = strtok(res, ">"), *result2 = strtok(NULL, ">");

    // Then print results
    process(result1);
    if (result2) process(result2);
}