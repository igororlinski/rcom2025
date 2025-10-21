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
    
    llclose();
}