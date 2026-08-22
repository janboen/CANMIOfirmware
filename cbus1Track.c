/*
 * File:   cbus1Track.c
 * Author: Jan Boen
 * Version: January 2024 
 *
 * Change version details in canmio.h 
 * 
 * This is the code specific for the 1Track functionality.
 * In essence it adds 4 components to the standard CANMIO
 * 1- The core logic to handle the 1Track logic which allows using 3-rail tracks to simultaneously operate
 *      DCC/digital 2-rail rolling stock and DCC/digital 3-rail rolling stock in any section of track
 *      where 3-rail is typically Märklin and 2-rail most other H0 vendors.
 * 2- Allow the modification of a produced event's Event Number
 * 3- Allow not taking action on a consumed ACON event
 * 4- Introduces an extra config option using NV spare 10 so the appropriate 1Track operating mode can be set
 *      If the spare 10 value is outside the range of 0x81 - 0x84 then the node will operate like a standard CANMIO
 *
 * Features 2 and 3 should be generic as well as 4, the use of spare 10, so that anyone who wishes to run local logic can
 * leverage the extra features with little effort. This is also why the extra class of eventMods and it's header file have been created.
 *
 * Added feature to support a fixed cross (vast kruis), removed pre in, pre out functionality.
 * Dropped fixed 3R mode and use this setting for fixed cross
 * Pre in will be used for the future Occ3R changes
 * Fixed cross will use a separate channel with relay and use channel 2 and 3 as feeder channels
 *
 * Rework Sun 30 Jun and later - rewrite so the code better supports different versions of IO boards and internally
 * all logic works with ACTIVE HIGH (TRUE) logic. free = 0 and implies idle and !free = 1 and implies active
 * 
 * Checked eventMods logic and adjusted so that the OFF event get correctly handled.
 * Removed all related to CALMODE as we don't use this.
 * 
 * Major rework January 2022 - 2 BETA 35 (set via canmio.h file)
 * Remove support for pre v2 hardware, # channel logic . Rework the OCC3R / OCC2R swap
 * Simplify & rewrite state logic
 * NV[9] = bootState
 * NV[10] = 1Track mode
 * 
 * Minor rework January 2022 - 2 BETA 36 (set via canmio.h file)
 * Added logic to produce an event when coming out of 3R Preset and going to normal 3R mode 
 *
 * Update 15 Sep 2023
 * Try to add code to toggle the yellow led RB6
 * 
 * Update 13 Jan 2024 - 2 BETA 37
 * Implement a solution to handle the non consumption of a 3R preset so that when a train changes direction the preset is undone when the
 * neighbouring section is no longer in 3R mode
 * Drop the 15 Sep 2023 mod
 * 
 */

#include "cbus1Track.h"

//Define & set global variables
unsigned char trackMode;
unsigned short timerCount;
unsigned char channels;
unsigned char xInitialised;
TickValue lastTime;
TickValue nowTime;
unsigned char cCount;
unsigned char count1T[4];
unsigned char sectionTime[4];
unsigned char state[4];
unsigned char previousState[4]; //added 22 Jan 22
unsigned char lostocc[4];
BOOL occ2Rio;
BOOL occ3Rio;
BOOL senseio;
BOOL prein[4];
BOOL occ2R[4];
BOOL occ3R[4];
BOOL sense[4];
BOOL mode[4];
BOOL tic;
BOOL tac;
BOOL flip; //added to help the yellow led blink

//Rework 29 June 19 new variables
//BOOL firstTime; //uncommented on 7 June 2021
BOOL freeOCC2R[4];
BOOL freeOCC3R[4];
BOOL freeSENSE[4];
BOOL freeMODE[4];
BOOL previousMODE[4];
BOOL changedPREIN[4];
BOOL changeState[];
unsigned char spare9;
unsigned char bootState;//Is used to define the state of the channels at start time

//Various Helper Methods
void hwProfiler(void){ //Populates various arrays with correct values so multiple different IO hardware versions are supported
    spare9 = 0x11;//A dummy for the hardware version
    // hardware version currently not used
    for (cCount = 0; cCount < 2; cCount++){
        //We add 4 channels even though they behave the same 2 by 2
        freeOCC2R[2*cCount]= 1;//Zero for active high
        freeOCC3R[2*cCount]= 1;//One for active low
        freeSENSE[2*cCount]= 1;
        freeMODE[2*cCount]= 0;
        freeOCC2R[2*cCount+1]= 1;
        freeOCC3R[2*cCount+1]= 1;
        freeSENSE[2*cCount+1]= 1;
        freeMODE[2*cCount+1]= 0;
    }
}

