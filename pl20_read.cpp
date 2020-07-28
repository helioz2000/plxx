/**
 * @file pl20.cpp
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
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

using namespace std;
//using namespace libconfig;

bool exitSignal = false;
bool runningAsDaemon = false;
std::string processName;

int tty_fd;		// file descriptor for serial port

/* PL20 comms */
const int pl20Baud = B9600;
#define PL20_PORT "/dev/ttyUSB0"
#define PL20_CMD_VERSION 0	// Software Version
#define PL20_CMD_BATV 50	// Battery Voltage
#define PL20_CMD_SOLV 53	// Solar Voltage MSB
#define PL20_CMD_RSTATE 101	// regulator state

/**
 * log to console and syslog for daemon
 */
template<typename... Args> void log(int priority, const char * f, Args... args) {
	if (runningAsDaemon) {
		syslog(priority, f, args...);
	} else {
		fprintf(stderr, f, args...);
		fprintf(stderr, "\n");
	}
}


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

	log(LOG_INFO, "Received %s", signame);
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
    tty.c_cc[VMIN] = 2;			// wait for 2 bytes (std reply)
    tty.c_cc[VTIME] = 10;		// wait for 1 second

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int open_tty() {
	string portname = PL20_PORT;
	tty_fd = open(portname.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (tty_fd < 0) {
		printf("Error opening %s: %s\n", portname.c_str(), strerror(errno));
		return -1;
	}
	/*baudrate 8 bits, no parity, 1 stop bit */
	if (set_interface_attribs(tty_fd, pl20Baud) < 0){
		close(tty_fd);
		return -1;
	}
	//set_mincount(tty_fd, 0);                /* set to pure timed read */
	return 0;
}

int tty_write(unsigned char cmd) {
	int wrLen;
	unsigned char txbuf[10];
	txbuf[0] = 20;
	txbuf[1] = cmd;
	txbuf[2] = 0;
	txbuf[3] = 235;

	return 0;

	wrLen = write(tty_fd, txbuf, 4);
	if (wrLen != 4) {
		printf("Error from write: %d, %d\n", wrLen, errno);
		return -1;
	}
	tcdrain(tty_fd);    /* delay for output */
	return 0;
}

/* read PL20 reply */
int tty_read(unsigned char *value) {
	int rxlen = 0;
	int rdlen;
	unsigned char buf[80];

	fd_set rfds;
	struct timeval tv;
	int select_result;

	FD_ZERO(&rfds);
	FD_SET(tty_fd, &rfds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;

	select_result = select(tty_fd + 1, &rfds, NULL, NULL, &tv);

	if (select_result == -1) {
		perror("select()");
		return -1;
	}
	if (select_result) {
		printf("Data is available now.\n");
	} else {
		printf("No data within timeout.\n");
		return -1;
	}

	do {
		rdlen = read(tty_fd, buf, sizeof(buf) - 1);
		if (rdlen > 0) {
			rxlen += rdlen;
			if  (buf[0] != 200) {
			printf("Error response expected:%d received:%d\n", 200, buf[0]);
			return -1;
			}
			*value = buf[1];
		} else if (rdlen < 0) {
			printf("Error from read: %d: %s\n", rdlen, strerror(errno));
			return -1;
		} else {  /* rdlen == 0 */
			printf("Timeout from read\n");
			return -1;
		}
	} while (rxlen < 2);
	return 0;
}

int main (int argc, char *argv[])
{
	unsigned char value;
	if ( getppid() == 1) runningAsDaemon = true;
	processName =  argv[0];

	//if (! parseArguments(argc, argv) ) goto exit_fail;

	//log(LOG_INFO,"[%s] PID: %d PPID: %d", argv[0], getpid(), getppid());
	//log(LOG_INFO,"Version %d.%02d [%s] ", version_major, version_minor, build_date_str);

	if (runningAsDaemon)
		signal (SIGINT, sigHandler);

	if (open_tty() < 0) 
		goto exit_fail;

	if (tty_write(PL20_CMD_BATV) < 0)
		goto exit_fail;

	if (tty_read(&value) < 0)
		goto exit_fail;

	printf("Reply: %d\n", value);

	exit(EXIT_SUCCESS);

exit_fail:
	//log(LOG_INFO, "exit with error");
	exit(EXIT_FAILURE);
}
