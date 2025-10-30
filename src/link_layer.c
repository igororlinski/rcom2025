#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define BUF_SIZE 5
#define FALSE 0
#define TRUE 1
#define ESC 0x7D
#define MAX_PACKET_SIZE (MAX_PAYLOAD_SIZE + 6)

#include "link_layer.h"
#include "serial_port.h"

// Frame constants
const unsigned char FLAG = 0x7E;
const unsigned char ADDRESS_TR = 0x03;
const unsigned char ADDRESS_RT = 0x01;

// Control field constants
const unsigned char CONTROL_SET = 0x03;
const unsigned char CONTROL_UA = 0x07;
const unsigned char CONTROL_DISC = 0x0B;
const unsigned char CONTROL_RR0 = 0x05;
const unsigned char CONTROL_RR1 = 0x85;
const unsigned char CONTROL_REJ0 = 0x01;
const unsigned char CONTROL_REJ1 = 0x81;

// Helper macros
#define C_RR(s) (((s) == 0) ? CONTROL_RR0 : CONTROL_RR1)
#define C_REJ(s) (((s) == 0) ? CONTROL_REJ0 : CONTROL_REJ1)
#define C_N(s) (((s) == 0) ? 0x00 : 0x40)

// State machine for frame reception
typedef enum
{
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    READING_DATA,
    DATA_FOUND_ESC,
    BCC1_OK,
    STOP_R
} LinkLayerState;

// Global variables for link control
int alarmEnabled = FALSE;
int alarmCount = 0;
int connection_fd = -1;
int tramaTx = 0;
int tramaRx = 0;
int retransmissions = 3;
int timeout = 3;
int discReceived = 0;

