/**
 * @file pl20_read.cpp
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

#include "pl20.h"

using namespace std;
//using namespace libconfig;

bool exitSignal = false;
bool runningAsDaemon = false;
std::string processName;

static string execName;
static string ttyDeviceStr;
static int address;
static int ttyBaudrate;

Pl20 pl20("/dev/ttyUSB0", B9600);

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

int getBaudrate(int baud) {
	switch (baud) {
		case 300: return B300;
			break;
		case 1200: return B1200;
			break;
		case 2400: return B2400;
			break
		default: return B9600;
	}
}

static void showUsage(void) {
	cout << "usage:" << endl;
	cout << execName << "-aAddress -pSerialDevice -bBaudrate -h" << endl;
	cout << "a = Address to read from PL device (e.g 50)" << endl;
	cout << "s = Serial device (e.g. /dev/ttyUSB0)" << endl;
	cout << "b = Baudrate (e.g. 9600)" << endl;
	cout << "h = Display help" << endl;
}


bool parseArguments(int argc, char *argv[]) {
	char buffer[64];
	int i, buflen;
	int retval = true;
	string str;
	execName = std::string(basename(argv[0]));

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			strcpy(buffer, argv[i]);
			buflen = strlen(buffer);
			if ((buffer[0] == '-') && (buflen >=2)) {
				switch (buffer[1]) {
				case 'a':
					str = std::string(&buffer[2]);
					address = std:stoi( str );
					break;
				case 's':
					ttyDeviceStr = std::string(&buffer[2]);
					break;
				case 'b':
					str = std::string(&buffer[2]);
					ttyBaudrate = std:stoi( str );
					break;
				case 'h':
					showUsage();
					retval = false;
					break;
				default:
					log(LOG_NOTICE, "unknown parameter: %s", argv[i]);
					showUsage();
					retval = false;
					break;
				} // switch
				;
			} // if
		}  // for (i)
	}  // if (argc >1)
	// add config file extension
	
	return retval;
}


int main (int argc, char *argv[])
{
	unsigned char value;
	if ( getppid() == 1) runningAsDaemon = true;
	processName =  argv[0];

	if (! parseArguments(argc, argv) ) goto exit_fail;
	
	printf("%s %d Address: %d", ttyDeviceStr.c_str(),ttyBaudrate ,address);

	//log(LOG_INFO,"[%s] PID: %d PPID: %d", argv[0], getpid(), getppid());
	//log(LOG_INFO,"Version %d.%02d [%s] ", version_major, version_minor, build_date_str);

	//if (runningAsDaemon)
	//	signal (SIGINT, sigHandler);

	if ( pl20.read_RAM((unsigned char) address, &value) < 0)
		goto exit_fail;

	printf("Reply: %d\n", value);

	exit(EXIT_SUCCESS);

exit_fail:
	//log(LOG_INFO, "exit with error");
	exit(EXIT_FAILURE);
}
