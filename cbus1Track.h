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
#define DEV0 //can also have values 1 or 2 depending on what we want to do
//#define DEV1 
//#define DEV2 
    
//1Track specific pins
#define OCC1 PORTCbits.RC0
#define SHORT1 PORTCbits.RC1
#define MODE1 PORTCbits.RC2 //Output
#define MODE1W LATCbits.LATC2 //Output must use Latch
//#define ANA1 PORTCbits.RC3 = not ANA
    
#define OCC2 PORTCbits.RC4
#define SHORT2 PORTCbits.RC5
#define MODE2 PORTCbits.RC6 //Output
#define MODE2W LATCbits.LATC6 //Output must use Latch
//#define ANA2 PORTCbits.RC7 = not ANA

#define OCC3 PORTBbits.RB0
#define SHORT3 PORTBbits.RB1
#define MODE3 PORTBbits.RB5 //Output
#define MODE3W LATBbits.LATB5 //Output must use Latch
//#define ANA3 PORTBbits.RB4

#define OCC4 PORTAbits.RA1
#define SHORT4 PORTAbits.RA0
#define MODE4 PORTAbits.RA3 //Output
#define MODE4W LATAbits.LATA3 //Output must use Latch
//#define ANA4 PORTAbits.RA4

#ifdef DEV0 //Standard mode
    #define TMRSHIFT 7
    #define ABSENT 1
    #define PRESENT 0
    #define ONEEIGHTSEC 4
    #define QUARTERSEC 8
    #define HALFSEC 15
    #define ONESEC 30
    #define FOURSEC 120
    #define DEVMODE 0 
#endif

#ifdef DEV1 //Development mode with shorter timings
    #define TMRSHIFT 6
    #define ABSENT 1
    #define PRESENT 0
    #define ONEEIGHTSEC 1
    #define QUARTERSEC 2
    #define HALFSEC 3
    #define ONESEC 6
    #define FOURSEC 24
    #define DEVMODE 1
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
    #define DEVMODE 1
#endif

#define  STDMODE 0x81
#define  RLMODE 0x82
#define  THREEMODE 0x83    
#define  CHANNELS 4

//Define & set global variables

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
extern unsigned char softprein[];
extern unsigned char forced[];
extern unsigned char forcedcount[];
extern unsigned char forcedio;
extern unsigned char forcedcountio;
extern unsigned char usepreinio;
extern unsigned char useforced;
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
void reverseLoop(void);

#ifdef	__cplusplus
}
#endif

#endif	/* CBUS1TRACK_H */