// Alarm handler for timeout control
void alarmHandler(int signal)
{
    (void)signal;
    alarmEnabled = FALSE;
    alarmCount++;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
    if (fd < 0)
    {
        printf("ERROR: Cannot open serial port %s\n", connectionParameters.serialPort);
        return -1;
    }

    connection_fd = fd;
    retransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    // Set alarm handler
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = alarmHandler;
    sigaction(SIGALRM, &act, NULL);

    unsigned char buf[BUF_SIZE];
    int state = 0, STOP = 0;
    alarmEnabled = FALSE;
    alarmCount = 0;

    // ---------- TRANSMITTER ----------
    if (connectionParameters.role == LlTx)
    {
        // Build SET frame
        buf[0] = FLAG;
        buf[1] = ADDRESS_TR;
        buf[2] = CONTROL_SET;
        buf[3] = buf[1] ^ buf[2];
        buf[4] = FLAG;

        // Send SET and wait for UA
        while (!STOP && alarmCount < retransmissions)
        {
            unsigned char byte;

            if (!alarmEnabled)
            {
                printf("Sending SET frame (attempt %d)\n", alarmCount + 1);
                writeBytesSerialPort(buf, 5);
                alarm(timeout);
                alarmEnabled = TRUE;
            }

            state = 0;
            while (alarmEnabled && !STOP)
            {
                if (readByteSerialPort(&byte) <= 0)
                    continue;
                printf("Byte received = 0x%02X\n", byte);
                switch (state)
                {
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
                    if (byte == FLAG)
                    {
                        printf("Received UA frame  - connection opened\n");
                        STOP = 1;
                    }
                    else
                        state = 0;
                    break;
                }
            }
        }

        if (!STOP)
        {
            printf("Failed to receive UA, closing\n");
            closeSerialPort();
            return -1;
        }

        printf("Connection established successfully as the transmitter\n\n");
        return fd;
    }
    // ---------- RECEIVER ----------
    else
    {
        printf("Waiting for SET frame...\n");
        while (!STOP)
        {
            unsigned char byte;
            if (readByteSerialPort(&byte) <= 0)
                continue;
            printf("Byte received = 0x%02X\n", byte);
            switch (state)
            {
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
                if (byte == FLAG)
                {
                    printf("Received SET frame - sending UA response\n");
                    STOP = 1;
                }
                else
                    state = 0;
                break;
            }
        }

        // Send UA back
        buf[0] = FLAG;
        buf[1] = ADDRESS_RT;
        buf[2] = CONTROL_UA;
        buf[3] = buf[1] ^ buf[2];
        buf[4] = FLAG;
        writeBytesSerialPort(buf, 5);
        printf("Connection opened successfully as the receiver\n\n");
        return fd;
    }
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    if (connection_fd < 0)
        return -1;

    unsigned char frame[MAX_PACKET_SIZE * 2];
    int frameSize = 0;

    // Header
    frame[frameSize++] = FLAG;
    frame[frameSize++] = ADDRESS_TR;
    frame[frameSize++] = C_N(tramaTx);
    frame[frameSize++] = frame[1] ^ frame[2];

    // Compute BCC2
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++)
        BCC2 ^= buf[i];

    // Byte stuffing
    for (int i = 0; i < bufSize; i++)
    {
        if (buf[i] == FLAG || buf[i] == ESC)
        {
            frame[frameSize++] = ESC;
            frame[frameSize++] = buf[i] ^ 0x20;
        }
        else
        {
            frame[frameSize++] = buf[i];
        }
    }

    // Add BCC2
    if (BCC2 == FLAG || BCC2 == ESC)
    {
        frame[frameSize++] = ESC;
        frame[frameSize++] = BCC2 ^ 0x20;
    }
    else
    {
        frame[frameSize++] = BCC2;
    }
    frame[frameSize++] = FLAG;

    int attempts = 0, ackReceived = FALSE;

    // Send frame and wait for RR/REJ
    while (attempts < retransmissions && !ackReceived)
    {
        printf("Sending I frame (Ns=%d), attempt %d\n", tramaTx, attempts + 1);
        writeBytesSerialPort(frame, frameSize);

        struct timeval start, now;
        gettimeofday(&start, NULL);

        LinkLayerState state = START;
        unsigned char byte = 0, cField = 0;

        while (!ackReceived)
        {
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1e6;
            if (elapsed > timeout)
                break;

            if (readByteSerialPort(&byte) <= 0)
            {
                usleep(100);
                continue;
            }
            
            // Parse acknowledgment frame
            switch (state)
            {
            case START:
                if (byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;
            case FLAG_RCV:
                if (byte == ADDRESS_RT)
                    state = A_RCV;
                else
                    state = START;
                break;
            case A_RCV:
                if (byte == C_RR(0) || byte == C_RR(1) ||
                    byte == C_REJ(0) || byte == C_REJ(1))
                {
                    cField = byte;
                    state = C_RCV;
                }
                else
                {
                    state = START;
                }
                break;
            case C_RCV:
                if (byte == (ADDRESS_RT ^ cField))
                    state = BCC1_OK;
                else
                    state = START;
                break;
            case BCC1_OK:
                if (byte == FLAG)
                {
                    if (cField == C_RR((tramaTx + 1) % 2))
                    {
                        printf("Received RR%d - frame accepted\n", (tramaTx + 1) % 2);
                        tramaTx = (tramaTx + 1) % 2;
                        ackReceived = TRUE;
                    }
                    else if (cField == C_REJ(tramaTx))
                    {
                        printf("Received REJ%d - retransmitting\n", tramaTx);
                    }
                }
                else
                    state = START;
                break;

            default:
                state = START;
                break;
            }
        }

        if (!ackReceived)
        {
            attempts++;
            printf("Timeout or REJ - retrying (%d/%d)\n", attempts, retransmissions);
        }
    }

    if (!ackReceived)
    {
        printf("ERROR: Transmission failed after %d attempts\n", retransmissions);
        return -1;
    }

    return bufSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    if (connection_fd < 0)
        return -1;

    LinkLayerState state = START;
    unsigned char byte = 0, cField = 0;
    unsigned char data[MAX_PACKET_SIZE];
    int dataIndex = 0;

    // Receive and parse I frame
    while (1)
    {
        if (readByteSerialPort(&byte) <= 0)
            continue;

        switch (state)
        {
        case START:
            if (byte == FLAG)
                state = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (byte == ADDRESS_TR)
                state = A_RCV;
            else if (byte != FLAG)
                state = START;
            break;
        case A_RCV:
            if (byte == C_N(0) || byte == C_N(1))
            {
                cField = byte;
                state = C_RCV;
            }
            else if (byte == CONTROL_DISC)
            {
                printf("Received DISC - closing link\n");
                discReceived = 1;
                return 0;
            }
            else
                state = START;
            break;
        case C_RCV:
            if (byte == (ADDRESS_TR ^ cField))
                state = READING_DATA;
            else
                state = START;
            break;
        case READING_DATA:
            if (byte == ESC)
            {
                state = DATA_FOUND_ESC;
            }
            else if (byte == FLAG)
            {
                if (dataIndex < 1)
                {
                    state = START;
                    dataIndex = 0;
                    break;
                }
                
                // Check BCC2
                unsigned char receivedBCC2 = data[dataIndex - 1];
                unsigned char computedBCC2 = data[0];
                for (int i = 1; i < dataIndex - 1; i++)
                    computedBCC2 ^= data[i];

                // Valid data
                if (computedBCC2 == receivedBCC2)
                {
                    memcpy(packet, data, dataIndex - 1);
                    int packetSize = dataIndex - 1;
                    int ns = (cField >> 6) & 0x01;
                    int expectedNext = (ns + 1) % 2;

                    unsigned char rrFrame[5] = {
                        FLAG, ADDRESS_RT,
                        C_RR(expectedNext),
                        ADDRESS_RT ^ C_RR(expectedNext),
                        FLAG};
                    writeBytesSerialPort(rrFrame, 5);
                    printf("Sent RR%d acknowledgment\n", expectedNext);
                    tramaRx = expectedNext;
                    return packetSize;
                }
                // BCC2 error → send REJ
                else
                {
                    printf("BCC2 error - sending REJ%d\n", tramaRx);
                    unsigned char rejFrame[5] = {
                        FLAG, ADDRESS_RT,
                        C_REJ(tramaRx),
                        ADDRESS_RT ^ C_REJ(tramaRx),
                        FLAG};
                    writeBytesSerialPort(rejFrame, 5);
                    state = START;
                    dataIndex = 0;
                }
            }
            else
            {
                // Append data byte
                if (dataIndex < MAX_PACKET_SIZE)
                    data[dataIndex++] = byte;
                else
                {
                    printf("ERROR: Frame too long - discarded\n");
                    dataIndex = 0;
                    state = START;
                }
            }
            break;
        case DATA_FOUND_ESC:
            data[dataIndex++] = byte ^ 0x20;
            state = READING_DATA;
            break;
        default:
            state = START;
            break;
        }
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(LinkLayer connectionParameters)
{
    if (connection_fd < 0)
        return -1;

    unsigned char buf[BUF_SIZE];
    int STOP = 0, state = 0;

    printf("Closure procedure started\n");

    // ---------- TRANSMITTER ----------
    if (connectionParameters.role == LlTx)
    {
        printf("This is the transmitter - initiating closure\n");

        // Send DISC and wait for DISC response
        buf[0] = FLAG;
        buf[1] = ADDRESS_TR;
        buf[2] = CONTROL_DISC;
        buf[3] = buf[1] ^ buf[2];
        buf[4] = FLAG;

        while (!STOP && alarmCount <= retransmissions)
        {
            unsigned char byte;
            if (!alarmEnabled)
            {
                printf("Sending DISC frame (attempt %d)\n", alarmCount);
                writeBytesSerialPort(buf, 5);
                alarm(timeout);
                alarmEnabled = TRUE;
            }

            while (alarmEnabled && !STOP)
            {
                if (readByteSerialPort(&byte) <= 0)
                    continue;
                printf("Byte received = 0x%02X\n", byte);
                switch (state)
                {
                case 0:
                    if (byte == FLAG)
                        state = 1;
                    else
                        state = 0;
                    break;
                case 1:
                    if (byte == ADDRESS_RT)
                        state = 2;
                    else
                        state = 0;
                    break;
                case 2:
                    if (byte == CONTROL_DISC)
                        state = 3;
                    else
                        state = 0;
                    break;
                case 3:
                    if (byte == (ADDRESS_RT ^ CONTROL_DISC))
                        state = 4;
                    else
                        state = 0;
                    break;
                case 4:
                    if (byte == FLAG)
                    {
                        printf("Received a DISC from receiver - sending UA\n");
                        STOP = 1;
                    }
                    else
                        state = 0;
                    break;
                }
            }
        }

        // Send UA
        buf[0] = FLAG;
        buf[1] = ADDRESS_TR;
        buf[2] = CONTROL_UA;
        buf[3] = buf[1] ^ buf[2];
        buf[4] = FLAG;
        writeBytesSerialPort(buf, 5);
        printf("Sent UA acknowledgment\n");
    }
    // ---------- RECEIVER ----------
    else
    {
        printf("This is the receiver - waiting for a DISC from the transmitter\n");

        // Wait for DISC (if not already received)
        if (discReceived == 0) 
        {
            while (!STOP)
            {
                unsigned char byte;
                if (readByteSerialPort(&byte) <= 0)
                    continue;
                printf("Byte received = 0x%02X\n", byte);
                switch (state)
                {
                case 0:
                    if (byte == FLAG)
                        state = 1;
                    break;
                case 1:
                    if (byte == ADDRESS_TR)
                        state = 2;
                    break;
                case 2:
                    if (byte == CONTROL_DISC)
                        state = 3;
                    break;
                case 3:
                    if (byte == (ADDRESS_TR ^ CONTROL_DISC))
                        state = 4;
                    break;
                case 4:
                    if (byte == FLAG)
                    {
                        printf("Received DISC from the transmitter - responding with a DISC\n");
                        STOP = 1;
                    }
                    else
                        state = 0;
                    break;
                }
            }
        }
        else {
            printf("Disc already received during reading\n");
        }

        // Send DISC back
        buf[0] = FLAG;
        buf[1] = ADDRESS_RT;
        buf[2] = CONTROL_DISC;
        buf[3] = buf[1] ^ buf[2];
        buf[4] = FLAG;
        writeBytesSerialPort(buf, 5);
        printf("Sending a DISC response\n");
    }

    // Close port
    closeSerialPort();
    connection_fd = -1;
    printf("Connection closed successfully\n");
    return 0;
}