/* 
 * File:   cbus1Track.h
 * Author: Jan Boen
 *
 * Created on 9 Feb, 2019, 16:26
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
//#define DEV0 //production mode
#define DEV1 //same timing as DEV0 but sets I/O pins 
//#define DEV2 //shorter timing than DEV1 also sets I/O pins

#define IDLE 10
#define TWORAIL 20
#define TRANSIT 23
#define THREERAIL 30
#define UNCERTAIN 88
#define CROSS 98
#define REVERSE 99
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
#define OCC3R PORTAbits.RA5

#ifdef DEV0 //Standard mode
    #define TMRSHIFT 7
    #define ABSENT 1
    #define PRESENT 0
    #define ONEEIGHTSEC 4
    #define QUARTERSEC 8
    #define HALFSEC 15
    #define ONESEC 30
    #define FOURSEC 120
#endif

#ifdef DEV1 //As standard mode but presets pin
    #define TMRSHIFT 7
    #define ABSENT 1
    #define PRESENT 0
    #define ONEEIGHTSEC 4
    #define QUARTERSEC 8
    #define HALFSEC 15
    #define ONESEC 30
    #define FOURSEC 120
#endif

#ifdef DEV2 //Development mode with shorter timings and uses active high logic
    #define TMRSHIFT 6
    #define ABSENT 0
    #define PRESENT 1
    #define ONEEIGHTSEC 1
    #define QUARTERSEC 2
    #define HALFSEC 3
    #define ONESEC 6
    #define FOURSEC 24
#endif

#define STDMODE 0x81 //Standard 1Track mode
#define RLMODE 0x82 //Reverse Loop mode
#define XMODE 0x83 //Fixed cross (vast kruis) mode    

#define SECNEWMASK 0b00000011

//Define & set global variables
extern unsigned char channels;
extern unsigned char xInitialised;
extern unsigned char secnew[];
extern unsigned char secnewio;
extern TickValue nowTime;
extern unsigned char channelCounter;
extern unsigned char countio;
extern unsigned char count1T[];
extern char maxlostocc;
extern char maxshort;
extern unsigned char rlsense;
extern unsigned char rloop;
extern unsigned char timeio;
extern unsigned char sectionTime[];
extern unsigned char stateio;
extern unsigned char state[];
extern unsigned char lostoccio;
extern unsigned char lostocc[];
extern BOOL free;
extern BOOL used;
extern BOOL passio;
extern BOOL pass[];
extern BOOL occio;
extern BOOL senseio;
extern BOOL preinio;
extern BOOL prein[];
extern BOOL modeio;
extern BOOL rlstate;
extern BOOL occ[];
extern BOOL sense[];
extern BOOL mode[];
extern BOOL preout[];
extern BOOL preoutio;
extern BOOL softprein[];
extern BOOL forced[];
extern unsigned char forcedcount[];
extern BOOL forcedio;
extern unsigned char forcedcountio;
//extern unsigned char useforced;
extern unsigned char dev;
extern unsigned char trackMode;
extern BOOL tmrbit;
extern BOOL tic;
extern BOOL tac;

void init1TrackVars(void);
unsigned char ticTac(void);
void section2PinMapping(void);
void initEventMods(void);
void getTrackMode(void);
void trackCoreLogic(void);
//void handleChannel(void);
void reverseLoop(void);
void fixedCross(void);

#ifdef	__cplusplus
}
#endif

#endif	/* CBUS1TRACK_H */
