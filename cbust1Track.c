/*
 * File:   cbus1Track.c
 * Author: Jan Boen
 * 
 * This is the code specific for the 1Track functionality.
 * In essence it adds 4 components to the standard CANMIO
 * 1- The core logic to handle the 1Track logic which allows using 3-rail tracks to simultaneously operate
 *      DCC/digital 2-rail rolling stock and DCC/digital 3-rail rolling stock in any section of track
 *      where 3-rail is typically Märklin and 2-rail most other H0 vendors. 
 * 2- Allow the modification of an produced events Event Number
 * 3- Allow not taking action on a consumed ACON event
 * 4- Introduces an extra config option using NV spare 10 so the appropriate 1Track operating mode can be set
 *      If the spare 10 value is outside the range of 0x81 - 0x83 then the node will operate like a standard CANMIO
 * 
 * Features 2 and 3 should be generic as well as 4, the use of spare 10, so that anyone who wishes to run local logic can
 * leverage the extra features with little effort. This is also why the extra class of eventMods. and it's header file have been created.
 * 
 * Created on 9 Feb 2019, 16:26
 */
#include "cbus1Track.h"

// To Do
// Test test and test...
// This will influence the message that will be produced

//Define & set global variables
TickValue nowTime;
unsigned char countio = 0;
unsigned char count1T[4] =  {0, 0, 0, 0};
char maxlostocc = 8; //x 125 ms - consecutive maximum number that sense may be lost in 3R mode before going back to 2R mode
char maxshort = 30;  //maximal number short must be seen before transiting to 3R mode - higher than in PIC as code runs faster. I think...
unsigned char rlsense = 0;
unsigned char rloop = 0; //holds the current switch/case state value of the reversing loop
unsigned char timeio = 0;
unsigned char sectionTime[4] = {0, 0, 0, 0};
unsigned char stateio = 0; //holds the current switch/case state value of a section
unsigned char state[4] = {0, 0, 0, 0};
unsigned char lostoccio = 0;
unsigned char lostocc[4] = {0, 0, 0, 0};
BOOL passio = 0;
BOOL pass[4] = {0, 0, 0, 0};
BOOL occupied = ABSENT;
BOOL senseio = ABSENT;
BOOL modeio = 0;
BOOL preinio = ABSENT;
BOOL prein[4] = {ABSENT, ABSENT, ABSENT, ABSENT};
BOOL rlstate = 0;
BOOL occ[4] = {0, 0, 0, 0}; //IN
BOOL sense[4] = {0, 0, 0, 0}; //IN
BOOL mode[4] = {0, 0, 0, 0}; //OUT
unsigned char softprein[4] = {0, 0, 0, 0}; //OUT
unsigned char forced[4] = {0, 0, 0, 0};
unsigned char forcedcount[4] = {0, 0, 0, 0};
unsigned char forcedio = 0;
unsigned char forcedcountio = 0;
unsigned char usepreinio = 0; //Set to 1 once pull-up resistors are in place, this will enable the prein
unsigned char useforced = 1;  //Set to 1 to enable the recovery logic when a section was forced from state 20 to 30
//unsigned char dev = 0;
unsigned char trackMode = 0; //Case variable for holding the software mode, has to be read from memory and external pins
unsigned char tmrbit = 0;
unsigned char tic = 0;
unsigned char tac = 1;

/* New code CBUS protocol and MERG CANMIO pic based hardware
The hardware input for pre in is dropped as well as the pre out outputs as this board only has 16 usable pins.

Software designed for PIC18F26K80, should also run on PIC18F25K80

Occupied = either 2R using current or 3R occupied voltage the old hardware generates 1 single signal for both

Sense = a short circuit indication (i.e. too much current is consumed)
Occupied = either 2R or 3R occupied detector using current
Possible valid states:
sense occ  meaning
0      X   short circuit
1      0   busy
1      1   Idle/PoT

Mapping CANMIO  Pin   Function
See cbus1Track.h

Reverse Loop
State can be uncertain (0), via S1 (1) or via S3 (3)
Uncertain state exists when S2 is occupied and when no train entered via S1 or S3
This can happen during a power cycle/outage
In no case shall all 3 sections be occupied at the same time when starting the system
The 3 sections will provide a stable input for the reversing loop code

Define & set global variables
See cbus1Track.h
*/

unsigned char ticTac(void){
    //From the timer bits read the second byte first bit so use a mask of 0x01
    //This will flip every 16,384 ms as it has to pass 1024 ticks at 16 us per tick
    
    nowTime.Val = tickGet();
    tmrbit = nowTime.byte.b1 && 0x01; // this should neatly produce 0 or 1 as the lowest bit is only returned. 
    //If want to use other bits then must also use bitwise operations
	if (tmrbit == tic) {
        tic = !tic;
    }
    return tic;
}

