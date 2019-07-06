/*
 * File:   cbus1Track.c
 * Author: Jan Boen
 *
 * This is the code specific for the 1Track functionality.
 * In essence it adds 4 components to the standard CANMIO
 * 1- The core logic to handle the 1Track logic which allows using 3-rail tracks to simultaneously operate
 *      DCC/digital 2-rail rolling stock and DCC/digital 3-rail rolling stock in any section of track
 *      where 3-rail is typically Märklin and 2-rail most other H0 vendors.
 * 2- Allow the modification of a produced event's Event Number
 * 3- Allow not taking action on a consumed ACON event
 * 4- Introduces an extra config option using NV spare 10 so the appropriate 1Track operating mode can be set
 *      If the spare 10 value is outside the range of 0x81 - 0x83 then the node will operate like a standard CANMIO
 *
 * Features 2 and 3 should be generic as well as 4, the use of spare 10, so that anyone who wishes to run local logic can
 * leverage the extra features with little effort. This is also why the extra class of eventMods and it's header file have been created.
 *
 * Created on 9 Feb 2019, 16:26
 * Major update from 19 June 2019
 * Added feature to support a fixed cross (vast kruis), removed pre in, pre out functionality.
 * Dropped fixed 3R mode and use this setting for fixed cross
 * Pre in will be used for the future Occ3R changes
 * Fixed cross will use a separate channel with relay and use channel 2 and 3 as feeder channels
 *
 * To support different hardware versions of the IO board the following is added
 * 2 bits where
 * 00 = v0 - the first prototype version
 * 01 = v1 - the current prototype version (June 2019) - IO works the other way around from previous version
 */
#include "cbus1Track.h"

// To Do
// Test test and test...
// This will influence the message that will be produced

//Define & set global variables
unsigned short timerCount;
unsigned char channels;
unsigned char xInitialised;
unsigned char secnew[4] =  {0, 0, 0, 0}; //zero implies v0 Track IO HW, current version is v1, increases by 1 for each version with different IO behaviour
unsigned char secnewio;
TickValue lastTime;
TickValue nowTime;
unsigned char channelCounter;
unsigned char countio;
unsigned char count1T[4];
char maxlostocc;
char maxshort;
unsigned char rlsense;
unsigned char rloop;
unsigned char timeio;
unsigned char sectionTime[4];
unsigned char stateio;
unsigned char state[4];
unsigned char lostoccio;
unsigned char lostocc[4];
BOOL free[4];
BOOL passio;
BOOL pass[4];
BOOL occio;
BOOL senseio;
BOOL modeio;
BOOL preinio;
BOOL prein[4];
BOOL preoutio; //Only used with reverse loop solution
BOOL preout[4]; //Only used with reverse loop solution
BOOL rlstate; //Only used with reverse loop solution
BOOL occ[4];
BOOL sense[4];
BOOL mode[4];
BOOL softprein[4]; //Only used with reverse loop solution
BOOL forced[4];
unsigned char forcedcount[4];
BOOL forcedio;
unsigned char forcedcountio;
//unsigned char useforced;
unsigned char trackMode;
BOOL tmrbit;
BOOL tic;
BOOL tac;

