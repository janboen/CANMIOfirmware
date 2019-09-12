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
 * Minor update on 28 Sep 18 to handle v2 IO hardware
 * Added feature to support a fixed cross (vast kruis), removed pre in, pre out functionality.
 * Dropped fixed 3R mode and use this setting for fixed cross
 * Pre in will be used for the future Occ3R changes
 * Fixed cross will use a separate channel with relay and use channel 2 and 3 as feeder channels
 *
 * Rework Sun 30 Jun and later - rewrite so the code better supports different versions of IO boards and internally
 * all logic works with ACTIVE HIGH (TRUE) logic. free = 0 and implies idle and !free = 1 and implies active
 * To support different hardware versions of the IO board the following is added
 * 4 bits where, per set of 2 channels 
 * 0000 = v0 - the first prototype version
 * 0001 = v1 - the prototype version (June 2019) - IO works the other way around from previous version
 * 0002 = v2 - the prototype version (Aug 2019) - IO works like v0 BUT OCC2R and OCC3R are reversed
 * 
 */
#include "cbus1Track.h"

//Define & set global variables
unsigned short timerCount;
unsigned char channels;
unsigned char xInitialised;
TickValue lastTime;
TickValue nowTime;
unsigned char cCount;
unsigned char count1T[4];
char maxlostocc;
char maxshort;
unsigned char rlsense;
unsigned char rloop;
unsigned char sectionTime[4];
unsigned char state[4];
unsigned char lostocc[4];
BOOL pass[4];
BOOL occ2Rio;
BOOL occ3Rio;
BOOL senseio;
BOOL prein[4];
BOOL preout[4]; //Only used with reverse loop solution
BOOL rlstate; //Only used with reverse loop solution
BOOL occ2R[4];
BOOL occ3R[4];
BOOL sense[4];
BOOL mode[4];
BOOL softprein[4]; //Only used with reverse loop solution
BOOL forced[4];
unsigned char trackMode;
BOOL tmrbit;
BOOL tic;
BOOL tac;
//Rework 29 June 19 new variables
unsigned char hwProfile[2];
unsigned char hwVersion[4];
BOOL freeOCC2R[4];
BOOL freeOCC3R[4];
BOOL freeSENSE[4];
BOOL freeMODE[4];
BOOL previousMODE[4];
BOOL changedPREIN[4];

//Various Helper Methods
void hwProfiler(void){ //Populates various arrays with correct values so multiple different IO hardware versions are supported
    //Get 1Track IO HW version
    //Use NV->spare[9] get 2 pairs of 4 bits which will indicate the hardware version of the 1Track IO
    unsigned char spare9 = NV->spare[9];
    for (cCount = 0; cCount < 2; cCount++){
        hwProfile[cCount] = spare9 & HWVERSIONMASK;
        //One hwProfile section required for each different hardware version
        //We add 4 channels even though they behave the same 2 by 2
        hwVersion[2*cCount]= hwProfile[cCount];
        hwVersion[2*cCount+1]= hwProfile[cCount];        
        if (hwProfile[cCount] == 0){//v0 hardware, pre 2019
            freeOCC2R[2*cCount]= 1;
            freeOCC3R[2*cCount]= 1;
            freeSENSE[2*cCount]= 1;
            freeMODE[2*cCount]= 0;
            freeOCC2R[2*cCount+1]= 1;
            freeOCC3R[2*cCount+1]= 1;
            freeSENSE[2*cCount+1]= 1;
            freeMODE[2*cCount+1]= 0;
        }
        if (hwProfile[cCount] == 1){//v1 hardware, Apr 2019
            freeOCC2R[2*cCount]= 0;
            freeOCC3R[2*cCount]= 0;
            freeSENSE[2*cCount]= 0;
            freeMODE[2*cCount]= 1;
            freeOCC2R[2*cCount+1]= 0;
            freeOCC3R[2*cCount+1]= 0;
            freeSENSE[2*cCount+1]= 0;
            freeMODE[2*cCount+1]= 1;
        }        
        if (hwProfile[cCount] == 2){//v2 hardware, Aug 2019, supports OCC3R
            freeOCC2R[2*cCount]= 1;
            freeOCC3R[2*cCount]= 1;
            freeSENSE[2*cCount]= 1;
            freeMODE[2*cCount]= 0;
            freeOCC2R[2*cCount+1]= 1;
            freeOCC3R[2*cCount+1]= 1;
            freeSENSE[2*cCount+1]= 1;
            freeMODE[2*cCount+1]= 0;
        }          
        spare9 = spare9 >> 4;
    }   
}

