/*
 * File:   cbusHelpers.c
 * Author: Jan Boen
 * 
 * Part of the June 2019 rework
 * Created 30 June 2019
 * Does NOT yet support OCC3R
 */
#include "cbus1Track.h"

// This file contains helper methods used by cbus1Track.c so this file becomes lighter

//Define & set global variables
unsigned short timerCount;
unsigned char channels;
unsigned char xInitialised;
TickValue lastTime;
TickValue nowTime;
unsigned char cCount;
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
//BOOL free[4];
BOOL passio;
BOOL pass[4];
BOOL occ2Rio;
BOOL occ3Rio;
BOOL senseio;
BOOL modeio;
BOOL preinio;
BOOL prein[4];
BOOL preoutio; //Only used with reverse loop solution
BOOL preout[4]; //Only used with reverse loop solution
BOOL rlstate; //Only used with reverse loop solution
BOOL occ2R[4];
BOOL occ3R[4];
BOOL sense[4];
BOOL mode[4];
BOOL softprein[4]; //Only used with reverse loop solution
BOOL forced[4];
unsigned char forcedcount[4];
BOOL forcedio;
unsigned char forcedcountio;
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

void hwProfiler(void){ //Populates various arrays with correct values so multiple different IO hardware versions are supported
    //Get 1Track IO HW version
    //Use NV->spare[9] get 2 pairs of 4 bits which will indicate the hardware version of the 1Track IO
    unsigned char spare9 = NV->spare[9];
    for (cCount = 0; cCount < 2; cCount++){
        hwProfile[cCount] = spare9 & HWVERSIONMASK;
        //One hwProfile section required for each different hardware version
        //We add 4 channels even though they behave the same 2 by 2
        hwVersion[cCount]= hwProfile[cCount];
        hwVersion[cCount+1]= hwProfile[cCount];        
        if (hwProfile[cCount] == 0){//v0 hardware, pre 2019
            freeOCC2R[cCount]= 0;
            freeOCC3R[cCount]= 0;
            freeSENSE[cCount]= 0;
            freeMODE[cCount]= 0;
            freeOCC2R[cCount+1]= 0;
            freeOCC3R[cCount+1]= 0;
            freeSENSE[cCount+1]= 0;
            freeMODE[cCount+1]= 0;
        }
        if (hwProfile[cCount] == 1){//v1 hardware, Apr 2019
            freeOCC2R[cCount]= 1;
            freeOCC3R[cCount]= 1;
            freeSENSE[cCount]= 1;
            freeMODE[cCount]= 1;
            freeOCC2R[cCount+1]= 1;
            freeOCC3R[cCount+1]= 1;
            freeSENSE[cCount+1]= 1;
            freeMODE[cCount+1]= 1;
        }        
        spare9 = spare9 >> 4;
    }   
}

void init1TrackVars(void){
    nowTime.Val = tickGet();
    cCount = 0;
    countio = 0;
    xInitialised = 0;
    hwProfiler(); //Get 1Track IO HW version
    for (cCount = 0; cCount < 4; cCount++){
        count1T[cCount] =  0;
        sectionTime[cCount] = 0;
       if (hwVersion[cCount] == 0){//for v0 hardware from 2018
            state[cCount] = 10;// stores the state value of a section
        }
       if (hwVersion[cCount] == 1){//for v1 hardware from June 2019
            state[cCount] = 30;// stores the state value of a section
        }
        lostocc[cCount] = 0;
        pass[cCount] = 0;
        prein[cCount] = FALSE;
        occ2R[cCount] = 0; //IN
        sense[cCount] = 0; //IN
        preout[cCount] = FALSE;
        softprein[cCount] = FALSE; //OUT
        forced[cCount] = FALSE;
        forcedcount[cCount] = 0;
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
    occ2Rio = FALSE;
    senseio = FALSE;
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
