// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

const char FLAG = 0x7E;
const char ADDRESS = 0x03;
const char CONTROL = 0x07;
const char BCC1 = ADDRESS ^ CONTROL;
int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d received\n", alarmCount);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    unsigned char buf[BUF_SIZE] = {0};

    buf[0] = FLAG;
    buf[1] = ADDRESS;
    buf[2] = CONTROL;
    buf[3] = BCC1;
    buf[4] = FLAG;
    
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    int STOP = 0;
    int maxAlarm = 3;
    int nBytesBuf = 0;
    int state = 0;


    //state = 0 - start
    //state = 1 - flagBegin
    //state = 2 - address
    //state = 3 - control
    //state = 4 - bcc1

    alarmEnabled = 1;
    
    while (STOP == 0 && alarmCount < maxAlarm) {
        unsigned char byte;

        if (alarmEnabled == FALSE) {
           printf("Sending SET frame (attempt %d)\n", alarmCount + 1);
           int bytes = writeBytesSerialPort(buf, 5);
           printf("%d bytes written to serial port\n", bytes);

           alarm(3);
           alarmEnabled = TRUE;
        }

        int state = 0;
        while (alarmEnabled == TRUE && STOP == 0) {
            int bytes = readByteSerialPort(&byte);
            if (bytes == 0) 
                continue;

            printf("Byte received = 0x%02X\n", byte);

            switch(state) {
                case 0:
                    if (byte == 0x7E)
                        state = 1;
                    break;
                case 1:
                    if (byte == 0x03)
                        state = 2;
                    else
                        state = 0;
                    break;
                case 2:
                    if (byte == 0x03)
                        state = 3;
                    else
                        state = 0;
                    break;
                case 3:
                    if (byte == 0x00)
                        state = 4;
                    else
                        state = 0;
                    break;
                case 4:
                    if (byte == 0x7E)
                            printf("Received FLAG. Stop reading from serial port.\n");
                            STOP = 1;
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
        return 1;
    }
    else {
        printf("Connection established successfully.\n");
        return 0;
    }
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose()
{
    // TODO: Implement this function

    return 0;
}
