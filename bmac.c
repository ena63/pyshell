/**
 * @file serial_comm.c
 * @author Noe Serres / Midi Ingenierie
 * @version 1.0
 * @date 14/02/2025
 * @brief Serial communication for sending commands and receiving responses.
 * 
 * https://www.midi-ingenierie.com/documentation/ressources/notes_application/Syntaxe-et-communication-calculateur.pdf
 * 
 * Command:	STX T1 T2 T3 A1 A2 X X X X C1 C2 ETX
 * Response:	ACK XETAT STX T1 T2 T3 A1 A2 X X X X C1 C2 ETX XON
 * NoResponse:	ACK XETAT XON
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>


// Global debug mode flag
int debug_mode = 0;

// Macro dbg_printf
#define dbg_printf(...) do { if (debug_mode) printf(__VA_ARGS__); } while (0)

// Special character definitions
#define STX 0x02
#define ETX 0x03
#define ACK 0x06
#define XOFFerror 0x18
#define XON 0x1A

/**
 * @brief Displays the program usage instructions.
 * 
 * @param progname Name of the program (argv[0]).
 */
void usage(const char *progname) {
    printf("Usage: %s [-d|--debug] [-p port] [-b baudrate] [-a address]\n", progname);
    printf("Options:\n");
    printf("  -d, --debug        Enable debug mode\n");
    printf("  -p, --port         Serial port name (default: /dev/ttyS0)\n");
    printf("  -b, --baudrate     Baud rate (default: 115200)\n");
    printf("  -a, --address      Module address (default: 0)\n");
    printf("  -h, --help         Show this help message\n");
}

/**
 * @brief Initializes and configures the serial port.
 * 
 * @param port The name of the serial port.
 * @param baudrate The baud rate for communication.
 * @return File descriptor of the opened serial port, or -1 on error.
 */
int init_serial(const char *port, int baudrate) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Error opening serial port");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error getting port attributes");
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, baudrate);
    cfsetispeed(&tty, baudrate);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 data bits
    tty.c_iflag &= ~IGNBRK;                     // Disable break processing
    tty.c_lflag = 0;                            // No canonical mode or echo
    tty.c_oflag = 0;                            // No output processing
    tty.c_cc[VMIN] = 1;                         // Read blocking until 1 byte
    tty.c_cc[VTIME] = 5;                        // Read timeout (0.5 sec)

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);     // No flow control
    tty.c_cflag |= (CLOCAL | CREAD);            // Enable receiver and set local mode
    tty.c_cflag &= ~(PARENB | PARODD);          // No parity
    tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                    // No hardware flow control

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error setting port attributes");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * @brief Computes the checksum for a given command string.
 * 
 * @param command The command string.
 * @param len The length of the command string.
 * @return The computed checksum as an unsigned char.
 */
unsigned char calculate_checksum(const char *command, int len) {
    unsigned char checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum += (unsigned char)(*command++);
    }
    return checksum % 256;
}

/**
 * @brief Sends a command to the device through the serial port.
 * 
 * @param fd File descriptor of the serial port.
 * @param command The command string to send.
 * @param address The address of the target module.
 * @return 0 on success, -1 on error.
 */
int send_command(int fd, const char *command, int address) {
    char formatted_command[256];
    unsigned char checksum;
    char final_command[256];
    int written;

    // Add address prefix to the command
    sprintf(formatted_command, "%02d%s", address, command);

    // Compute the checksum
    checksum = calculate_checksum(formatted_command, strlen(formatted_command));

    // Add STX, command length, checksum, and ETX
    sprintf(final_command, "%c%03u%s%02X%c", STX, strlen(formatted_command), formatted_command, checksum, ETX);

    // Write the command to the serial port
    written = write(fd, final_command, strlen(final_command));
    if (written < 0) {
        perror("Error sending command");
        return -1;
    }

    dbg_printf("Sent %d chars: %s\n", written, final_command);
    return 0;
}

/**
 * @brief Reads and processes the response from the device.
 * 
 * @param fd File descriptor of the serial port.
 * @param buffer Buffer to store the processed response.
 * @return Length of the processed response on success, -1 on error.
 */
