/* 
 * File:   cbus1Track.h
 * Author: Jan Boen
 *
 * Created on 9 Feb, 2019, 16:26
 * Major update June & Nov 2019
 */

#ifndef CBUS1TRACK_H
#define	CBUS1TRACK_H

#include "GenericTypeDefs.h"
#include "devincs.h"
#include "TickTime.h"
#include "nvCache.h"
#include "mioNv.h"
#include "mioEvents.h"
#include "actionQueue.h"
#include "digitalOut.h"
#include "eventMods.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
//1Track specific defines
//#define TEST //test mode, can be combined with both PROD and DEV modes
#define PROD //production mode
//#define DEV //10X shorter timing as PROD and sets I/O pins 

//1Track specific statuses
#define NOPOWER 0
#define IDLE 10 //Default mode
#define PRESET3R 13
#define TWORAIL 20
#define THREERAIL 30
#define FORCED3R 33
#define SPECIALSTATES 90 //Modes from 90 are special cases    
#define CROSS 98
#define REVERSE 99
   
#ifdef PROD
    #define FIFTYMILLIS 3125 //Number of tick needed for 50 ms
#endif
    
#ifdef DEV
    #define FIFTYMILLIS 300 //Shorter timing for debug purposes
#endif
    
#define SHORTWAIT 3 //approx 150 ms
#define QUARTERSEC 5
#define HALFSEC 10
#define ONESEC 20
#define TWOSEC 40
#define FOURSEC 80

#define STDMODE 0x81 //129 - Standard 1Track mode
#define RLMODE 0x82 //130 - Reverse Loop mode
#define XMODE 0x83 //131 - Fixed cross (vast kruis) mode    
#define BLINK 0x84 //132 - Blink mode    
    
#define HWVERSIONMASK 0b00001111
    
//Define & set global variables
extern unsigned short timerCount;
extern unsigned char channels;
extern unsigned char xInitialised;
extern TickValue nowTime;
extern TickValue lastTime;
extern unsigned char cCount;
extern unsigned char count1T[];
extern char maxlostocc;
extern char maxshort;
extern unsigned char sectionTime[];
extern unsigned char state[];
extern unsigned char lostocc[];
extern BOOL pass[];
extern BOOL senseio;
extern BOOL prein[];
extern BOOL sense[];
extern BOOL mode[];
extern BOOL forced[];
extern unsigned char dev;
extern unsigned char trackMode;
extern BOOL tmrbit;
extern BOOL tic;
extern BOOL tac;

//Rework 29 June 19 new variables
extern unsigned char hwProfile[];
extern unsigned char hwVersion[];
extern BOOL freeMODE[];
extern BOOL hwOCC2R[];
extern BOOL hwOCC3R[];
extern BOOL hwSENSE[];
extern BOOL hwMODE[];
extern BOOL occ2R[];
extern BOOL occ3R[];
extern BOOL occ2Rio;
extern BOOL occ3Rio;
extern BOOL changeState[];
//7 June 21 changes
extern BOOL firstTime;

void init1TrackVars(void);
unsigned char ticTac(void);
void section2PinMapping(void);
void initEventMods(void);
void getTrackMode(void);
void trackCoreLogic(void);
void reverseLoop(void);
void fixedCross(void);
void hwProfiler(void);

#ifdef	__cplusplus
}
#endif

#endif	/* CBUS1TRACK_H */
