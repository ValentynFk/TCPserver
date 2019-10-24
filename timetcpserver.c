#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ifaddrs.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

#define SOCKET int
#define ISSOCKETVALID(s) ((s) >= 0)

int main() {
    printf("Local address configuration...\n");
    struct ifaddrs *addresses;

    if (getifaddrs(&addresses) == -1) {
        printf("getifaddrs call failed\n");
        return -1;
    }

    struct ifaddrs *address = addresses;
    char *chosen_address = NULL;
    int chosen_family = AF_INET6;
    while (address) {
        int family = address->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6) {

            printf("%s\t", address->ifa_name);
            printf("%s\t", family == AF_INET ? "IPv4" : "IPv6");
            
            char ap[100];
            const int family_size = family == AF_INET ?
                sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            getnameinfo(address->ifa_addr, family_size, ap, sizeof(ap), 0, 0, NI_NUMERICHOST);
            printf("\t%s\n", ap);
            
            for (;;) {
                char choice;
                printf("choose this address (y or n): ", ap);
                do {
                    choice = getchar();
                } while (choice == '\n');
                
                if (toupper(choice) == 'Y') {
                    int address_size = strlen(ap) + 1;
                    chosen_address = malloc(address_size);
                    memcpy(chosen_address, ap, address_size);
                    chosen_family = family;
                    break;
                } else if (toupper(choice) == 'N') {
                    break;
                }
            }
        }
        address = address->ifa_next;
        
        if (chosen_address != NULL) {
            //memcmp(chosen_address, "127.0.0.1", 9)? "127.0.0.1" : chosen_address;
            break;
        }
    }
    freeifaddrs(addresses);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = chosen_family; // AF_INET of AF_INET6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *bind_address;
    getaddrinfo(chosen_address, "8080", &hints, &bind_address); // fill bind_address structure

    printf("Socket creation...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISSOCKETVALID(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", errno);
        return 1;
    }
    if (chosen_family == AF_INET6) {
        /* for dual stack (IPv6 and IPv4) */
        int ipv6_socket_option = 0;
        if (setsockopt(socket_listen, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&ipv6_socket_option, sizeof(ipv6_socket_option))) {
            fprintf(stderr, "setsocketopt() failed. (%d)\n", errno);
            return 1;
        }
    }
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", errno);
        return 1;
    }
    freeaddrinfo(bind_address); // release bind_address structure
    printf("Listening...\n");
    if (listen(socket_listen, 10) < 0) { // listen on socket_listen up to 10 queued connections
        fprintf(stderr, "listen() failed. (%d)\n", errno);
        return 1;
    }
    for (;;) {
        printf("Waiting for connection...\n");
        struct sockaddr_storage client_address;
        socklen_t client_len = sizeof(client_address);
        SOCKET socket_client = accept(socket_listen, (struct sockaddr*)&client_address, &client_len);
        if (!ISSOCKETVALID(socket_client)) {
            fprintf(stderr, "accept() failed. (%d)\n", errno);
            break;
        }
        printf("========================================================================\n");
        printf("Client is connected... ");
        char address_buffer[100];
        getnameinfo((struct sockaddr*)&client_address, client_len, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
        printf("%s\n", address_buffer);

        printf("Reading request...\n");
        char request[1024];
        int bytes_received = recv(socket_client, request, 1024, 0);
        if (bytes_received <= 0) { 
            fprintf(stderr, "client connection broken. (bytes received: %d)\n", bytes_received);
            break;
        }
        printf("Received %d bytes.\n%.*s\n", bytes_received, bytes_received, request);
        
        printf("Sending response...\n");
        
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain\r\n\r\n"
            "You have sent me this message: ";
        int bytes_sent = send(socket_client, response, strlen(response), 0);
        printf("Sent %d bytes of %d\n", bytes_sent, (int)strlen(response));

        bytes_sent = send(socket_client, request, bytes_received, 0);
        printf("Sent %d bytes of %d\n", bytes_sent, bytes_received);

        time_t timer;
        time(&timer);
        const char *time_msg = ctime(&timer);
        bytes_sent = send(socket_client, time_msg, strlen(time_msg), 0);
        printf("Sent %d bytes of %d\n", bytes_sent, (int)strlen(time_msg));

        printf("Closing connection...\n");
        close(socket_client);
        printf("========================================================================\n");
    }

    return 0;
}
