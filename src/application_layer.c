// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h" 
#include <stdio.h>
#include <string.h>

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
        unsigned char testData[] = "Hello from transmitter!";
        int writeResult = llwrite(testData, sizeof(testData) - 1);
        if (writeResult > 0) {
            printf("Successfully sent %d bytes\n", writeResult);
        } else {
            printf("Failed to send data\n");
        }
    } else {
        unsigned char buffer[MAX_PAYLOAD_SIZE];
        int readResult = llread(buffer);
        if (readResult > 0) {
            buffer[readResult] = '\0';
            printf("Received %d bytes: %s\n", readResult, buffer);
        } else if (readResult == 0) {
            printf("Connection closed by transmitter\n");
        } else {
            printf("Failed to receive data\n");
        }
    }
    
    llclose(connectionParameters);
}