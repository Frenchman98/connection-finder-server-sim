
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

#define min(x, y) (((x) < (y)) ? (x) : (y))

// -------------------------------------

// Used in list
struct ch {
    char *person;
    char *score;
    TAILQ_ENTRY(ch) entries;
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
    addr.sin_port = htons(server_s_udp_port);
    memset(&central_addr, 0, sizeof(central_addr));

    socklen_t len_addr = (socklen_t) sizeof(addr),
              len_central_addr = (socklen_t) sizeof(central_addr);

    // Bind the socket to the address
    if (bind(fd, (struct sockaddr *) &addr, len_addr)) {
        fprintf(stderr, "Unable to bind to the required port.\n");
        return 1;
    }

    // Load scores from file
    FILE* score_f = fopen("./scores.txt", "r");

    size_t len;
    char *scores = 0;
    getdelim(&scores, &len, '\0', score_f);

    fclose(score_f);

    // Create list of people and their scores
    char *str = strtok(scores, " \n");
    TAILQ_HEAD(scorelist, ch) shead = TAILQ_HEAD_INITIALIZER(shead);
    TAILQ_INIT(&shead);
    struct ch *cur;

    do {
        cur = (struct ch *) malloc(sizeof(struct ch));
        cur->person = str;
        cur->score = strtok(NULL, " \n");
        TAILQ_INSERT_TAIL(&shead, cur, entries);

        str = strtok(NULL, " \n");
    } while (str);

    // Print update
    printf("The ServerS is up and running using UDP on port %d.\n", server_s_udp_port);

    while (1) {

        // Get info from the central server
        char *in = receive(fd, &central_addr, &len_central_addr);

        printf("The ServerS received a request from Central to get the scores.\n");

        // Iterate through the names and add them and their scores 
        // to the message 
        char *msg = calloc(3, sizeof(char)), *tmp2, *tmp, *tok = strtok(in, ";");

        do {
            TAILQ_FOREACH(cur, &shead, entries) {
                if (!strcmp(cur->person, tok)) {
                    // Add the score to the person
                    tmp = calloc(strlen(tok) + strlen(cur->score) + 5, sizeof(char));
                    strcpy(tmp, tok);
                    strcat(tmp, ":");
                    strcat(tmp, cur->score);
                    strcat(tmp, ";");

                    // Update the msg
                    tmp2 = strcpy(calloc(strlen(msg) + strlen(tmp) + 10, sizeof(char)), msg);
                    free(msg);
                    msg = strcat(tmp2, tmp);
                    free(tmp);

                    break;
                }
            }
        } while ((tok = strtok(NULL, ";")));

        // Add header and send to central server
        send_to(msg, fd, &central_addr, len_central_addr);
        free(in);
        free(msg);

        // Send update to the terminal
        printf("The ServerS finished sending the scores to Central.\n");
    }
}