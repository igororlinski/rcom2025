// Application layer protocol implementation for file transfer
#include "application_layer.h"
#include "link_layer.h" 
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 64 

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;
    
    if (strcmp(role, "tx") == 0) {
        connectionParameters.role = LlTx;
    } else if (strcmp(role, "rx") == 0) {
        connectionParameters.role = LlRx;
    } else {
        printf("ERROR: Invalid role\n");
        return;
    }
    
    int result = llopen(connectionParameters);
    if (result < 0) {
        printf("ERROR: Could not establish connection\n");
        return;
    }
    
    printf("Connection established successfully!\n");
    
    if (strcmp(role, "tx") == 0) {
        // TRANSMITTER: otwieramy plik i wysyłamy blokami
        FILE *file = fopen(filename, "rb");
        if (!file) {
            printf("ERROR: Could not open file %s\n", filename);
            llclose(connectionParameters);
            return;
        }

        unsigned char buffer[BLOCK_SIZE];
        int bytesRead;
        while ((bytesRead = fread(buffer, 1, BLOCK_SIZE, file)) > 0) {
            int writeResult = llwrite(buffer, bytesRead);
            if (writeResult < 0) {
                printf("ERROR: Failed to send data\n");
                break;
            }
            printf("Sent %d bytes\n", writeResult);
        }

        fclose(file);
        printf("File transmission finished.\n");

    } else {
        // RECEIVER: otwieramy plik do zapisu i odbieramy blokami
        FILE *file = fopen(filename, "wb");
        if (!file) {
            printf("ERROR: Could not create file %s\n", filename);
            llclose(connectionParameters);
            return;
        }

        unsigned char buffer[BLOCK_SIZE];
        int readResult;
        while ((readResult = llread(buffer)) > 0) {
            fwrite(buffer, 1, readResult, file);
            printf("Received %d bytes\n", readResult);
        }

        if (readResult < 0) {
            printf("ERROR: Failed to receive data\n");
        } else {
            printf("File reception finished.\n");
        }

        fclose(file);
    }
    
    llclose(connectionParameters);
}
