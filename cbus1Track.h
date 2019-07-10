/* 
 * File:   cbus1Track.h
 * Author: Jan Boen
 *
 * Created on 9 Feb, 2019, 16:26
 * Major update June 2019
 */

#ifndef CBUS1TRACK_H
#define	CBUS1TRACK_H

#include "GenericTypeDefs.h"
#include "devincs.h"
#include "TickTime.h"
#include "nvCache.h"
#include "mioNv.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
//1Track specific defines
#define PROD //production mode
//#define DEV //10X shorter timing as PROD and sets I/O pins 

//1Track specific statuses
#define TWORAIL 10 //2R modes are less than IDLE mode
#define IDLE 20 //Default mode
#define TRANSIT 25 //All modes larger than IDLE and less than 90 are 3R related
#define THREERAIL 30
#define UNCERTAIN 88
#define SPECIALSTATES 90 //Modes from 90 are special cases    
#define CROSS 98
#define REVERSE 99
    
#define RLFREE 0 //Reverseloop free
#define RLBUSY 1 //Reverseloop free
#define RLVIAS1 1 //RL entered via S1
#define RLVIAS3 3 //RL entered via S3
    
//1Track specific pins
#define OCC2R1 PORTCbits.RC0
#define SHORT1 PORTCbits.RC1
#define MODE1 PORTCbits.RC2 //Output
#define MODE1W LATCbits.LATC2 //Output must use Latch
#define OCC3R1 PORTCbits.RC3
    
#define OCC2R2 PORTCbits.RC4
#define SHORT2 PORTCbits.RC5
#define MODE2 PORTCbits.RC6 //Output
#define MODE2W LATCbits.LATC6 //Output must use Latch
#define OCC3R2 PORTCbits.RC7

#define OCC2R3 PORTBbits.RB0
#define SHORT3 PORTBbits.RB1
#define MODE3 PORTBbits.RB5 //Output
#define MODE3W LATBbits.LATB5 //Output must use Latch
#define OCC3R3 PORTBbits.RB4

#define OCC2R4 PORTAbits.RA1
#define SHORT4 PORTAbits.RA0
#define MODE4 PORTAbits.RA3 //Output
#define MODE4W LATAbits.LATA3 //Output must use Latch
#define OCC3R4 PORTAbits.RA5

#ifdef PROD
    #define FIFTYMILLIS 3125 //Number of tick needed for 50 ms
#endif
    
#ifdef DEV
    #define FIFTYMILLIS 300 //Shorter timing for debug purposes
#endif
    
#define SHORTWAIT 3
#define QUARTERSEC 5
#define HALFSEC 10
#define ONESEC 20
#define TWOSEC 40
#define FOURSEC 80

#define STDMODE 0x81 //Standard 1Track mode
#define RLMODE 0x82 //Reverse Loop mode
#define XMODE 0x83 //Fixed cross (vast kruis) mode    

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
extern unsigned char rlsense;
extern unsigned char rloop;
extern unsigned char sectionTime[];
extern unsigned char state[];
extern unsigned char lostocc[];
extern BOOL pass[];
extern BOOL senseio;
extern BOOL prein[];
extern BOOL rlstate;
extern BOOL sense[];
extern BOOL mode[];
extern BOOL preout[];
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