void init1TrackVars(void){

    //set some NV values (saves time during debugging)
    #ifndef PROD
        NV->io[2].type = 1;
        NV->io[6].type = 1;
        NV->io[11].type = 1;
        NV->io[14].type = 1;
        NV->spare[9] = 0x10;
        NV->spare[10] = 0x81;        
    #endif
    nowTime.Val = tickGet();
    cCount = 0;
    xInitialised = 0;
    hwProfiler(); //Get 1Track IO HW version
    for (cCount = 0; cCount < 4; cCount++){
        count1T[cCount] =  0;
        sectionTime[cCount] = 0;
       if ((hwVersion[cCount] == 0) || (hwVersion[cCount] == 2)){//for v0 hardware from 2018
            state[cCount] = IDLE;// stores the state value of a section
        }
       if (hwVersion[cCount] == 1){//for v1 hardware from June 2019
            state[cCount] = THREERAIL;// stores the state value of a section
        }
        lostocc[cCount] = 0;
        pass[cCount] = 0;
        prein[cCount] = FALSE;
        occ2R[cCount] = 0; //IN
        sense[cCount] = 0; //IN
        preout[cCount] = FALSE;
        softprein[cCount] = FALSE; //OUT
        forced[cCount] = FALSE;
        changedPREIN[cCount] = FALSE;
    }
    maxlostocc = 8; // x 150 ms - consecutive maximum number that sense may be lost in 3R mode before going back to 2R mode
    maxshort = 30;  // maximal number short must be seen before transiting to 3R mode - higher than in PIC as code runs faster. I think...
    rlsense = RLFREE;
    rloop = 0; //holds the current switch/case state value of the reversing loop
    occ2Rio = FALSE;
    senseio = FALSE;
    rlstate = RLFREE; //Reverseloop is free
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

// To Do
// Test Reverse Loop and Fixed Cross Modes

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

*/

//Reverse Loop Logic
void reverseLoop(void) {
    // Reverse Loop
    // state can be uncertain (RLFREE), via S1 (RLVIAS1) or via S3 (RLVIAS3)
    // Uncertain state exists when S2 is occupied and when no train entered via S1 or S3
    // This can happen during a power cycle/outage
    // In no case shall all 3 sections be occupied at the same time when starting the system
    // The 3 sections will provide a stable input for the reversing loop code
    // 21 July 19 = adjust the logic so it reacts the same with 2R or 3R, should work
    state[3] = REVERSE; // Force state to REVERSE to show RL is being used    
    if (rlstate == RLFREE) { //Take snapshot of usage while in idle state
        count1T[3] = 0; // reset counter
        if (state[0] == TWORAIL) { // Via S1
            rlsense = RLVIAS1;
            mode[3] = freeMODE[3];
            rlstate = RLBUSY;
        }
        if (state[2] == TWORAIL) { // Via S3
            rlsense = RLVIAS3;
            mode[3] = !freeMODE[3];
            rlstate = RLBUSY;
        }
        if ((state[1] == TWORAIL) && (rlsense == 0)) { // Uncertain. Will assume Via S3
            rlsense = RLVIAS3;
            mode[3] = !freeMODE[3];
            rlstate = RLBUSY;
        }
        //3R logic to avoid short circuits
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
    if (rlstate == RLBUSY) { // Will use the determined sense as long as occupied
        if (rlsense == RLVIAS1) { // Via S1
            if (state[1] == TWORAIL) { // Via S1 - normal change mode
                mode[3] = !freeMODE[3];
                count1T[3] = 0; // reset counter
            }
            if (state[0] == TWORAIL) { // Via S1 - must have changed direction inside reverse loop
                mode[3] = freeMODE[3]; // Also true while entering the RL but has no impact
                count1T[3] = 0; // reset counter
            }            
        }
        if (rlsense == RLVIAS3) { // Via S3
            if (state[0] == TWORAIL) { // Via S3 - normal change mode
                mode[3] = freeMODE[3];
                count1T[3] = 0; // reset counter
            }
            if (state[1] == TWORAIL) { // Via S3 - must have changed direction inside reverse loop
                mode[3] = !freeMODE[3]; // Also true while entering the RL but has no impact
                count1T[3] = 0; // reset counter
            }
        }
        if ((state[0] == IDLE) && (state[1] == IDLE) && (state[2] == IDLE)) { // reverse loop is probably free
            if (count1T[3] >= SHORTWAIT) { // waited long enough
                rlstate = RLFREE; // reverse loop idle
                mode[3] = freeMODE[3]; // relay switched to via S1
                rlsense = RLFREE; // sense set via S1
                count1T[3] = 0; // reset counter
            }
        } else {
            count1T[3] = 0; // reset counter
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
    state[3] = CROSS; // Force state to CROSS to show cross is being used
    if (xInitialised == FALSE) {//OK first time into this logic so must check both sections for occupancy and handle
        xInitialised = TRUE;
        //Logic to be added if needed
        //If not needed then simplify the logic
    } else {//Normal operations where North/South (NS) has power and East/West is toggled as needed
        if (state[1] == IDLE) {//NS section is idle so can look at EW section
            if (state[2] != IDLE) {//OK this section is occupied
                mode[3] = !freeMODE[3];
                if ((state[2] >= TRANSIT) && (state[2] <= UNCERTAIN) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
                    mode[1] = !freeMODE[1];
                }
            }
        } else {
            if ((state[1] >= TRANSIT) && (state[1] <= UNCERTAIN) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
                mode[2] = !freeMODE[2];
            }
        }
    }
}

void trackCoreLogic(){ //One invocation of this method will handle the needed channels depending on use case

    //Define & set local variables
	cCount = 0;
    //Check the track mode. Normally this shouldn't change frequently during normal operations
    getTrackMode();
    if (trackMode == STDMODE) {channels = 4;}
    if (trackMode == RLMODE) {channels = 3;}
    if (trackMode == XMODE) {channels = 3;}

    hwProfiler(); //Get 1Track IO HW version
    
    //Latch the values of the I/O ports into variables
    //Due to an inversion in use of pins of the v2 PCB we tweak the code here to handle the inversion
    //OCC2R and OCC3R have been switched
    if (hwVersion[0] > 1) {
        occ2R[0] = OCC3R1;
        occ3R[0] = OCC2R1;
    } else {
        occ2R[0] = OCC2R1;
        occ3R[0] = occ2R[0];
    } 
    if (hwVersion[1] > 1) {
        occ2R[1] = OCC3R2;
        occ3R[1] = OCC2R2;
    } else {
        occ2R[1] = OCC2R2;
        occ3R[1] = occ2R[1];
    } 
    if (hwVersion[2] > 1) {
        occ2R[2] = OCC3R3;
        occ3R[2] = OCC2R3;
    } else {
        occ2R[2] = OCC2R3;
        occ3R[2] = occ2R[2];
    } 
    if (hwVersion[3] > 1) {
        occ2R[3] = OCC3R4;
        occ3R[3] = OCC2R4;
    } else {
        occ2R[3] = OCC2R4;
        occ3R[3] = occ2R[3];
    } 

    
    //For old HW versions 2R and 3R OCC are the same. From v2 HW version this will change
    if (hwVersion[0] > 1) {occ3R[0] = OCC3R1;} else {occ3R[0] = occ2R[0];} 
    if (hwVersion[1] > 1) {occ3R[1] = OCC3R2;} else {occ3R[1] = occ2R[1];} 
    if (hwVersion[2] > 1) {occ3R[2] = OCC3R3;} else {occ3R[2] = occ2R[2];} 
    if (hwVersion[3] > 1) {occ3R[3] = OCC3R4;} else {occ3R[3] = occ2R[3];} 

    sense[0] = SHORT1;
    sense[1] = SHORT2;
    sense[2] = SHORT3;
    sense[3] = SHORT4;

    previousMODE[0] = mode[0];
    previousMODE[1] = mode[1];
    previousMODE[2] = mode[2];
    previousMODE[3] = mode[3];
    
    changedPREIN[0] = FALSE;
    changedPREIN[1] = FALSE;
    changedPREIN[2] = FALSE;
    changedPREIN[3] = FALSE;

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
	for (cCount = 0; cCount < channels; cCount++){
		//Get values
        //All IO get XOR with their HW profile modifier to make sure that all are converted to active HIGH logic
        senseio = sense[cCount] ^ freeSENSE[cCount];
        occ2Rio = occ2R[cCount] ^ freeOCC2R[cCount];
        occ3Rio = occ2Rio; //Later to be occ3Rio[cCount] ^ freeOCC3R[cCount];
        changedPREIN[cCount] = FALSE;
		//Check current section
		//Possible valid states:
		//SENSE  OCC    meaning
		//TRUE   X      Short circuit
		//FALSE  TRUE   Busy
		//FALSE  FALSE  Idle/PoT
        switch (state[cCount]) {
            case IDLE:  //Idle mode
                if (senseio == TRUE) { //This section is possibly shortened
                    mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                    state[cCount] = TRANSIT;  //must go to transition mode
                    count1T[cCount] = 0; //reset counter
                } else { //section safe so check if
                    if (occ2Rio == TRUE) { //section occupied
                        if (count1T[cCount] >= SHORTWAIT) { //waited long enough
                            count1T[cCount] = 0; //reset counter
                            if (senseio == FALSE) {
                                mode[cCount] = freeMODE[cCount]; //set relay to 2R
                                state[cCount] = TWORAIL;  //Looks normal so let's go to 2R
                            } else {
                                mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                                state[cCount] = TRANSIT; //Hmm, also shortened so let's go 3R instead
                            }
                        }
                    } else {//section free
                        if (prein[cCount] == TRUE) { //preset received
                                mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                                state[cCount] = FORCED3R;
                        }
                        count1T[cCount] = 0;
                    }
                }
                forced[cCount] = FALSE; //not forced to 30 from 20
            break;
            case TWORAIL:  //2R occupied mode
                forced[cCount] = FALSE; //Not forced from 20 to 30
                prein[cCount] = FALSE; //Can't be preset when in 2R
                if (senseio == TRUE) { //possible short circuit
                    mode[cCount] = !freeMODE[cCount]; //just to be safe toggle relay to 3R mode &
                    state[cCount] = TRANSIT; //switch to 3R transition mode
                    forced[cCount] = TRUE; //Being forced from 20 to 30
                }
                if (occ2Rio == FALSE) { //section free
                    if (count1T[cCount] >= QUARTERSEC) { //waited long enough
                        mode[cCount] = freeMODE[cCount];  //set relay to 2R
                        state[cCount] = IDLE; //set Idle state
                    }
                }else {
                    count1T[cCount] = 0; //reset counter
                }
            break;
            case TRANSIT:  //3R Transition mode - to allow sensor stabilisation
                if (count1T[cCount] >= QUARTERSEC) { //waited long enough
                    mode[cCount] = !freeMODE[cCount];  //Just to be safe set relay to 3R (again)
                    state[cCount] = THREERAIL; //3R mode
                    count1T[cCount] = 0;//reset counter
                }
            break;
            case THREERAIL:  //3R mode, occupied signal which combines current consumption and voltage presence
                if (prein[cCount] == FALSE ){
                    if (senseio == FALSE) { //3R normal status
                        if (occ3Rio == FALSE){ //looks like the section might be free
                            mode[cCount] = !freeMODE[cCount];  //keep relay in 3R
                            state[cCount] = UNCERTAIN; //3R occupied lost
                        }
                    }
                }
                if ((forced[cCount] == TRUE) && (hwVersion[cCount] == 0)) {
                    //Only for v0 hw will try to recover from being forced in to state 30 from 20
                    //v1 hardware handles this hardware wise
                    if (count1T[cCount] >= FOURSEC) { //Waited long enough so let's try
                        count1T[cCount] = 0; //reset counter
                        mode[cCount] = freeMODE[cCount]; //free relay
                        state[cCount] = TWORAIL; //set state 20
                        forced[cCount] = FALSE; //set back to normal state
                    }
                }
            break;
            case FORCED3R:  //Forced from 2R to 3R mode
                if (occ3Rio == TRUE){ // If the section is occupied 
                    if (count1T[cCount] >= SHORTWAIT) { //waited long enough
                        prein[cCount] = FALSE; //then the preset has become irrelevant
                        count1T[cCount] = 0;
                        changedPREIN[cCount] = TRUE;
                        state[cCount] = THREERAIL;// Switch to normal 3R mode
                    }
                }
            break;            
            case UNCERTAIN:  //3R occupied lost
                if (prein[cCount] == FALSE) { //preset is not TRUE
                    if (occ3Rio == FALSE) { //3R occupied lost
                        if (count1T[cCount] >= SHORTWAIT) { //waited long enough
                            count1T[cCount] = 0; //reset counter
                            lostocc[cCount] = lostocc[cCount] + 1;
                            mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                            state[cCount] = UNCERTAIN; //stay in occupied lost mode
                        }
                    } else { //3R occupied TRUE
                        lostocc[cCount] = 0;
                        mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                        state[cCount] = THREERAIL; //occupied lost mode ended
                    }
                    if (lostocc[cCount] >= maxlostocc) { //go to idle mode
                        lostocc[cCount] = 0;
                        mode[cCount] = freeMODE[cCount];  //set relay to 2R
                        state[cCount] = IDLE;
                        count1T[cCount] = 0; //reset counter
                    }
                } else { //preset is TRUE
                    mode[cCount] = !freeMODE[cCount];  //set relay to 3R
                    state[cCount] = THREERAIL; //occupied lost mode ended
                }
            break;
        }
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
    if ((previousMODE[0] != mode[0]) || (changedPREIN[0] == TRUE)){
        if (mode[0] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[0].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[0].io));
        }
        MODE1W = mode[0];
    }
    if ((previousMODE[1] != mode[1]) || (changedPREIN[1] == TRUE)){
        if (mode[1] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[1].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[1].io));
        }
        MODE2W = mode[1];
    }
    if ((previousMODE[2] != mode[2]) || (changedPREIN[2] == TRUE)){
        if (mode[2] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[2].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[2].io));
        }
        MODE3W = mode[2];
    }
    if ((previousMODE[3] != mode[3]) || (changedPREIN[3] == TRUE)){
        if (mode[3] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[3].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[3].io));
        }
        MODE4W = mode[3];
    }
}
