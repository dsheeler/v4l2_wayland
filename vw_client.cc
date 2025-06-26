#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <CLI/CLI.hpp>
#define PORT 12345

int main(int argc, char const *argv[]) {
    const char *history_file = "/home/dsheeler/.vw_client_history";
    int status, valread, client_fd;
    struct sockaddr_in serv_addr;
    
    CLI::App app{"vw_client"};
    std::string entered_command;
    std::vector<std::string> files;
    app.add_option("-e", entered_command, "Command to execute");
    app.add_option("file", files, "Files to read commands from")
    ->check(CLI::ExistingFile)
    ->expected(-1); // Allow multiple files
    CLI11_PARSE(app, argc, argv);
    
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IPv4 and IPv6 addresses from text to binary
    // form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
    
    if ((status = connect(client_fd, (struct sockaddr *)&serv_addr,
    sizeof(serv_addr))) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }
    using_history();
    // Load the history file
    int ret = read_history(history_file);
    if (ret != 0) {
        printf("Failed to read history file: %s\n", history_file);
    }
    if (entered_command.length() > 0) {
        // If a command was entered, send it to the server
        printf("Sending command: %s\n", entered_command.c_str());
        send(client_fd, entered_command.c_str(), entered_command.length(), 0);
        return 0;
    } else if (!files.empty()) {
        // If a file is provided, read it line by line and send each line to the server
        for (auto filename : files) {
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Error opening file: " << filename << std::endl;
                return 1; // Indicate an error
            }
            file.close();
        }
        for (auto filename : files) {
            std::ifstream file(filename);
            struct timespec tim;
            tim.tv_sec = 0;       // 0 seconds
            tim.tv_nsec = 5000000; // 5 million nanoseconds (0.5 seconds)
            
            while (!file.eof()) {
                std::string line;
                std::getline(file, line);
                if (file.eof()) {
                    break; // Exit the loop if end of file is reached
                }
                if (file.fail()) {
                    std::cerr << "Error reading line from file: " << filename << std::endl;
                    break; // Exit the loop on read error
                }
                // Check if the line is not empty before sending
                if (!line.empty()) {
                    printf("Sending: %s\n", line.c_str());
                    size_t sent = send(client_fd, (line + "\n").c_str(), line.length() + 1, 0);
                    if (sent < 0) {
                        perror("send");
                        break; // Exit the loop on send error
                    }
                    fsync(client_fd); // Ensure the data is sent immediately
                    nanosleep(&tim, NULL);
                }
            }
            printf("End of file reached for %s\n", filename.c_str());
            // Close the file after reading
            file.close();
        }
    } else {
        char *input;
        while ((input = readline("vw_client$ ")) != nullptr) {
            if (strlen(input) > 0) {
                add_history(input);
                send(client_fd, input, strlen(input), 0);
                write_history(history_file);
            }
            free(input); // Free the allocated memory}
        }
    }
    // closing the connected socket
    close(client_fd);
    return 0;
}