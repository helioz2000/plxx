/**
 * @file pl20d.cpp
 *
 * https://github.com/helioz2000/pl20
 *
 * Author: Erwin Bejsta
 * July 2020
 */

/*********************
 *      INCLUDES
 *********************/

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <termios.h>
#include <syslog.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

using namespace std;
//using namespace libconfig;

bool exitSignal = false;
bool runningAsDaemon = false;
std::string processName;

int ttyBaud = B9600;

/* Handle OS signals
*/
void sigHandler(int signum)
{
	char signame[10];
	switch (signum) {
		case SIGTERM:
			strcpy(signame, "SIGTERM");
			break;
		case SIGHUP:
			strcpy(signame, "SIGHUP");
			break;
		case SIGINT:
			strcpy(signame, "SIGINT");
			break;

		default:
			break;
	}

	//log(LOG_INFO, "Received %s", signame);
	exitSignal = true;
}

int set_interface_attribs(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}



int main (int argc, char *argv[])
{
	if ( getppid() == 1) runningAsDaemon = true;
	processName =  argv[0];

	//if (! parseArguments(argc, argv) ) goto exit_fail;

	//log(LOG_INFO,"[%s] PID: %d PPID: %d", argv[0], getpid(), getppid());
	//log(LOG_INFO,"Version %d.%02d [%s] ", version_major, version_minor, build_date_str);

	if (runningAsDaemon)
		signal (SIGINT, sigHandler);

	string portname = "/dev/ttyUSB0";
	int fd;
	int wlen;
	int rdLen;

    fd = open(portname.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
		printf("Error opening %s: %s\n", portname.c_str(), strerror(errno));
		goto exit_fail;
    }
    /*baudrate 115200, 8 bits, no parity, 1 stop bit */
    set_interface_attribs(fd, ttyBaud);
    //set_mincount(fd, 0);                /* set to pure timed read */

    /* simple output */
	unsigned char txbuf[10];
	txbuf[0] = 20;
	txbuf[1] = 0;
	txbuf[2] = 0;
	txbuf[3] = 235;
	txbuf[2] = 192;
	txbuf[2] = 192;
	txbuf[2] = 192;
	txbuf[2] = 192;
	//printf("writing to %s\n", portname.c_str());
    wlen = write(fd, txbuf, 4);
    if (wlen != 4) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }
    tcdrain(fd);    /* delay for output */

	printf("%d bytes written\n", wlen);

	rdLen = 0;
    /* simple noncanonical input */
    do {
        unsigned char buf[80];
        int rdlen;

        rdlen = read(fd, buf, sizeof(buf) - 1);
        if (rdlen > 0) {
		//	display ASCII string
		//	buf[rdlen] = 0;
		//	printf("Read %d: \"%s\"\n", rdlen, buf);
		// display hex
			rdLen += rdlen;
            unsigned char   *p;
            printf("Read %d:\n", rdlen);
            for (p = buf; rdlen-- > 0; p++)
                printf(" 0x%x", *p);
            printf("\n");
        } else if (rdlen < 0) {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        } else {  /* rdlen == 0 */
            printf("Timeout from read\n");
        }
        /* repeat read to get full message */
    } while (rdLen < 2);

	exit(EXIT_SUCCESS);

exit_fail:
	//log(LOG_INFO, "exit with error");
	exit(EXIT_FAILURE);
}