void getTrackMode(void){
    
    if ((NV->spare[10] != RLMODE) && (NV->spare[10] != THREEMODE)){
        trackMode = 0;        
    }
    if (NV->spare[10] == RLMODE){
        trackMode = 1;
    }
    if (NV->spare[10] == THREEMODE){
        trackMode = 2;        
    }    
}

//Reverse Loop Logic
void reverseLoop(void) {
    // Reverse Loop
    // state can be uncertain (0), via S1 (1) or via S3 (3)
    // Uncertain state exists when S2 is occupied and when no train entered via S1 or S3
    // This can happen during a power cycle/outage
    // In no case shall all 3 sections be occupied at the same time when starting the system
    // The 3 sections will provide a stable input for the reversing loop code
    stateio = 99; // Force state to 99 to show RL is being used
    if (rlstate == 0) { //Take snapshot of usage while in idle state
        countio = 0; // reset counter
        if (state[0] == 20) { // Via S1
            rlsense = 1;
            modeio = 0;
            rlstate = 1;
        }
        if (state[2] == 20) { // Via S3
            rlsense = 3;
            modeio = 1;
            rlstate = 1;
        }
        if ((state[1] == 20) && (rlsense == 0)) { // Uncertain. Will assume Via S3
            rlsense = 3;
            modeio = 1;
            rlstate = 1;
        }
        //The following lines might eventually be removed by properly handling produced events
        /*if (preout[1] == PRESENT) { // S2 is in 3-rail mode so preset S3
            softprein[2] = PRESENT;
        } else { // S2 is not in 3-rail mode so don't preset S3
            softprein[2] = ABSENT;
        }
        if (preout[2] == PRESENT) { // S3 is in 3-rail mode so preset S2
            softprein[1] = PRESENT;
        } else { // S3 is not in 3-rail mode so don't preset S2
            softprein[1] = ABSENT;
        }*/
    }       
    if (rlstate == 1) { // Will use the determined sensio as long as occupied
        if ((rlsense == 1) && (state[1] = 20)) { // Via S1 - change mode
            modeio = 1;
            countio = 0; // reset counter
        }
        if ((rlsense == 3) && (state[0] = 20)) { // Via S3 - change mode
            modeio = 0;
            countio = 0; // reset counter
        }
        if ((state[0] == 10) && (state[1] == 10) && (state[2] == 10)) { // reverse loop is probably free
            if (countio >= ONEEIGHTSEC) { // waited long enough
                rlstate = 0; // reverse loop idle
                modeio = 0; // relay switched to via S1
                rlsense = 0; // sensio set to uncertain
                countio = 0; // reset counter
            }
        } else {
            countio = 0; // reset counter
        }
    }
}

