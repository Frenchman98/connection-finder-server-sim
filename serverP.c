
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
#include <math.h>

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

// Used in list
struct person {
    // Properties
    char *name;
    int score;
    char *edge_str;
    struct person **edges;
    int num_conn;

    // Search
    float total_gap;
    struct person *parent;

    TAILQ_ENTRY(person) pentries;
    TAILQ_ENTRY(person) oentries;
};

// This is adapted from more recent versions of ubuntu
// https://clickhouse.com/codebrowser/html_report/ClickHouse/contrib/librdkafka/src/queue.h.html
// It is used when modifying the linked list within the next block
#define TAILQ_FOREACH_SAFE(var, head, field, next)\
    for ((var) = ((head)->tqh_first); \
         (var) && ((next) = TAILQ_NEXT(var, field), 1);\
         (var) = (next))


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
    addr.sin_port = htons(server_p_udp_port);
    memset(&central_addr, 0, sizeof(central_addr));

    socklen_t len_addr = (socklen_t) sizeof(addr),
              len_central_addr = (socklen_t) sizeof(central_addr);

    // Bind the socket to the address
    if (bind(fd, (struct sockaddr *) &addr, len_addr)) {
        fprintf(stderr, "Unable to bind to the required port.\n");
        return 1;
    }

    // Print update
    printf("The ServerP is up and running using UDP on port %d.\n", server_p_udp_port);

    while (1) {

        // Get info from the central server
        char *in = receive(fd, &central_addr, &len_central_addr);

        printf("The ServerP received the topology and score information.\n");

        // Make list of people - the first 2 are the people to connect 
        TAILQ_HEAD(people, person) phead = TAILQ_HEAD_INITIALIZER(phead);
        TAILQ_INIT(&phead);

        char *msg = calloc(1, sizeof(char)),
             *tmpin = strcpy(calloc(strlen(in) + 2, sizeof(char)), in),
             *save1, *entry = strtok_r(tmpin, ";", &save1), *tok, *tmps;
        
        do {
            struct person *p = malloc(sizeof(struct person));
            char *endptr, *save2;

            tok = strtok_r(entry, ":>", &save2);
            p->name = tok;

            tok = strtok_r(NULL, ":>", &save2);
            p->score = (int) strtol(tok, &endptr, 10);

            tok = strtok_r(NULL, ":>", &save2);
            p->edge_str = calloc(strlen(tok)+1, sizeof(char));
            strcpy(p->edge_str, tok);

            p->total_gap = INFINITY;
            p->parent = NULL;

            TAILQ_INSERT_TAIL(&phead, p, pentries);
        } while ((entry = strtok_r(NULL, ";", &save1)));

        // Now that all people are loaded process the edges
        // Not efficient at all. easiest to implement though lol
        struct person *cur, *conn, *next;
        TAILQ_FOREACH_SAFE(cur, &phead, pentries, next) {

            // Determine number of edges so mem can be allocated
            int i = 0, k = strlen(cur->edge_str);
            cur->num_conn = 1;  // num_conn starts at 1 since there's 1 less comma than conns
            for (i = 0; i < k; i++)
                if (cur->edge_str[i] == ',')
                    cur->num_conn++;
            cur->edges = calloc(cur->num_conn+1, sizeof(struct person *));
            
            // For each name, find the corresponding person obj
            tmps = strcpy(calloc(strlen(cur->edge_str) + 5, sizeof(char)), cur->edge_str);
            char *name = strtok(tmps, ",");
            for (i = 0; name; name = strtok(NULL, ","), i++) {
                TAILQ_FOREACH(conn, &phead, pentries)
                    if (!strcmp(conn->name, name)) {
                        cur->edges[i] = conn;
                        break;
                    }
                if (!cur->edges[i]) fprintf(stderr, "Couldn't find %s.\n", name);
            }
            free(tmps);
        }

        // Now just use Djikstra's algorithm.
        // First make and initialize the open list.
        TAILQ_HEAD(olist, person) ohead = TAILQ_HEAD_INITIALIZER(ohead);
        TAILQ_INIT(&ohead);

        cur = TAILQ_FIRST(&phead);
        cur->total_gap = 0;
        TAILQ_INSERT_HEAD(&ohead, cur, oentries);

        struct person *tmp;
        while (!TAILQ_EMPTY(&ohead)) {
            cur = TAILQ_FIRST(&ohead);
            TAILQ_REMOVE(&ohead, cur, oentries);

            // Add each child to beginning of list and update their values.
            // If they are already in the list, check if the gap would be 
            // lower coming from this person, and update vals accordingly
            int i = 0;
            for (i = 0; i < cur->num_conn; i++) {
                int s = cur->edges[i]->score;
                float gap = (float) abs(cur->score - s) / (float) (cur->score + s);
                float egap = cur->edges[i]->total_gap;

                int present = 0;
                TAILQ_FOREACH(tmp, &ohead, oentries)
                    if (tmp == cur->edges[i]) {
                        present = 1;
                        break;
                    }
                
                float ngap = gap + cur->total_gap;
                if (ngap < egap) {
                    cur->edges[i]->total_gap = ngap;
                    cur->edges[i]->parent = cur;
                    if (!present) TAILQ_INSERT_TAIL(&ohead, cur->edges[i], oentries);
                }
            }
        }

        // Construct the message
        tmp = TAILQ_FIRST(&phead);  // src
        cur = TAILQ_NEXT(tmp, pentries);  // dst
        float tot_gap = cur->total_gap;
        if (cur->parent == NULL) {
            // No path exists
            msg = calloc(strlen(cur->name) + strlen(tmp->name) + 50, sizeof(char));
            strcpy(msg, "None;");
            strcat(msg, cur->name);
            strcat(msg, ";");
            strcat(msg, tmp->name);
            
            tot_gap = (float) 0.0;

        } else while (cur) {
            tok = strcpy(calloc(strlen(msg) + strlen(cur->name) + 2, sizeof(char)), msg);
            free(msg);
            msg = strcat(tok, cur->name);
            strcat(msg, ";");

            cur = cur->parent;
        }

        // Free memory
        cur = TAILQ_FIRST(&phead);
        while (cur != NULL) {
                tmp = TAILQ_NEXT(cur, pentries);
                free(cur->edges);
                free(cur);
                cur = tmp;
        }

        // Add gap value to head (if appplicable) and send to central server
        char *out = calloc(strlen(msg) + 20, sizeof(char));
        if (tot_gap) snprintf(out, (size_t) strlen(msg) + 20, "%.6f;%s", tot_gap, msg);
        else strcpy(out, msg);
        send_to(out, fd, &central_addr, len_central_addr);
        free(out);
        free(in);

        // Send update to the terminal
        printf("The ServerP finished sending the results to Central.\n");
    }
}