void init1TrackVars(void){
    //set some NV values (saves time during debugging)
    #ifndef PROD
        NV->io[2].type = 1;
        NV->io[6].type = 1;
        NV->io[11].type = 1;
        NV->io[14].type = 1;
        NV->spare[9] = 0x11;
        NV->spare[10] = 0x81;
    #endif

    bootState = IDLE; //IDLE, hard coded to avoid possible simulator problem
    #ifndef TEST
        #ifdef PROD
        if (NV->spare[9] != IDLE){
            if (NV->spare[9] == TWORAIL){bootState =  NV->spare[9];}//20
            if (NV->spare[9] == THREERAIL){bootState =  NV->spare[9];}//30
            if (NV->spare[9] == NOPOWER){bootState =  NV->spare[9];}//00
        }
        #endif
    #endif

    nowTime.Val = tickGet();
    cCount = 0;
    xInitialised = 0;
    for (cCount = 0; cCount < 4; cCount++){
        count1T[cCount] =  0;
        sectionTime[cCount] = 0;
        state[cCount] = 10; //bootState;// stores the state value of a section
        previousState[cCount] = 10; //bootState;// stores the previous state value of a section
        pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[cCount].io));//Set relais to 2R mode - added 7 June 2021
        //lostocc[cCount] = 0;
        //pass[cCount] = 0;
        prein[cCount] = FALSE;
        occ2R[cCount] = 0; //IN
        occ3R[cCount] = 0; //IN
        sense[cCount] = 0; //IN
        changedPREIN[cCount] = FALSE;
        changeState[cCount] = FALSE;
    }
    occ2Rio = FALSE;
    senseio = FALSE;
    trackMode = 0; //Case variable for holding the software mode, has to be read from memory
    tic = 1;
    tac = 0;
    flip = 0;
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
    trackMode = STDMODE; //standard mode unless another one is present
    #ifndef TEST 
        if (NV->spare[10] == RLMODE){
            trackMode = RLMODE;
        }
        if (NV->spare[10] == XMODE){
            trackMode = XMODE;
        }
    #endif
}