/* New code CBUS protocol and MERG CANMIO pic based hardware
The hardware input for pre in is dropped as well as the pre out outputs as this board only has 16 usable pins.
These hardware pins are replaced by CBUS messages and associated logic
Software designed for PIC18F26K80

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

void getSecnew(void){
    //Get 1Track IO HW version
    //Use NV->spare[9] get 4 pairs of 2 bits which will indicate the hardware version of the 1Track IO
    unsigned char spare9 = NV->spare[9];
    for (channelCounter = 0; channelCounter < 4; channelCounter++){
        secnew[channelCounter] = spare9 & SECNEWMASK;
        //One secnew line required for each different hardware version
        if (secnew[channelCounter] == 0){free[channelCounter]= 0;}
        if (secnew[channelCounter] == 1){free[channelCounter]= 1;}        
        spare9 = spare9 >> 2;
    }
    
}

void init1TrackVars(void){
    unsigned char c;
    nowTime.Val = tickGet();
    channelCounter = 0;
    secnewio = 0;
    countio = 0;
    xInitialised = 0;
    getSecnew(); //Get 1Track IO HW version
    for (c = 0; c < 4; c++){
        count1T[c] =  0;
        sectionTime[c] = 0;
       if (secnew[c] == 0){//for v0 hardware from 2018
            state[c] = 10;// stores the state value of a section
        }
       if (secnew[c] == 1){//for v1 hardware from June 2019
            state[c] = 30;// stores the state value of a section
        }
        lostocc[c] = 0;
        pass[c] = 0;
        prein[c] = FALSE;
        occ[c] = 0; //IN
        sense[c] = 0; //IN
        preout[c] = FALSE;
        softprein[c] = FALSE; //OUT
        forced[c] = FALSE;
        forcedcount[c] = 0;
    }
    #ifdef DEV0
        maxlostocc = 8; // x 125 ms - consecutive maximum number that sense may be lost in 3R mode before going back to 2R mode
        maxshort = 30;  // maximal number short must be seen before transiting to 3R mode - higher than in PIC as code runs faster. I think...
    #endif
    #ifndef DEV0 //Simulation mode with shorter timings
        maxlostocc = 1; // x 16 ms - consecutive maximum number that sense may be lost in 3R mode before going back to 2R mode
        maxshort = 2;  // maximal number short must be seen before transiting to 3R mode - higher than in PIC as code runs faster. I think...
    #endif
    rlsense = 0;
    rloop = 0; //holds the current switch/case state value of the reversing loop
    timeio = 0;
    stateio = 0; //holds the current switch/case state value of a section
    lostoccio = 0;
    passio = 0;
    occio = ABSENT;
    senseio = ABSENT;
    modeio = 0;
    preinio = FALSE;
    rlstate = 0;
    forcedio = FALSE;
    forcedcountio = 0;
    trackMode = 0; //Case variable for holding the software mode, has to be read from memory
    tmrbit = 0;
    tic = 1;
    tac = 0;
}

unsigned char ticTac(void){
    lastTime.Val= nowTime.Val;
    nowTime.Val = tickGet();
	if (nowTime.Val > lastTime.Val) {////Normal situation
        timerCount = timerCount + (nowTime.Val - lastTime.Val);    
    }// We don't do anything when timer overruns, this implies a max 50ms extra in timing and that is just fine
	if (timerCount >= FIFTYMILLIS) {//This will flip every 50 ms
        tic = !tic;
        timerCount = 0;
    }
    return tic;
}

void getTrackMode(void){

    if ((NV->spare[10] != RLMODE) && (NV->spare[10] != XMODE)){
        trackMode = STDMODE;
    }
    if (NV->spare[10] == RLMODE){
        trackMode = RLMODE;
    }
    if (NV->spare[10] == XMODE){
        trackMode = XMODE;
    }
}

void set1TrackPorts(void){//only needed for debugging/simulation purposes

    //Set the proper ports to outputs, default is input
    if (TRISCbits.TRISC2 != 0) {TRISCbits.TRISC2 = 0;}
    if (TRISCbits.TRISC6 != 0) {TRISCbits.TRISC6 = 0;}
    if (TRISBbits.TRISB5 != 0) {TRISBbits.TRISB5 = 0;}
    if (TRISAbits.TRISA3 != 0) {TRISAbits.TRISA3 = 0;}

    //Set all input pins to high as the logic is active low
    PORTCbits.RC0 = 0;
    PORTCbits.RC1 = 0;
    PORTCbits.RC3 = 0;
    PORTCbits.RC4 = 0;
    PORTCbits.RC5 = 0;
    PORTCbits.RC7 = 0;
    PORTBbits.RB0 = 0;
    PORTBbits.RB1 = 0;
    PORTBbits.RB4 = 0;
    PORTAbits.RA1 = 0;
    PORTAbits.RA0 = 0;
    PORTAbits.RA5 = 0;
}

//Reverse Loop Logic
void reverseLoop(void) {
    // Reverse Loop
    // state can be uncertain (0), via S1 (1) or via S3 (3)
    // Uncertain state exists when S2 is occupied and when no train entered via S1 or S3
    // This can happen during a power cycle/outage
    // In no case shall all 3 sections be occupied at the same time when starting the system
    // The 3 sections will provide a stable input for the reversing loop code
    stateio = REVERSE; // Force state to 99 to show RL is being used
    if (rlstate == 0) { //Take snapshot of usage while in idle state
        countio = 0; // reset counter
        if (state[0] == TWORAIL) { // Via S1
            rlsense = 1;
            modeio = free[3];
            rlstate = 1;
        }
        if (state[2] == TWORAIL) { // Via S3
            rlsense = 3;
            modeio = !free[3];
            rlstate = 1;
        }
        if ((state[1] == TWORAIL) && (rlsense == 0)) { // Uncertain. Will assume Via S3
            rlsense = 3;
            modeio = !free[3];
            rlstate = 1;
        }
        //We will replace the local logic and may in the future replace with consuming relevant events
        if (preout[1] == TRUE) { // S2 is in 3-rail mode so preset S3
            softprein[2] = TRUE;
        } else { // S2 is not in 3-rail mode so don't preset S3
            softprein[2] = FALSE;
        }
        if (preout[2] == TRUE) { // S3 is in 3-rail mode so preset S2
            softprein[1] = TRUE;
        } else { // S3 is not in 3-rail mode so don't preset S2
            softprein[1] = FALSE;
        }
    }
    if (rlstate == 1) { // Will use the determined sensio as long as occupied
        if ((rlsense == 1) && (state[1] == TWORAIL)) { // Via S1 - change mode
            modeio = !free[3];
            countio = 0; // reset counter
        }
        if ((rlsense == 3) && (state[0] == TWORAIL)) { // Via S3
            modeio = free[3];
            countio = 0; // reset counter
        }
        if ((state[0] == IDLE) && (state[1] == IDLE) && (state[2] == IDLE)) { // reverse loop is probably free
            if (countio >= ONEEIGHTSEC) { // waited long enough
                rlstate = 0; // reverse loop idle
                modeio = free[3]; // relay switched to via S1
                rlsense = 0; // sense set to uncertain
                countio = 0; // reset counter
            }
        } else {
            countio = 0; // reset counter
        }
    }
}

//Fixed Cross Logic
void fixedCross(void) {
    // Fixed Cross logic
    // Channel 0 used for normal operations
    // Channel 1 is occupancy detection for North/South (NS) passage
    // Channel 2 is occupancy detection for East/West (EW)passage
    // Channel 3 is a relay channel that will control which part of the cross has power
    // During startup both NS and EW passages will be powered alternatively to detect presence
    // in one of the sections
    // By default NS is powered and EW will be toggled and powered as needed
    // While EW is powered NS will not be powered
    stateio = CROSS; // Force state to 98 to show cross is being used
    if (!xInitialised) {//OK first time into this logic so must check both sections for occupancy and handle

    } else {//Normal operations where North/South (NS) has power and East/West is toggled as needed
        if (state[1] == IDLE) {//NS section is idle so can look at EW section
            if (state[2] != IDLE) {//OK this section is occupied
                mode[3] = !free[3];
                if ((state[2] >= TRANSIT) && (state[2] <= UNCERTAIN) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
                    mode[1] = !free[1];
                }
            }
        } else {
                if ((state[1] >= TRANSIT) && (state[1] <= UNCERTAIN) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
                    mode[2] = !free[2];
                }
        }
    }
}

void trackCoreLogic(){ //One invocation of this method will handle the needed channels depending on use case

    //Define & set local variables
	unsigned char channelCounter = 0;
    //Check the track mode. Normally this shouldn't change frequently during normal operations
    getTrackMode();
    if (trackMode == STDMODE) {channels = 4;}
    if (trackMode == RLMODE) {channels = 3;}
    if (trackMode == XMODE) {channels = 3;}

    getSecnew(); //Get 1Track IO HW version
    
    //Latch the values of the I/O ports into variables
    //Use state[] with logic to chose which type of OCC has to be loaded from new hardware (10)b & software version
    occ[0] = OCC2R1;
    occ[1] = OCC2R2;
    occ[2] = OCC2R3;
    occ[3] = OCC2R4;

    sense[0] = SHORT1;
    sense[1] = SHORT2;
    sense[2] = SHORT3;
    sense[3] = SHORT4;

    if (mode[0] != MODE1) {mode[0] = MODE1;} // Output
    if (mode[1] != MODE2) {mode[1] = MODE2;} // Output
    if (mode[2] != MODE3) {mode[2] = MODE3;} // Output
    if (mode[3] != MODE4) {mode[3] = MODE4;} // Output        

    //This is the tic/tac logic we will use for timing below each tic/tac is 50 ms
    tic = ticTac();
    if (tic == tac) {//this section timer counter
		count1T[0] = count1T[0] + 1;
		count1T[1] = count1T[1] + 1;
		count1T[2] = count1T[2] + 1;
		count1T[3] = count1T[3] + 1;
		tac = !tac;
	}

	//For each channel execute below, this handles the standard mode channels
	for (channelCounter = 0; channelCounter < channels; channelCounter++){
		//Get values
        countio = count1T[channelCounter];
		occio = occ[channelCounter];
		senseio = sense[channelCounter];
        //Apply secnew logic to relevant inputs - new since 4-4
        secnewio = secnew[channelCounter];
        if (secnewio == 1) {// is Jun 19 prototype hardware
            senseio = !senseio;
            occio = !occio;
        }
		modeio = mode[channelCounter];
        preinio = prein[channelCounter];
        preoutio = preout[channelCounter];
		timeio = sectionTime[channelCounter];
		stateio = state[channelCounter];
		lostoccio = lostocc[channelCounter];
		passio = pass[channelCounter];
        forcedio = forced[channelCounter];
		//Check current section
		//Possible valid states:
		//Sense Occ     meaning
		//Pres   X      Short circuit
		//Abs    Pres   Busy
		//Abs    Abs    Idle/PoT
        switch (stateio) {
            case IDLE:  //Idle mode
                preoutio = FALSE;
                if (senseio == PRESENT) { //This section is possibly shortened
                    modeio = !free[channelCounter];  //set relay to 3R
                    stateio = TRANSIT;  //must go to transition mode
                    countio = 0; //reset counter
                } else { //section safe so check if
                    if (occio == PRESENT) { //section occupied
                        if (countio >= ONEEIGHTSEC) { //waited long enough
                            countio = 0; //reset counter
                            if (senseio == ABSENT) {
                                stateio = TWORAIL;  //Looks normal so let's go to 2R
                            } else {
                                stateio = TRANSIT; //Hmm, also shortened so let's go 3R instead
                            }
                        }
                    } else {//section free
                        if (preinio == TRUE) { //preset received
                                modeio = !free[channelCounter];  //set relay to 3R
                                stateio = THREERAIL;  //set 3R
                                preoutio = FALSE; //don't propagate the preset
                        }
                        countio = 0;
                    }
                }
                forcedio = FALSE; //not forced to 30 from 20
                forcedcountio = 0; //reset the forced counter
            break;
            case TWORAIL:  //2R occupied mode
                preoutio = FALSE;
                if ((occio == ABSENT) && (senseio == ABSENT)) { //section free
                    if (countio >= ONEEIGHTSEC) { //waited long enough
                        stateio = IDLE; //set Idle state
                        forcedio = FALSE; //Not forced from 20 to 30
                    }
                }else {
                    countio = 0; //reset counter
                }
                if (senseio == PRESENT) { //possible short circuit
                    modeio = !free[channelCounter]; //just to be safe toggle relay &
                    stateio = TRANSIT; //switch to 3R transition mode
                    forcedio = TRUE; //Being forced from 20 to 30
                }
            break;
            case TRANSIT:  //3R Transition mode
                preoutio = FALSE;
                if (countio >= ONEEIGHTSEC) { //waited long enough
                    stateio = THREERAIL; //3R mode
                    preoutio = TRUE;
                    countio = 0;//reset counter
                }
            break;
            case THREERAIL:  //3R mode, occupied signal which combines current consumption and voltage presence
                //Don't do anything with preoutio that is handled in other cases
                if (senseio == ABSENT) { //3R normal status
                    if ((occio == ABSENT) || (preinio == FALSE)){ //looks like the section might be free
                        stateio = UNCERTAIN; //3R occupied lost
                    }
                }
                if (forcedio == TRUE) { //Will try to recover from being forced in to state 30 from 20
                        if (countio >= ONEEIGHTSEC) { //Timer
                            countio = 0;  //reset counter
                            forcedcountio = forcedcountio + 1;
                        }
                        if (forcedcountio > FOURSEC) { //Waited long enough so let's try
                            forcedcountio = 0; //reset the forced counter
                            modeio = free[channelCounter]; //free relay
                            stateio = TWORAIL; //set state 20
                            countio = 0; //reset counter
                        }
                } else {
                    countio = 0; //reset counter
                }
            break;
            case UNCERTAIN:  //3R occupied lost
                //Don't change preoutio to avoid rattling relays
                if (preinio == FALSE) { //preset is not PRESENT
                    if (occio == ABSENT) { //3R occupied lost
                        if (countio >= ONEEIGHTSEC) { //waited long enough
                            countio = 0; //reset counter
                            lostoccio = lostoccio + 1;
                            stateio = UNCERTAIN; //stay in occupied lost mode
                        }
                    } else { //3R occupied PRESENT
                        lostoccio = 0;
                        stateio = THREERAIL; //occupied lost mode ended
                    }
                    if (lostoccio >= maxlostocc) { //go to idle mode
                        lostoccio = 0;
                        stateio = IDLE;
                        countio = 0; //reset counter
                        modeio = free[channelCounter]; //toggle relay to 2R mode
                    }
                } else { //preset is PRESENT
                    stateio = THREERAIL; //occupied lost mode ended
                }
            break;
        }
        //return values
        mode[channelCounter] = modeio;
        if (prein[channelCounter] != preinio) {prein[channelCounter] = preinio;}
        if (preout[channelCounter] != preoutio) {preout[channelCounter] = preoutio;}
        count1T[channelCounter] = countio;
        if (state[channelCounter] != stateio){//Only write if state changes as it helps with debugging
            state[channelCounter] = stateio;
        }
        lostocc[channelCounter] = lostoccio;
        pass[channelCounter] = passio;
        forced[channelCounter] = forcedio;
        forcedcount[channelCounter] = forcedcountio;
	}
    //Handle Reverse Loop Mode
    if (trackMode == RLMODE){
        reverseLoop();
    }
    //Handle Cross Mode 
    if (trackMode == XMODE){
        fixedCross();
    }
    //LATCH mode values to Port Latch if changed only to avoid rattling relay
    if (MODE1W != mode[0]){
        if (mode[0] == 0){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[0].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[0].io));
        }
        MODE1W = mode[0];
    }
    if (MODE2W != mode[1]){
        if (mode[1] == 0){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[1].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[1].io));
        }
        MODE2W = mode[1];
    }
    if (MODE3W != mode[2]){
        if (mode[2] == 0){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[2].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[2].io));
        }
        MODE3W = mode[2];
    }
    if (MODE4W != mode[3]){
        if (mode[3] == 0){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[3].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[3].io));
        }
        MODE4W = mode[3];
    }
}
