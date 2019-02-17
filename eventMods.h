/* 
 * File:   eventMods.h
 * Author: Jan Boen
 *
 * Created on February 15, 2019, 10:10 AM
 */

#ifndef EVENTMODS_H
#define	EVENTMODS_H

#include "GenericTypeDefs.h"

#ifdef	__cplusplus
extern "C" {
#endif

//Define & set global variables
    
extern struct io2Pin {
   unsigned char  section;
   unsigned char  io;
};

extern struct io2Pin io2Pins[];

void io2PinMapping();
WORD modifyEN(WORD);
BOOL executeAction (unsigned char, unsigned char, int);

#ifdef	__cplusplus
}
#endif

#endif	/* EVENTMODS_H */

