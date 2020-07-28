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


/*********************
 *      DEFINES
 *********************/


/**********************
 *      TYPEDEFS
 **********************/

class Pl20 {
public:
	Pl20();		// empty constructor throws error
	Pl20(const char* ttyDeviceStr);
	~Pl20();
private:
	std::string _ttyDevice;
	int _tty_fd;

}
