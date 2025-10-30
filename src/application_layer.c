#include "application_layer.h"
#include "link_layer.h" 
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 512 // Size of each data block to send/receive

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    
    // Set link layer configuration parameters
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    
    // Define application role: transmitter or receiver
    if (strcmp(role, "tx") == 0) {
        connectionParameters.role = LlTx;
    } else if (strcmp(role, "rx") == 0) {
        connectionParameters.role = LlRx;
    } else {
        printf("ERROR: Invalid role\n");
        return;
    }
    
    // Establish connection using link layer
    int result = llopen(connectionParameters);
    if (result < 0) {
        printf("ERROR: Could not establish connection\n");
        return;
    }
    
    printf("Connection established successfully\n");
    
    // --- TRANSMITTER MODE ---
    if (strcmp(role, "tx") == 0) {
        FILE *file = fopen(filename, "rb");
        if (!file) {
            printf("ERROR: Could not open file %s\n", filename);
            llclose(connectionParameters);
            return;
        }

        unsigned char buffer[BLOCK_SIZE];
        int bytesRead;

        // Read file and send data blocks through llwrite
        while ((bytesRead = fread(buffer, 1, BLOCK_SIZE, file)) > 0) {
            int writeResult = llwrite(buffer, bytesRead);
            if (writeResult < 0) {
                printf("ERROR: Failed to send data\n");
                break;
            }
            printf("Sent %d bytes\n", writeResult);
        }

        fclose(file);
        printf("File transmission finished\n");
    
    // --- RECEIVER MODE ---
    } else {
        FILE *file = fopen(filename, "wb");
        if (!file) {
            printf("ERROR: Could not create file %s\n", filename);
            llclose(connectionParameters);
            return;
        }

        unsigned char buffer[BLOCK_SIZE];
        int readResult;

        // Receive data blocks through llread and write to file
        while ((readResult = llread(buffer)) > 0) {
            fwrite(buffer, 1, readResult, file);
            printf("Received %d bytes\n", readResult);
        }

        // Check if transmission ended successfully
        if (readResult < 0) {
            printf("ERROR: Failed to receive data\n");
        } else {
            printf("File reception finished\n");
        }

        fclose(file);
    }
    
    // Close link layer connection
    llclose(connectionParameters);
}