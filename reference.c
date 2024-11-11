#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>

#define MAX_REQUEST_SIZE 1024
#define STATIC_DIR "static"

int request_count = 0;
long total_received_bytes = 0;
long total_sent_bytes = 0;
int server_port = 80;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

// Utility function to send an HTTP response
void send_response(int client_socket, const char *status_code, const char *content_type, const char *body) {
    char response[MAX_REQUEST_SIZE];
    int content_length = strlen(body);

    // Format HTTP response
    snprintf(response, sizeof(response),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "Server: SimpleCServer/1.0\r\n\r\n"
             "%s",
             status_code, content_type, content_length, body);

    // Send the response
    send(client_socket, response, strlen(response), 0);

    // Update stats for sent bytes
    pthread_mutex_lock(&stats_lock);
    total_sent_bytes += strlen(response);
    pthread_mutex_unlock(&stats_lock);
}

// Handle the /stats endpoint
void handle_stats(int client_socket) {
    char body[MAX_REQUEST_SIZE];
    
    // Format stats in HTML
    snprintf(body, sizeof(body),
             "<html><head><title>Server Stats</title></head>"
             "<body><h1>Server Stats</h1>"
             "<p>Requests received: %d</p>"
             "<p>Total received bytes: %ld</p>"
             "<p>Total sent bytes: %ld</p>"
             "</body></html>", request_count, total_received_bytes, total_sent_bytes);

    // Send response with stats
    send_response(client_socket, "200 OK", "text/html", body);
}

// Handle the /calc endpoint
void handle_calc(int client_socket, const char *query_string) {
    double a, b, result;
    char body[MAX_REQUEST_SIZE];

    // Parse query parameters
    if (sscanf(query_string, "a=%lf&b=%lf", &a, &b) == 2) {
        result = a + b;
        snprintf(body, sizeof(body), "<html><body><h1>Calculation Result: %.2f + %.2f = %.2f</h1></body></html>", a, b, result);
        send_response(client_socket, "200 OK", "text/html", body);
    } else {
        snprintf(body, sizeof(body), "<html><body><h1>Error: Invalid input parameters</h1></body></html>");
        send_response(client_socket, "400 Bad Request", "text/html", body);
    }
}

// Handle the /static endpoint
void handle_static(int client_socket, const char *file_path) {
    FILE *file;
    char file_full_path[MAX_REQUEST_SIZE];
    char body[MAX_REQUEST_SIZE];
    
    // Construct the full file path
    snprintf(file_full_path, sizeof(file_full_path), "%s%s", STATIC_DIR, file_path);

    file = fopen(file_full_path, "rb");
    if (file) {
        // File found, send its content
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char *file_content = malloc(file_size);
        fread(file_content, 1, file_size, file);
        fclose(file);

        // Send binary data with appropriate content type
        send(client_socket, file_content, file_size, 0);

        free(file_content);

        // Update stats for received bytes
        pthread_mutex_lock(&stats_lock);
        total_received_bytes += file_size;
        pthread_mutex_unlock(&stats_lock);
    } else {
        snprintf(body, sizeof(body), "<html><body><h1>File Not Found</h1></body></html>");
        send_response(client_socket, "404 Not Found", "text/html", body);
    }
}

// Parse the HTTP request
void parse_request(int client_socket, const char *request) {
    char method[16], path[256], version[16];
    char query_string[256] = {0};
    int i = 0;
    const char *query_start = NULL;

    // Parse the request line: method, path, and version
    if (sscanf(request, "%15s %255s %15s", method, path, version) != 3) {
        return;
    }

    // Update stats for received bytes
    pthread_mutex_lock(&stats_lock);
    total_received_bytes += strlen(request);
    pthread_mutex_unlock(&stats_lock);

    // Handle GET requests only
    if (strcasecmp(method, "GET") != 0) {
        return;
    }

    // Check if there is a query string
    query_start = strchr(path, '?');
    if (query_start) {
        strncpy(query_string, query_start + 1, sizeof(query_string) - 1);
        *strchr(query_string, ' ') = '\0'; // Remove any space after query string
        *query_start = '\0'; // Null-terminate the path part
    }

    // Handle routes based on the path
    if (strncmp(path, "/static", 7) == 0) {
        handle_static(client_socket, path);
    } else if (strcmp(path, "/stats") == 0) {
        handle_stats(client_socket);
    } else if (strncmp(path, "/calc", 5) == 0) {
        handle_calc(client_socket, query_string);
    } else {
        send_response(client_socket, "404 Not Found", "text/html", "<html><body><h1>404 Not Found</h1></body></html>");
    }
}

// Thread function to handle client requests
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    char request[MAX_REQUEST_SIZE];
    int bytes_received;

    // Receive the request from the client
    bytes_received = recv(client_socket, request, sizeof(request) - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return NULL;
    }

    // Null-terminate the request
    request[bytes_received] = '\0';

    // Parse and handle the request
    parse_request(client_socket, request);

    // Close the client socket
    close(client_socket);
    return NULL;
}

// Start the server
void start_server() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread;

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    // Set up server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_port);

    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("Server running on port %d...\n", server_port);

    // Accept client connections and create a new thread for each client
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        // Increment request count
        pthread_mutex_lock(&stats_lock);
        request_count++;
        pthread_mutex_unlock(&stats_lock);

        // Create a new thread to handle the client
        pthread_create(&thread, NULL, handle_client, (void *)&client_socket);
        pthread_detach(thread);
    }

    close(server_socket);
}

int main(int argc, char *argv[]) {
    // Parse command-line arguments for port
    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        server_port = atoi(argv[2]);
    }

    // Create the static directory if it doesn't exist
    if (access(STATIC_DIR, F_OK) == -1) {
        if (mkdir(STATIC_DIR, 0755) == -1) {
            perror("Failed to create static directory");
            return 1;
        }
    }

    // Start the server
    start_server();
    return 0;
}
