/**
 * @file plxx.cpp
 *
 * https://github.com/helioz2000/pl20
 *
 * Author: Erwin Bejsta
 * July 2020
 */

/*********************
 *      INCLUDES
 *********************/

#include "plxx.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <stdexcept>
#include <iostream>

using namespace std;

/* PL20 comms */
#define PL_CMD_RD_RAM 20		// Read from processor RAM
#define PL_CMD_RD_EEPROM 72	// Read from EEPROM
#define PL_CMD_WR_RAM 152		// Write to processor RAM
#define PL_CMD_WR_EEPROM 202	// Write to EEPROM
#define PL_CMD_PUSH 87		// Short push or long push
#define TTY_TIMEOUT_S 1
#define TTY_TIMEOUT_US 0

/*********************
 * MEMBER FUNCTIONS
 *********************/
 
Plxx::Plxx() {
	printf("%s\n", __func__);
	throw runtime_error("Class Pl20 - forbidden constructor");
}

Plxx::Plxx(const char* ttyDeviceStr, int baud) {
	if (ttyDeviceStr == NULL) {
		throw invalid_argument("Class Pl20 - ttyDeviceStr is NULL");
	}
	this->_ttyDevice = ttyDeviceStr;
	this->_ttyBaud = baud;
	this->_ttyFd = -1;
}

Plxx::~Plxx() {
	_tty_close();
}

int Plxx::write_RAM(unsigned char address, unsigned char writeValue) {
	struct stat sb;

	// if serial device is not open ....
	if (fstat(this->_ttyFd, &sb) != 0) {
		if (_tty_open() < 0)	// open serial device
			return -1;
	}

	if (_tty_write(address, PL_CMD_WR_RAM, writeValue) < 0)
		goto return_fail;

	// PLxx does not reply to a successful write command
//	if (_tty_read(&value) < 0)
//		goto return_fail;

	return 0;

return_fail:
	_tty_close();
	return -1;
}

/**
 * read single byte value from RAM address
 * @param address: RAM address of the requested value
 * @param readValue: pointer to a byte which will hold the value
 * @returns 0 if successful, -1 on failure
 */
int Plxx::read_RAM(unsigned char address, unsigned char *readValue) {
	unsigned char value;
	struct stat sb;

	// if serial device is not open ....
	if (fstat(this->_ttyFd, &sb) != 0) {
		// open serial device
		if (_tty_open() < 0)
			return -1;			// failed to open
	}

	if (_tty_write(address, PL_CMD_RD_RAM) < 0)
		goto return_fail;

	if (_tty_read(&value) < 0)
		goto return_fail;

	*readValue = value;
	return 0;

return_fail:
	_tty_close();
	return -1;
}

/**
 * read two bytes from RAM addresses with minimum delay between readings
 * @param lsb_addr: RAM address of the LSB byte value
 * @param msb_addr: RAM address of the MSB byte value
 * @param lsb_value: pointer to LSB value
 * @param msb_value: pointer to MSB value
 * @returns 0 if successful, -1 on failure
 *
 * Note: inconsistency between the byte values can arise as the values
 * can change between reading lsb and msb. 
 */
int Plxx::read_RAM(unsigned char lsb_addr, unsigned char msb_addr, unsigned char *lsb_value, unsigned char *msb_value) {
	uint8_t lsbVal, msbVal;
	int retVal1, retVal2;

	retVal1 = read_RAM(lsb_addr, &lsbVal);
	retVal2 = read_RAM(msb_addr, &msbVal);
	if ( (retVal1 < 0) || (retVal2 < 0) ) return -1;
	*lsb_value = lsbVal;
	*msb_value = msbVal;
	return 0;
}

int Plxx::_tty_open() {
	this->_ttyFd = open(this->_ttyDevice.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (_ttyFd < 0) {
		printf("Error opening %s: %s\n", this->_ttyDevice.c_str(), strerror(errno));
		return -1;
	}
	/*baudrate 8 bits, no parity, 1 stop bit */
	if (_tty_set_attribs(this->_ttyFd, this->_ttyBaud) < 0){
		this->_tty_close();
		return -1;
	}

	// acquire exclusive lock
	if (flock(_ttyFd, LOCK_EX | LOCK_NB) != 0) {
		printf("%s: Unable to lock %s - %d %s\n",__func__,this->_ttyDevice.c_str(), errno, strerror(errno)); 
		_tty_close(true);
		return -1;
	}
	//set_mincount(_tty_Fd, 0);                /* set to pure timed read */
	return 0;
}

void Plxx::_tty_close(bool ignoreLock) {
	if (this->_ttyFd < 0)
		return;
	if (!ignoreLock) {
		flock(_ttyFd, LOCK_UN);	// release exclusive lock
	}
	close(this->_ttyFd);
	this->_ttyFd = -1;
}


int Plxx::_tty_set_attribs(int fd, int speed)
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

int Plxx::_tty_write(unsigned char address, unsigned char cmd, unsigned char value) {
	int wrLen;
	unsigned char txbuf[10];
	txbuf[0] = cmd;
	txbuf[1] = address;
	txbuf[2] = value;			// used in write operations
	txbuf[3] = 255 - cmd;		// One's complement

	wrLen = write(this->_ttyFd, txbuf, 4);
	if (wrLen != 4) {
		printf("Error from write: %d, %d\n", wrLen, errno);
		return -1;
	}
	tcdrain(this->_ttyFd);    /* delay for output */
	return 0;
}

/**
 * read PLxx double byte response
 * @param value: pointer to read value
 */
int Plxx::_tty_read(unsigned char *value) {
	int rxlen = 0;
	int rdlen;
	unsigned char buf[80];

	fd_set rfds;
	struct timeval tv;
	int select_result;

	if (value == NULL) return -1;

	FD_ZERO(&rfds);
	FD_SET(this->_ttyFd, &rfds);
	tv.tv_sec = TTY_TIMEOUT_S;
	tv.tv_usec = TTY_TIMEOUT_US;

	select_result = select(this->_ttyFd + 1, &rfds, NULL, NULL, &tv);

	if (select_result == -1) {
		perror("select()");
		return -1;
	}
	if (!select_result) {
		perror("No data within timeout.\n");
		return -1;
	}

	do {
		rdlen = read(this->_ttyFd, buf, sizeof(buf) - 1);
		if (rdlen > 0) {
			rxlen += rdlen;
			if  (buf[0] != 200) {
				fprintf(stderr, "Error response expected:%d received:%d\n", 200, buf[0]);
				*value = 0;
				return -1;
			}
			*value = buf[1];
		} else if (rdlen < 0) {
			fprintf(stderr, "Error from read: %d: %s\n", rdlen, strerror(errno));
			return -1;
		} else {  /* rdlen == 0 */
			perror("Timeout from read\n");
			return -1;
		}
	} while (rxlen < 2);
	return 0;
}


