#include <sys/socket.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ifaddrs.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

// Socket descriptor and validator
#define SOCKET int
#define ISSOCKETVALID(s) ((s) >= 0)

bool log_yes_no_dialog(const char *question_body) {
    // Display "yes or no" question to the user
    for (;;) {
        char choice;
        printf("%s (Y or N): ", question_body);
        do {
            choice = getchar();
        } while (choice == '\n');

        if (toupper(choice) == 'Y') {
            return true; 
        } else if (toupper(choice) == 'N') {
            return false;
        }
    }
}

int configure_server_address(struct addrinfo **r_address) {
    struct ifaddrs *available_addresses;
    if (getifaddrs(&available_addresses) == -1) {
        printf("getifaddrs call failed\n");
        return -1;
    }

    char *chosen_address = NULL;
    int chosen_address_family = AF_INET6; // IPv6 by default
    struct ifaddrs *address_iter = available_addresses;
    while (address_iter) {
        int address_family = address_iter->ifa_addr->sa_family;
        if (address_family == AF_INET || address_family == AF_INET6) {
            // Address is either IPv4 or IPv6 address
            printf("%s\t", address_iter->ifa_name);
            printf("%s\t", address_family == AF_INET ? "IPv4" : "IPv6");
            char user_friendly_address[100];
            const int address_info_size = (address_family == AF_INET)?
                sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            getnameinfo(address_iter->ifa_addr, address_info_size,
                        user_friendly_address, sizeof(user_friendly_address),
                        0, 0, NI_NUMERICHOST);
            printf("\t%s\n", user_friendly_address);

            if(log_yes_no_dialog("use this address?")) {
                const int address_size = strlen(user_friendly_address) + 1;
                chosen_address = malloc(address_size);
                memcpy(chosen_address, user_friendly_address, address_size);
                
                chosen_address_family = address_family;
                break;
            }
        }
        address_iter = address_iter->ifa_next;
    }
    freeifaddrs(available_addresses);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = chosen_address_family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    getaddrinfo(chosen_address, "8080", &hints, r_address);
    return 0;
}

int create_listener_socket(struct addrinfo *address, SOCKET *listener_socket) {
    *listener_socket =
        socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (!ISSOCKETVALID(*listener_socket)) {
        fprintf(stderr, "socket() failed. (%d)\n", errno);
        return -1;
    }
    if (address->ai_family == AF_INET6) {
        printf("IPv6 address\n");
        // for dual stack (IPv6 and IPv4)
        int ipv6_socket_option = 0;
        if (setsockopt(*listener_socket, IPPROTO_IPV6, IPV6_V6ONLY, 
            (void *)&ipv6_socket_option, sizeof(ipv6_socket_option))) {
            fprintf(stderr, "setsocketopt() failed. (%d)\n", errno);
            return -1;
        }
    }
    if (bind(*listener_socket, address->ai_addr, address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", errno);
        return -1;
    }
    if (listen(*listener_socket, 10) < 0) { // 10 queued connections
        fprintf(stderr, "listen() failed. (%d)\n", errno);
        return -1;
    }
    return 0;
}

int read_request_and_send_response(const SOCKET socket_descriptor) {
    printf("Reading request...\n");
    char request[1024];
    int bytes_received = recv(socket_descriptor, request, 1024, 0);
    if (bytes_received <= 0) { 
        fprintf(stderr, "connection broken. (bytes received: %d)\n", bytes_received);
        return -1;
    }
    request[bytes_received] = '\0'; // No guarantees that request is null terminated
    printf("Received %d bytes.\n%s\n", bytes_received, request);

    printf("Sending response...\n");
    int bytes_sent = 0;
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "You have sent me this message:\r\n";
    time_t timer;
    time(&timer);
    const char *time_msg = ctime(&timer);

    bytes_sent = send(socket_descriptor, response, strlen(response), 0);
    printf("Sent %d bytes of %d\n", bytes_sent, (int)strlen(response));
    bytes_sent = send(socket_descriptor, request, strlen(request), 0);
    printf("Sent %d bytes of %d\n", bytes_sent, bytes_received);
    bytes_sent = send(socket_descriptor, time_msg, strlen(time_msg), 0);
    printf("Sent %d bytes of %d\n", bytes_sent, (int)strlen(time_msg));
    return 0;
}

int main() {
    printf("Local address configuration...\n");
    struct addrinfo *bind_address; // create bind_address structure
    if(configure_server_address(&bind_address)) {
        fprintf(stderr, "address configuration failed.\n");
        return -1;
    }
    
    printf("Socket creation...\n");
    SOCKET listener_socket; // define socket
    if(create_listener_socket(bind_address, &listener_socket)) {
        fprintf(stderr, "address configuration failed.\n");
        return -1;
    }
    freeaddrinfo(bind_address); // release bind_address structure
    
    for (;;) {
        printf("Waiting for connection...\n");
        
        struct sockaddr_storage client_address;
        socklen_t client_len = sizeof(client_address);
        SOCKET client_socket = accept(listener_socket,
            (struct sockaddr*)&client_address, &client_len);
        if (!ISSOCKETVALID(client_socket)) {
            fprintf(stderr, "accept() failed. (%d)\n", errno);
            continue;
        }
        printf("================================================================================\n");
        printf("Client is connected... ");
        
        char address_buffer[100];
        getnameinfo((struct sockaddr*)&client_address, client_len,
            address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
        printf("Client address is: %s\n", address_buffer);

        if (read_request_and_send_response(client_socket)) {
            continue;
        }

        printf("Closing connection...\n");
        close(client_socket);
        printf("================================================================================\n");
    }

    return 0;
}