void set1TrackPorts(void){//only needed for debugging/simulation purposes
    //Set the proper ports to outputs, default is input
    if (TRISCbits.TRISC2 != 0) {TRISCbits.TRISC2 = 0;}
    if (TRISCbits.TRISC6 != 0) {TRISCbits.TRISC6 = 0;}
    if (TRISBbits.TRISB5 != 0) {TRISBbits.TRISB5 = 0;}
    if (TRISAbits.TRISA3 != 0) {TRISAbits.TRISA3 = 0;}
    if (TRISBbits.TRISB6 != 0) {TRISBbits.TRISB6 = 0;} //Yellow Led output

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
    // 15 Dec 19 = Review code to work with CBUS functionalities
    // 23 Dec 19 = Simplify and rewrite logic so RL logic is used both in 2R and 3R mode
    state[3] = REVERSE; // Force state to REVERSE to show RL is being used
    if ((state[0] != IDLE) && (state[0] < SPECIALSTATES)) {//Via S1
        mode[3] = freeMODE[3];
    }
    if ((state[2] != IDLE) && (state[2] < SPECIALSTATES)) {//Via S3
        mode[3] = !freeMODE[3];
        count1T[3] = 0; // reset counter
    }
    if ((state[1] != IDLE) && (state[1] < SPECIALSTATES)) {//Uncertain, so safest is to assume via S3
        mode[3] = !freeMODE[3];
    }
    if ((state[0] == IDLE) && (state[1] == IDLE) && (state[2] == IDLE)) {//reverse loop is probably free
        if (count1T[3] >= QUARTERSEC) {// Waited long enough
            mode[3] = freeMODE[3];//relay switched to via S1
            count1T[3] = 0;//reset counter
        }
    } else {// Force waiting 
        count1T[3] = 0;//reset counter
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
                if ((state[2] >= THREERAIL) && (state[2] <= FORCED3R) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
                    mode[1] = !freeMODE[1];
                }
            }
        } else {
            if ((state[1] >= THREERAIL) && (state[1] <= FORCED3R) ) {//If the occupied section is in 3R mode then also set the other one to 3R to avoid possible short circuit
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
    channels = 4;
    #ifndef TEST
        #ifdef PROD
            /* if (trackMode == STDMODE) {
                if (spare9 < 0x44) {channels = 4;}//Any value less than 0x44/68 indicates a second IO board should be attached 0xFx indicates only 1 board
                else {channels = 2;}//We assume at least 1 IO board is always attached
            }*/
            if (trackMode == RLMODE) {channels = 3;}//Two IO boards must be present for proper operations
            if (trackMode == XMODE) {channels = 3;}//Two IO boards must be present for proper operations
        #endif
    #endif
    hwProfiler(); //Get 1Track IO HW version

    //Latch the values of the I/O ports into variables
    occ2R[0] = PORTCbits.RC3;
    occ3R[0] = PORTCbits.RC0;
    sense[0] = PORTCbits.RC1;
    
    occ2R[1] = PORTCbits.RC7;
    occ3R[1] = PORTCbits.RC4;
    sense[1] = PORTCbits.RC5;
    
    occ2R[2] = PORTBbits.RB4;
    occ3R[2] = PORTBbits.RB0;
    sense[2] = PORTBbits.RB1;
    
    occ2R[3] = PORTAbits.RA5;
    occ3R[3] = PORTAbits.RA1;
    sense[3] = PORTAbits.RA0;
    
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
    //count1T[cCount] normally gets reset every time a changeState[cCount] = TRUE because from then the timer starts running again.
    for (cCount = 0; cCount < channels; cCount++){
        //Get values
        //All Inputs get XOR with their HW profile modifier to make sure that all are converted to active HIGH logic
        senseio = sense[cCount] ^ freeSENSE[cCount];
        occ2Rio = occ2R[cCount] ^ freeOCC2R[cCount];
        occ3Rio = occ3R[cCount] ^ freeOCC3R[cCount];//Changed on 29 Nov 19
        changedPREIN[cCount] = FALSE;
        changeState[cCount] = FALSE;//at the beginning is always false
        #ifdef TEST
                prein[cCount] = FALSE;
        #endif        
        //Check current section
        //Possible valid states:
        //SENSE  OCC    meaning
        //TRUE   X      Short circuit
        //FALSE  TRUE   Busy
        //FALSE  FALSE  Idle/PoT
        switch (state[cCount]) {
        #ifndef TEST    
            case NOPOWER://Power is off or during startup
                //None of the sensors are active
                //Wait for something to happen
                if (senseio == TRUE) {//This section is possibly shortened
                    mode[cCount] = !freeMODE[cCount];//set relay to 3R
                    state[cCount] = THREERAIL;//go to 3R mode
                    previousState[cCount] = NOPOWER;
                    changeState[cCount] = TRUE;
                } else { //section safe so check if
                    if (occ2Rio == TRUE) {//section occupied
                        mode[cCount] = freeMODE[cCount];//set relay to 2R
                        state[cCount] = TWORAIL;//go to 2R mode
                        previousState[cCount] = NOPOWER;
                        changeState[cCount] = TRUE;
                    }
                }       
            break;
        #endif
            case IDLE://Idle mode
                if (senseio == TRUE) {//This section is possibly shortened
                    mode[cCount] = !freeMODE[cCount];//set relay to 3R
                    state[cCount] = THREERAIL;//must go to 3R mode
                    previousState[cCount] = IDLE;
                    changeState[cCount] = TRUE;
                } else { //section safe so check if
                    if (count1T[cCount] >= SHORTWAIT) {//waited long enough)
                        if (occ2Rio == TRUE) {//section occupied
                            mode[cCount] = freeMODE[cCount];//set relay to 2R
                            state[cCount] = TWORAIL;
                            previousState[cCount] = IDLE;
                            changeState[cCount] = TRUE;
                        } else {//section free
                            if (prein[cCount] == TRUE) {//preset received
                                mode[cCount] = !freeMODE[cCount];//set relay to 3R
                                state[cCount] = PRESET3R;
                                previousState[cCount] = IDLE;
                                changeState[cCount] = TRUE;
                                changedPREIN[cCount] = TRUE;
                            }
                        }
                    }
                }
            break;
            case TWORAIL://2R occupied mode
                prein[cCount] = FALSE;//Can't be preset when in 2R
                if (senseio == TRUE) {//possible short circuit
                    mode[cCount] = !freeMODE[cCount];//set relay to 3R mode
                    state[cCount] = FORCED3R;//forced to 3R mode
                    previousState[cCount] = TWORAIL;
                    changeState[cCount] = TRUE;
                } else {
                    if (occ2Rio == FALSE) {//section free
                        if (count1T[cCount] >= SHORTWAIT) {//waited long enough
                            mode[cCount] = freeMODE[cCount];//set relay to 2R
                            state[cCount] = IDLE;//set Idle state
                            previousState[cCount] = TWORAIL;
                            changeState[cCount] = TRUE;
                        }
                    }else {//occupied so we reset the counter every time
                        count1T[cCount] = 0;//reset counter
                    }
                }
            break;
            case THREERAIL://3R mode, we always wait a while in 3R mode before checking when in = stabilising
                prein[cCount] = FALSE;//Can't be preset when in 3R
                if (occ3Rio == FALSE){//looks like the section might be free
                    if (count1T[cCount] >= ONESEC) {//Must be continously free for this time
                        mode[cCount] = freeMODE[cCount];//set relay to 2R
                        state[cCount] = IDLE;//set Idle state
                        previousState[cCount] = THREERAIL;
                        changeState[cCount] = TRUE;                        
                    }
                }else {//occupied so we reset the counter every time
                    count1T[cCount] = 0;//reset counter
                }
            break;
            case PRESET3R://Preset to 3R mode
                if (previousState[cCount] == IDLE){//Due to preset
                    if (occ3Rio == TRUE){//looks like the section might be occupied
                        if (count1T[cCount] >= SHORTWAIT) {//Must be continously occupied for this time
                            mode[cCount] = !freeMODE[cCount];//set relay to 3R
                            state[cCount] = THREERAIL;//set 3R mode
                            previousState[cCount] = PRESET3R;
                            changeState[cCount] = TRUE;                        
                        }
                    } else {//3R free so we reset the counter every time and check if the preset is still present
                        count1T[cCount] = 0;//reset counter
                        //If preset true then nothing changes
                        if (prein[cCount] == FALSE){//Preset was removed
                            mode[cCount] = freeMODE[cCount];//set relay to 2R
                            state[cCount] = IDLE;//set Idle
                            previousState[cCount] = PRESET3R;
                            changeState[cCount] = TRUE;
                            changedPREIN[cCount] = TRUE;
                        }
                    }             
                }
            break;
            case FORCED3R://Forced to 3R mode
                if (previousState[cCount] == TWORAIL){//Due to 3R entered 2R occupied section
                    if (occ3Rio == FALSE){//looks like the section might be free
                        if (count1T[cCount] >= ONESEC) {//Must be continously free for this time
                            mode[cCount] = freeMODE[cCount];//set relay to 2R
                            state[cCount] = TWORAIL;//set 2R mode
                            previousState[cCount] = FORCED3R;
                            changeState[cCount] = TRUE;                        
                        }
                    }else {//occupied so we reset the counter every time
                        count1T[cCount] = 0;//reset counter
                    }
                }
            break;
        }
        if (changeState[cCount] == TRUE) {
            count1T[cCount] = 0;//reset counter
        }
    }
    #ifndef TEST
    //Handle Reverse Loop Mode
    if (trackMode == RLMODE){
        reverseLoop();
    }
    //Handle Cross Mode
    if (trackMode == XMODE){
        fixedCross();
    }
    #endif
    //LATCH mode values to Port Latch if changed only to avoid rattling relay
    //Normal 1Track mode
    if ((previousMODE[0] != mode[0]) || (changedPREIN[0] == TRUE)){
        if (mode[0] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[0].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[0].io));
        }
    }
    if ((previousMODE[1] != mode[1]) || (changedPREIN[1] == TRUE)){
        if (mode[1] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[1].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[1].io));
        }
    }
    if ((previousMODE[2] != mode[2]) || (changedPREIN[2] == TRUE)){
        if (mode[2] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[2].io));
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[2].io));
        }
     }
    if ((previousMODE[3] != mode[3]) || (changedPREIN[3] == TRUE)){
        if (mode[3] == FALSE){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[3].io));
            if (trackMode == RLMODE){//Must set the extra relay also
                pushAction(ACTION_IO_CONSUMER_OUTPUT_OFF(io2Pins[4].io));
            }
        } else {
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[3].io));
            if (trackMode == RLMODE){//Must set the extra relay also
                pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[4].io));
            }
        }
    }
    //Special logic to handle going from 3R Preset to 3R mode as normal logic will not generate an event as no relay is changed
    //Added by Jan on 17 Jan 2023
    for (cCount = 0; cCount < 4; cCount++){//Check for all 4 sections
        if ((previousState[cCount] == PRESET3R) && (state[cCount] == THREERAIL) && (changeState[cCount] == TRUE)){
            pushAction(ACTION_IO_CONSUMER_OUTPUT_ON(io2Pins[cCount].io));//the output should already be on but we want an ON event with 3R mode
        }
    }
    //Handle BLINK mode, 19 Sep 2023
    if (trackMode == BLINK){
        LATBbits.LATB6 = flip; // RB6/LATB6 used for blinking, yellow led
        flip = !flip;
    }   
}
