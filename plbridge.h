/**
 * @file plbridge.h
 *
 */

#ifndef PLBRIDGE_H
#define PLBRIDGE_H

//#include <time.h>

struct updatecycle {
	int	ident;
	int interval;	// seconds
	int *tagArray = NULL;
	int tagArraySize = 0;
	time_t nextUpdateTime;			// next update time 
};


#endif /* PLBRIDGE_H */
