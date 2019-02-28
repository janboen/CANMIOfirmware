
/*
 * File:   eventMods.c
 * Author: Jan Boen
 * 
 * This is the code specific for the 1Track functionality.
 * 
 * 
 * Created on 15 Feb 2019, 10:26
 */

#include "eventMods.h"
#include "cbus1Track.h"

struct io2Pin io2Pins[4];

void io2PinMapping(){
    //To map which of the CANMIO I/O pins will be used as a mode output
    io2Pins[0].section = 0; //1
    io2Pins[0].io = 2;
    io2Pins[1].section = 1; //2
    io2Pins[1].io = 6;
    io2Pins[2].section = 2; //3
    io2Pins[2].io = 11;
    io2Pins[3].section = 3; //4
    io2Pins[3].io = 14;  
     
}

WORD modifyEN(WORD workEN){
    //Idea is simple, change the EN so it generates a code that will normally not be consumed by one of the other CANMIO
    //JMRI, or such, will use these events to update displays etc
    //if needed increment EN + 30 (28 pins)
    if ((workEN > 100) && (workEN < 200)){//Is it related to a mode output message?
        unsigned char ioPin = workEN - 100; //should provide the I/O port number between 1 and 16
        int i;
        BOOL foundEN = FALSE;
        unsigned char sectionEN = 0;
        for (i = 0; 3; i++){
            if (io2Pins[i].io == ioPin){
                foundEN = TRUE; //Matching IO section found
                sectionEN = i; //Let's store which section it is
            }
        }
        if (foundEN){//OK there is a match
            if (prein[sectionEN] == PRESENT){//and the section has been preset
                workEN = workEN + 30;//Let's change the EN so it will no trigger a preset of a neighbour
            }
        }
    }
    return workEN;
}

BOOL executeAction (unsigned char io, unsigned char ca, int action) {
    BOOL actionStatus = TRUE;
        if ((NV->spare[10] >= STDMODE) && (NV->spare[10] <= THREEMODE)){
            int i = 0;
            BOOL foundEN = FALSE;
            unsigned char sectionEN = 0;
            for (i = 0; 3; i++){
                if (io2Pins[i].io == io){
                    foundEN = TRUE; //Matching IO section found
                    sectionEN = i; //Let's store which section it is
                }
            }
            if (foundEN){//OK there is a match
                if (state[sectionEN] != 10){//and the section is NOT idle
                    actionStatus = FALSE; //We only allow an external event to activate the section if it is idle
                }
            }
        }    
    return actionStatus; 
}

    

