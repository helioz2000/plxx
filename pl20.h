/**
 * @file pl20.h

-----------------------------------------------------------------------------
 This class provides encapsulation for Plasmatronics PL20 solar charge 
 controller.

-----------------------------------------------------------------------------
*/

#ifndef _PL20_H_
#define _PL20_H_

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

class Pl20 {
public:
	Pl20();		// empty constructor throws error
	Pl20(const char* ttyDeviceStr, int baud);
	~Pl20();
	int read_RAM(unsigned char address, unsigned char *readValue);
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

#endif /* _PL20_H_ */