void trackCoreLogic(){ //One invocation of this method will handle the 4 channels
    
    //Define & set local variables
	char i = 0;
	char j = 0;
    int a = 0;
    
    //Latch the values of the I/O ports into variables
    occ[0] = OCC1;
    occ[1] = OCC2;
    occ[2] = OCC3;
    occ[3] = OCC4;
    
    sense[0] = SHORT1;
    sense[1] = SHORT2;
    sense[2] = SHORT3;
    sense[3] = SHORT4;

    mode[0] = MODE1;
    mode[1] = MODE2;
    mode[2] = MODE3;
    mode[3] = MODE4;

    //This is the tic/tac logic we will use for timing below each tic/tac is 16,384 ms
    tic = ticTac();
    if (tic == tac) {//this section timer counter
		count1T[0] = count1T[0] + 1;
		count1T[1] = count1T[1] + 1;
		count1T[2] = count1T[2] + 1;
		count1T[3] = count1T[3] + 1;
		tac = !tac;
	}
    
    //Check the track mode. Normally this shouldn't change frequently during normal operations
    getTrackMode();
    
	//For each channel execute below
	for (j = 0; 3; j++){
		//Get values
        countio = count1T[j];
		occupied = occ[j];
		senseio = sense[j];
		modeio = mode[j];
        preinio = prein[j];
		timeio = sectionTime[j];
		stateio = state[j];
		lostoccio = lostocc[j];
		passio = pass[j];
		//Check current section
		//Possible valid states:
		//Sense Occ     meaning
		//Pres   X      Short circuit
		//Abs    Pres   Busy
		//Abs    Abs    Idle/PoT
		switch (trackMode) {
			case 0: //standard mode
            #ifdef DEV1 !! DEV2
				preinio = ABSENT;  //Force preset value to zero so it is not taken into account during debugging
            #endif
			switch (stateio) {
				case 10:  //Idle mode
					if (senseio == PRESENT) { //This section is possibly shortened
						modeio = 1;  //set relay to 3R
						stateio = 23;  //must go to transition mode
                        countio = 0; //reset counter
					} else { //section safe so check if
                        if (occupied == PRESENT) { //section occupied
                            if (countio >= ONEEIGHTSEC) { //waited long enough
                                countio = 0; //reset counter
                            }
                            if (sense == PRESENT) {
                                stateio = 20;  //Looks normal so let's go to 2R
                            } else {
                                stateio = 23; //Hmm, also shortened so let's go 3R instead
                            }
                        } else {//section free
                            if (preinio == PRESENT) { //preset received
                                    modeio = 1;  //set relay to 3R
                                    stateio = 30;  //set 3R
                            }
                            countio = 0;
                        }
                    }
                    forcedio = 0; //not forced to 30 from 20
                    forcedcountio = 0; //reset the forced counter                   
				break;
				case 20:  //2R occupied mode
                    if ((occupied == ABSENT) && (senseio == ABSENT)) { //section free
                        if (countio >= ONEEIGHTSEC) { //waited long enough
                            stateio = 10; //set Idle state
                            forcedio = 0; //Not forced from 20 to 30
                        } 
                    }else {
                        countio = 0; //reset counter
                    }
                    if (sense == PRESENT) { //possible short circuit
                        modeio = 1; //just to be safe toggle relay &
                        stateio = 23; //switch to 3R transition mode
                        forcedio = 1; //Being forced from 20 to 30
                    }
				break;
				case 23:  //3R Transition mode
                    if (countio >= ONEEIGHTSEC) { //waited long enough
                        stateio = 30; //3R mode
                        countio = 0;//reset counter
                    }
				break;
				case 30:  //3R mode, occupied signal which combines current consumption and voltage presence
                    if (senseio == ABSENT) { //3R normal status
                        if (occupied == ABSENT) { //looks like the section might be free
                            stateio = 88; //3R occupied lost
                        } else { //still occupied
                            //preoutio = PRESENT; //set preset out
                        }    
                    } else { //3R short cut status
                        //This can't be resolved by 1Track at this stage
                        //preoutio = ABSENT; //will remove preout setting while in short circuited
                    } 
                    if ((useforced == 1) && (forcedio == 1)) { //Will try to recover from being forced in to state 30 from 20
                            if (countio >= ONEEIGHTSEC) { //Timer
                                countio = 0;  //reset counter
                                forcedcountio = forcedcountio + 1;
                            }
                            if (forcedcountio > FOURSEC) { //Waited long enough so let's try
                                forcedcountio = 0; //reset the forced counter
                                modeio = 0; //release relay
                                stateio = 20; //set state 20
                                countio = 0; //reset counter
                            }
                    } else {
                        countio = 0; //reset counter
                    }
				break;
				case 88:  //3R occupied lost
                    if (preinio == ABSENT) { //preset is not PRESENT
                        if (occupied == ABSENT) { //3R occupied lost
                            if (countio >= ONEEIGHTSEC) { //waited long enough
                                countio = 0; //reset counter
                                lostoccio = lostoccio + 1;
                                stateio = 88; //stay in occupied lost mode
                            }
                        } else { //3R occupied PRESENT
                            lostoccio = 0;
                            stateio = 30; //occupied lost mode ended
                            //preoutio = PRESENT; //set preset out
                        }
                        if (lostoccio >= maxlostocc) { //go to idle mode
                            lostoccio = 0;
                            stateio = 10;
                            countio = 0; //reset counter
                            modeio = 0; //toggle relay to 2R mode
                            //preoutio = ABSENT; //clear preset out
                        }
                    } else { //preset is PRESENT
                        stateio = 30; //occupied lost mode ended
                        //preoutio = ABSENT; //clear preset out as we stay in 3R mode due to preset
                    }
				break;
				//return values
				if (mode[j] != modeio){
                    mode[j] = modeio;	//Set mode out if changed. To avoid rattling relay
                    //Must also generate a mode message to indicate a mode change
                    //Can be 3 different messages: 2R, 3R and 3R forced by preset
                }
                //Must also generate a occupancy message when state changes
                //Could probably be a generic message as any normal occupancy detector would do
                //Double check with Luc
                
				//if (preout[j] != preoutio) {preout[j] = preoutio;}
				if (prein[j] != preinio) {prein[j] = preinio;}
                count1T[j] = countio;
				state[j] = stateio;
				lostocc[j] = lostoccio;
				pass[j] = passio;
                forced[j] = forcedio;
                forcedcount[j] = forcedcountio;
				//Check if reverseloop is needed
				if ((rloop == 1) && (j == 3)){//For now only section 3 as reverseloop detector and section 4 then used for reversing the loop
					reverseLoop();
				}
			}
			break;
			case 1: //reverseloop mode
				rloop = 1;
			break;
			case 2: //Permanent 3R
				//Just set the relay to 3R mode
				for (a = 0; 3; a++){
					if (mode[a] != 1) {
					mode[a] = 1;
					state[a] = 30;
                    }
                }
			break;
		}
	}
}


