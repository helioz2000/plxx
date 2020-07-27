/**
 * @file pl20d.cpp
 *
 * https://github.com/helioz2000/pl20
 *
 * Author: Erwin Bejsta
 *July 2020
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
#include <syslog.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

using namespace std;
using namespace libconfig;

bool exitSignal = false;
bool runningAsDaemon = false;
std::string processName;

/** Handle OS signals
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




int main (int argc, char *argv[])
{
	if ( getppid() == 1) runningAsDaemon = true;
	processName =  argv[0];

	//if (! parseArguments(argc, argv) ) goto exit_fail;

	//log(LOG_INFO,"[%s] PID: %d PPID: %d", argv[0], getpid(), getppid());
	//log(LOG_INFO,"Version %d.%02d [%s] ", version_major, version_minor, build_date_str);

	signal (SIGINT, sigHandler);
	exit(EXIT_SUCCESS);

exit_fail:
	//log(LOG_INFO, "exit with error");
	exit(EXIT_FAILURE);
}
