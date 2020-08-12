/**
 * @file plxx.h

-----------------------------------------------------------------------------
 This class provides encapsulation for Plasmatronics PL20/40/60 solar charge 
 controller.


-----------------------------------------------------------------------------
*/

#ifndef _PLXX_H_
#define _PLXX_H_

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include <termios.h>
//#include <iostream>
#include <string>

/*********************
 *      DEFINES
 *********************/


/**********************
 *      TYPEDEFS
 **********************/


/**********************
 *      CLASS
 **********************/

class Plxx {
public:
	Plxx();		// empty constructor throws error
	Plxx(const char* ttyDeviceStr, int baud);
	~Plxx();
	int read_RAM(unsigned char address, unsigned char *readValue);
	int read_RAM(unsigned char lsb_addr, unsigned char msb_addr, int *readValue);
private:
	int _tty_open();
	void _tty_close();
	int _tty_set_attribs(int fd, int speed);
	int _tty_write(unsigned char address, unsigned char cmd);
	int _tty_read(unsigned char *value);
	
	std::string _ttyDevice;
	int _ttyBaud;
	int _ttyFd;

};

#endif /* _PLXX_H_ */
