#define _POSIX_SOURCE 1
#define BUF_SIZE 5  
#define FALSE 0
#define TRUE 1

#include "link_layer.h"
#include "serial_port.h"
#include <signal.h>
#include <stdlib.h> 
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

const char FLAG = 0x7E;
const char ADDRESS_TR = 0x03;
const char ADDRESS_RT = 0x01;
const char CONTROL_SET = 0x03;
const char CONTROL_UA = 0x07;
const char CONTROL_DISC = 0x0B;

int alarmEnabled = FALSE;
int alarmCount = 0;
int connection_fd = -1;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0) {
        printf("ERROR: Cannot open serial port %s\n", connectionParameters.serialPort);
        return -1;
    }
    
    connection_fd = fd;

    unsigned char buf[BUF_SIZE] = {0};
    
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        closeSerialPort();
        return -1;
    }

    int STOP = 0;
    int maxAlarm = connectionParameters.nRetransmissions;
    int state;

    alarmEnabled = FALSE;
    alarmCount = 0;
    
    switch (connectionParameters.role) {
        case LlTx: {
            buf[0] = FLAG;
            buf[1] = ADDRESS_TR;
            buf[2] = CONTROL_SET;
            buf[3] = ADDRESS_TR ^ CONTROL_SET;
            buf[4] = FLAG;
            
            while (STOP == 0 && alarmCount <= maxAlarm) {
                unsigned char byte;

                if (alarmEnabled == FALSE) {
                   printf("Sending SET frame (attempt %d)\n", alarmCount + 1);
                   int bytes = writeBytesSerialPort(buf, 5);
                   if (bytes != 5) {
                       printf("ERROR: Failed to write SET frame\n");
                       closeSerialPort();
                       return -1;
                   }
                   printf("%d bytes written to serial port\n", bytes);

                   alarm(connectionParameters.timeout);
                   alarmEnabled = TRUE;
                }

                state = 0;
                while (alarmEnabled == TRUE && STOP == 0) {
                    int bytes = readByteSerialPort(&byte);
                    if (bytes <= 0) 
                        continue;

                    printf("Byte received = 0x%02X\n", byte);

                    switch(state) {
                        case 0: 
                            if (byte == FLAG)
                                state = 1;
                            break;
                        case 1: 
                            if (byte == ADDRESS_RT)
                                state = 2;
                            else
                                state = 0;
                            break;
                        case 2:
                            if (byte == CONTROL_UA)
                                state = 3;
                            else
                                state = 0;
                            break;
                        case 3: 
                            if (byte == (ADDRESS_RT ^ CONTROL_UA))
                                state = 4;
                            else
                                state = 0;
                            break;
                        case 4: 
                            if (byte == FLAG) {
                                printf("Received UA frame. Connection established.\n");
                                STOP = 1;
                            }
                            else
                                state = 0;
                            break;
                        default:
                            state = 0;
                            break;
                    }
                }
            }

            if (STOP == 0) {
                printf("Failed to receive UA after %d attempts. Exiting.\n", alarmCount);
                closeSerialPort();
                return -1;
            }
            
            alarm(0);
            printf("Connection established successfully as Transmitter.\n");
            return fd;
        }

        case LlRx: {
            printf("Waiting for SET frame...\n");
            
            while (STOP == 0) {
                unsigned char byte;
                
                state = 0;
                while (STOP == 0) {
                    int bytes = readByteSerialPort(&byte);
                    if (bytes <= 0) 
                        continue;

                    printf("Byte received = 0x%02X\n", byte);

                    switch(state) {
                        case 0: 
                            if (byte == FLAG)
                                state = 1;
                            break;
                        case 1:
                            if (byte == ADDRESS_TR)
                                state = 2;
                            else
                                state = 0;
                            break;
                        case 2:
                            if (byte == CONTROL_SET)
                                state = 3;
                            else
                                state = 0;
                            break;
                        case 3:
                            if (byte == (ADDRESS_TR ^ CONTROL_SET))
                                state = 4;
                            else
                                state = 0;
                            break;
                        case 4:
                            if (byte == FLAG) {
                                printf("Received SET frame. Sending UA response.\n");
                                STOP = 1;
                            }
                            else
                                state = 0;
                            break;
                        default:
                            state = 0;
                            break;
                    }
                }
            }

            buf[0] = FLAG;
            buf[1] = ADDRESS_RT;
            buf[2] = CONTROL_UA;
            buf[3] = ADDRESS_RT ^ CONTROL_UA; 
            buf[4] = FLAG;
            
            printf("Sending UA frame\n");
            int bytes = writeBytesSerialPort(buf, 5);
            if (bytes != 5) {
                printf("ERROR: Failed to write UA frame\n");
                closeSerialPort();
                return -1;
            }
            printf("%d bytes written to serial port\n", bytes);

            printf("Connection established successfully as Receiver.\n");
            return fd;
        }

        default:
            printf("ERROR: Invalid role specified\n");
            closeSerialPort();
            return -1;
    }
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose()
{
    if (connection_fd < 0) {
        printf("ERROR: No active connection to close\n");
        return -1;
    }

    unsigned char buf[BUF_SIZE] = {0};
    int STOP = 0;
    int state;
    int maxAlarm = 3;
    int timeout = 3;

    alarmEnabled = FALSE;
    alarmCount = 0;

    printf("Closing connection...\n");

    // SPRAWDŹ CZY JESTEŚMY RECEIVEREM CZY TRANSMITTEREM
    // Na razie używamy prostego sprawdzenia - jeśli otrzymaliśmy SET, jesteśmy Receiverem
    // W prawdziwej implementacji potrzebowałbyś globalnej zmiennej z rolą
    
    // Sprawdź czy mamy ramkę DISC czekającą (jesteśmy Receiverem)
    int isReceiver = FALSE;
    state = 0;
    struct timeval tv;
    tv.tv_sec = 1;  // 1 sekunda timeout
    tv.tv_usec = 0;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(connection_fd, &readfds);
    
    int retval = select(connection_fd + 1, &readfds, NULL, NULL, &tv);
    if (retval > 0) {
        unsigned char byte;
        while (readByteSerialPort(&byte) > 0) {
            printf("Checking role: Byte received = 0x%02X\n", byte);
            
            switch(state) {
                case 0: 
                    if (byte == FLAG) state = 1;
                    break;
                case 1: 
                    if (byte == ADDRESS_TR) state = 2;
                    else state = 0;
                    break;
                case 2:
                    if (byte == CONTROL_DISC) state = 3;
                    else state = 0;
                    break;
                case 3: 
                    if (byte == (ADDRESS_TR ^ CONTROL_DISC)) state = 4;
                    else state = 0;
                    break;
                case 4: 
                    if (byte == FLAG) {
                        printf("We are Receiver - received DISC from Transmitter\n");
                        isReceiver = TRUE;
                        STOP = 1;
                    }
                    else state = 0;
                    break;
                default:
                    state = 0;
                    break;
            }
            if (STOP) break;
        }
    }

    if (isReceiver) {
        // JESTEŚMY RECEIVEREM: Otrzymaliśmy DISC, wyślij DISC w odpowiedzi
        printf("Responding to DISC frame...\n");
        
        buf[0] = FLAG;
        buf[1] = ADDRESS_RT;
        buf[2] = CONTROL_DISC;
        buf[3] = ADDRESS_RT ^ CONTROL_DISC;
        buf[4] = FLAG;
        
        printf("Sending DISC response\n");
        int bytes = writeBytesSerialPort(buf, 5);
        if (bytes != 5) {
            printf("ERROR: Failed to write DISC response\n");
        } else {
            printf("%d bytes written to serial port\n", bytes);
        }
        
        // Oczekuj na UA od Transmittera
        printf("Waiting for UA from Transmitter...\n");
        state = 0;
        STOP = 0;
        alarm(timeout);
        alarmEnabled = TRUE;
        
        while (alarmEnabled == TRUE && STOP == 0) {
            unsigned char byte;
            int bytes = readByteSerialPort(&byte);
            if (bytes <= 0) 
                continue;

            printf("Byte received = 0x%02X\n", byte);

            switch(state) {
                case 0: 
                    if (byte == FLAG) state = 1;
                    break;
                case 1: 
                    if (byte == ADDRESS_TR) state = 2;
                    else state = 0;
                    break;
                case 2:
                    if (byte == CONTROL_UA) state = 3;
                    else state = 0;
                    break;
                case 3: 
                    if (byte == (ADDRESS_TR ^ CONTROL_UA)) state = 4;
                    else state = 0;
                    break;
                case 4: 
                    if (byte == FLAG) {
                        printf("Received UA from Transmitter. Closing connection.\n");
                        STOP = 1;
                    }
                    else state = 0;
                    break;
                default:
                    state = 0;
                    break;
            }
        }
        
        alarm(0);
        
        if (STOP == 0) {
            printf("Failed to receive UA from Transmitter\n");
        }
    }
    else {
        // JESTEŚMY TRANSMITTEREM: Wyślij DISC i oczekuj DISC w odpowiedzi
        printf("We are Transmitter - initiating closure\n");
        
        buf[0] = FLAG;
        buf[1] = ADDRESS_TR;
        buf[2] = CONTROL_DISC;
        buf[3] = ADDRESS_TR ^ CONTROL_DISC;
        buf[4] = FLAG;

        while (STOP == 0 && alarmCount <= maxAlarm) {
            unsigned char byte;

            if (alarmEnabled == FALSE) {
                printf("Sending DISC frame (attempt %d)\n", alarmCount + 1);
                int bytes = writeBytesSerialPort(buf, 5);
                if (bytes != 5) {
                    printf("ERROR: Failed to write DISC frame\n");
                    return -1;
                }
                printf("%d bytes written to serial port\n", bytes);

                alarm(timeout);
                alarmEnabled = TRUE;
            }

            state = 0;
            while (alarmEnabled == TRUE && STOP == 0) {
                int bytes = readByteSerialPort(&byte);
                if (bytes <= 0) 
                    continue;

                printf("Byte received = 0x%02X\n", byte);

                switch(state) {
                    case 0: 
                        if (byte == FLAG) state = 1;
                        break;
                    case 1: 
                        if (byte == ADDRESS_RT) state = 2;
                        else state = 0;
                        break;
                    case 2:
                        if (byte == CONTROL_DISC) state = 3;
                        else state = 0;
                        break;
                    case 3: 
                        if (byte == (ADDRESS_RT ^ CONTROL_DISC)) state = 4;
                        else state = 0;
                        break;
                    case 4: 
                        if (byte == FLAG) {
                            printf("Received DISC response from Receiver. Sending UA.\n");
                            STOP = 1;
                        }
                        else state = 0;
                        break;
                    default:
                        state = 0;
                        break;
                }
            }
        }

        if (STOP == 0) {
            printf("Failed to receive DISC from Receiver after %d attempts\n", alarmCount);
            alarm(0);
            closeSerialPort();
            return -1;
        }

        // Wyślij UA jako potwierdzenie
        buf[0] = FLAG;
        buf[1] = ADDRESS_TR;
        buf[2] = CONTROL_UA;
        buf[3] = ADDRESS_TR ^ CONTROL_UA;
        buf[4] = FLAG;

        printf("Sending UA acknowledgment\n");
        int bytes = writeBytesSerialPort(buf, 5);
        if (bytes != 5) {
            printf("ERROR: Failed to write UA frame\n");
        } else {
            printf("%d bytes written to serial port\n", bytes);
        }
    }

    // Zamknij port
    alarm(0);
    int result = closeSerialPort();
    connection_fd = -1;

    if (result == 0) {
        printf("Connection closed successfully\n");
    } else {
        printf("Error closing serial port\n");
        return -1;
    }

    return 0;
}