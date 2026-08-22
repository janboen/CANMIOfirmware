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

struct io2Pin io2Pins[5];
int i;

void io2PinMapping(){
    //To map which of the CANMIO I/O pins will be used as a mode output
    io2Pins[0].section = 0; //1
    io2Pins[0].io = 2; //RC2
    io2Pins[1].section = 1; //2
    io2Pins[1].io = 6; //RC6
    io2Pins[2].section = 2; //3
    io2Pins[2].io = 11; //RB5
    io2Pins[3].section = 3; //4
    io2Pins[3].io = 14; //RA3      
    io2Pins[4].section = 4; //5, actually the extra relay
    io2Pins[4].io = 13; //RA0      
}

//Produced event or happening modification
WORD modifyEN(WORD workEN){
    //Idea is simple, change the EN so it generates a code that will normally not be consumed by one of the other CANMIO
    //JMRI, or such, will use these events to update displays etc
    //if needed increment EN + 30 (28 pins)
    if ((workEN > 100) && (workEN < 200)){//Is it related to a mode output message?
        unsigned char ioPin = workEN - 101; //should provide the I/O port number between 0 and 15
        BOOL foundEN = FALSE;
        unsigned char sectionEN = 0;
        for (i = 0; i < channels; i++){
            if (io2Pins[i].io == ioPin){
                foundEN = TRUE; //Matching IO section found
                sectionEN = i; //Let's store which section it is
            }
        }
        if (foundEN){//OK there is a match
            if (prein[sectionEN] == TRUE){//and the section has been preset
                workEN = workEN + 30;//Let's change the EN so it will not trigger a preset of a neighbour
            }
        }
    }
    return workEN;
}

//Consumed event action
//Only acts on 1T outputs, ie MODE pins, and prevents changing the MODE without passing through the logic, it uses the prein params
BOOL executeAction (unsigned char io, BOOL on) {
    BOOL actionStatus;
    actionStatus = TRUE;
        if ((NV->spare[10] >= STDMODE) && (NV->spare[10] <= XMODE)){
            BOOL foundEN = FALSE;
            unsigned char sectionEN = 0;
            for (i = 0; i < channels; i++){
                if (io2Pins[i].io == io){
                    foundEN = TRUE; //Matching IO section found
                    sectionEN = i; //Let's store which section it is
                }
            }
            if (foundEN == TRUE){//OK there is a match
                //iot get to the real meaning of an ON or OFF event we must combine with the freeMODE
                //only works properly with the same hardware version (for now))
                on = on ^ freeMODE[sectionEN];
                if (state[sectionEN] == IDLE){//Idle so also not in 2R OCC - was checking on TWORAIL and SPECIALSTATES
                    if (on == TRUE) {// ON Action
                        prein[sectionEN] = TRUE; //this is a preset for this section
                    } //moved the OFF action to separate condition as state is then not IDLE but FORCED3R
                }
                if ((state[sectionEN] == FORCED3R)||(state[sectionEN] == PRESET3R)){//Was forced or preset to 3R
                    if (on == FALSE) {// OFF Action
                        prein[sectionEN] = FALSE; //this removes the preset in for this section
                    }
                }
                actionStatus = FALSE;
            }
        }    
    return actionStatus; 
}