int read_response(int fd, char *buffer) {
    char raw_response[256];
    int n = read(fd, raw_response, sizeof(raw_response) - 1);
    if (n <= 0) {
        perror("Error reading serial port");
        return -1;
    }

    raw_response[n] = '\0'; // Null-terminate the raw response
    dbg_printf("Read %d chars: %s\n", n, raw_response);

    // Analyze the response for protocol characters
    if (strchr(raw_response, ACK) == NULL) { // Missing ACK
        sprintf(buffer, "Missing ACK");
        return -1;
    }

    if (strchr(raw_response, XOFFerror) != NULL) { // XOFFerror detected
        sprintf(buffer, "Syntax error");
        return -1;
    }

    if (strchr(raw_response, XON) == NULL) { // Missing XON
        sprintf(buffer, "Missing XON");
        return -1;
    }

    // Valid response
    if (raw_response[0] == ACK && raw_response[n - 1] == XON) {
        int content_len;
        int checksum1;
        int checksum2;

        if (raw_response[2] == STX && raw_response[n - 2] == ETX) { // Command with response
            content_len = (raw_response[3] - '0') * 100 + (raw_response[4] - '0') * 10 + (raw_response[5] - '0');
            checksum1 = calculate_checksum(&raw_response[6], content_len);
            sscanf(&raw_response[n - 4], "%2X", &checksum2);

            if (checksum1 != checksum2) {
                sprintf(buffer, "Bad checksum: expected %02X received %02X", checksum1, checksum2);
                return -1;
            }

            strncpy(buffer, raw_response + 6, content_len);
            buffer[content_len] = '\0'; // Null-terminate the string
        } else { // Command without response
            sprintf(buffer, "OK");
        }
        return content_len;
    }

    sprintf(buffer, "Protocol error");
    return -1;
}

/**
 * @brief Main function for serial communication handling.
 * 
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char *argv[]) 
{
    const char *port_name = "/dev/ttyUSB0"; // Remplacez par le port approprié
    int baudrate = B115200;
    int address = 0;
    char buffer[256];
    char command[256];
    int fd;

    // Analyse des arguments de la ligne de commande
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port_name = argv[++i];
        } else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--baudrate") == 0) && i + 1 < argc) {
            int baud = atoi(argv[++i]);
            switch(baud){
				case 9600:		baudrate=B9600;		break;
				case 19200:		baudrate=B19200;	break;
				case 38400:		baudrate=B38400;	break;
				case 57600:		baudrate=B57600;	break;
				case 115200:	baudrate=B115200;	break;
				default:		printf("unsupported baudrate\n"); return 1;
			}
        } else if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--address") == 0) && i + 1 < argc) {
            address = atoi(argv[++i]);
            if(address<0 || address>127){
				printf("invalid address\n"); 
				return 1;
			}
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option : %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    // Affichage des paramètres si le mode débogage est activé
    dbg_printf("Port série : %s\n", port_name);
    dbg_printf("Baudrate : %d\n", baudrate);
    dbg_printf("Adresse : %d\n", address);
 
    // Initialisation du port série
    fd = init_serial(port_name, baudrate);
    if (fd < 0) 
	{
        return 1;
    }
 
    printf("╔════════════════════════════╗\n");
    printf("║        BMAC-SHELL          ║\n");
    printf("║    ©Midi Ingenierie 2025   ║\n");
    printf("╚════════════════════════════╝\n");
 
    while (1) {
        printf("->>");
        if (fgets(command, sizeof(command), stdin) == NULL) 
		{
            break;
        }
 
        // Retirer le caractère de nouvelle ligne
        command[strcspn(command, "\n")] = '\0';
 
        if (strcmp(command, "quit") == 0) 
		{
            break;
        }
 
		//flush du buffer (facultatif)
		if(tcflush(fd, TCIOFLUSH) != 0)
		{
			perror("UART buffer flush error");
			continue;
		}

		/* Sample commands: 
		 * MOVE_SPEED 20000
		 * STOP
		 * #OUTPUT.3:=1
		 * #V12:=1234
		 * READ #SUPPLY_VOLTAGE
		 * READ #V12
		 * refer to user manual for a complete list of commands
		 */
        if (send_command(fd, command, address) == 0) 
		{
            int n = read_response(fd, buffer);
            if (n > 0) 
			{
                printf("   %s\n", buffer);
            }
            else 
			{
				printf("Error : %s\n", buffer);
			}
        }
    }
 
    close(fd);
    return 0;
}


