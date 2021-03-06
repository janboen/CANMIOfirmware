
/*
 Routines for CBUS FLiM operations - part of CBUS libraries for PIC 18F
  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material
    The licensor cannot revoke these freedoms as long as you follow the license terms.
    Attribution : You must give appropriate credit, provide a link to the license,
                   and indicate if changes were made. You may do so in any reasonable manner,
                   but not in any way that suggests the licensor endorses you or your use.
    NonCommercial : You may not use the material for commercial purposes. **(see note below)
    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                  your contributions under the same license as the original.
    No additional restrictions : You may not apply legal terms or technological measures that
                                  legally restrict others from doing anything the license permits.
   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms
**************************************************************************************************************
	The FLiM routines have no code or definitions that are specific to any
	module, so they can be used to provide FLiM facilities for any module 
	using these libraries.
	
*/ 
/* 
 * File:   servo.c
 * Author: Ian
 * 
 * Handle the servo outputs. The output signal is a pulse between 1ms and 2ms where the width of the
 * pulse results in the servo moving to an angle. The outputs are driven by Timers to ensure that the
 * pulse width is accurate - although if interrupts are disabled then the width could be longer than
 * intended.
 * Pulses are output approximately every 20ms. Therefore we need more than 1 timer for all 16 possible 
 * servo outputs (16 * 2ms = 32ms which is greater than the 20ms available). A minimum of 2 timers 
 * (each handling 8 servos) is required but if we allow overdrive beyond 2ms then 3 (6 servos) or 
 * 4 (4 servos) timers is better.
 * Here we use 4 timers. Timer1..Timer4.
 * 
 * The timers are driven from Fosc/4 and use a 1:4 prescalar. With a 16MHz resonator and 4x PLL this 
 * equates to a timer increment every 0.25us. We require counts from 1ms to 2ms or 4000 - 8000 timer ticks.
 * We have an 8 bit position value and actually want to allow a bit of overdrive of the servo 0.9ms - 2.1ms.
 * (3600 ticks - 8400 ticks). This gives a range of 4800 ticks over the 8 bit range. Therefore each value
 * of the position is equivalent to 18.75 ticks - let's call it 19. The 3600 ticks at position 0 so to 
 * convert from position to ticks we need to use:
 *    Ticks = 3600 + 19 * position 
 * This is fine for the 16bit Timer1 and Timer3 but the 8 bit timers Timer2 and Timer4 need a bit more work.
 * 
 *
 * Created on 17 April 2017, 13:14
 */
#include "devincs.h"
#include "module.h"
#ifdef SERVO
#include "mioNv.h"
#include "mioEvents.h"
#include "FliM.h"
#include "config.h"
#include "GenericTypeDefs.h"
#include "TickTime.h"
#include "romops.h"
#include "mioEEPROM.h"
#include "servo.h"
#include "actionQueue.h"
#include "bounce.h"

#define POS2TICK_OFFSET         3600    // change this to affect the min pulse width
#define POS2TICK_MULTIPLIER     19      // change this to affect the max pulse width

#define MAX_SERVO_LOOP          250      // Max number of loops 
#define MAX_BOUNCE_LOOP         250      // Max number of loops 
//#define MAX_MULTI_LOOP          100      // Max number of loops 

#define SERVOS_IN_BLOCK         8

// forward definitions
void setupTimer1(unsigned char io);
void setupTimer2(unsigned char io);
void setupTimer3(unsigned char io);
void setupTimer4(unsigned char io);

// Externs
extern void setOutputPin(unsigned char io, BOOL state);
extern TickValue   lastServoStartTime;

// Variables
ServoState servoState[NUM_IO];
unsigned char currentPos[NUM_IO];
unsigned char targetPos[NUM_IO];
unsigned char stepsPerPollSpeed[NUM_IO];
unsigned char pollsPerStepSpeed[NUM_IO];
unsigned char pollCount[NUM_IO];
int speed[NUM_IO];
unsigned char loopCount[NUM_IO];

#define MAX_BOUNCE_LOOPS    255

#define EVENT_FLAG_ON       0x01
#define EVENT_FLAG_OFF      0x02
#define EVENT_FLAG_MID      0x04
#define EVENT_FLAG_POS1     0x08
#define EVENT_FLAG_POS2     0x10
#define EVENT_FLAG_POS3     0x20
#define EVENT_FLAG_POS4     0x40
TickValue  ticksWhenStopped[NUM_IO];

static unsigned char servoInBlock;

void initServos(void) {
    unsigned char io;
    for (io=0; io<NUM_IO; io++) {
        // try STOPPED state to reduced or correct power on jump
        if (NV->io[io].flags & FLAG_STARTUP) {
            servoState[io] = STOPPED;
        } else {
            servoState[io] = OFF;
        }
        ticksWhenStopped[io].Val = tickGet();
        currentPos[io] = targetPos[io] = ee_read(EE_OP_STATE+io);   // restore last known positions
        stepsPerPollSpeed[io] = 0;
    }
    
    // initialise the timers for one-shot mode with interrupts and clocked from Fosc/4
    T1GCONbits.TMR1GE = 0;      // gating disabled
    T1CONbits.TMR1CS = 0;       // clock source Fosc/4
    T1CONbits.T1CKPS = 2;       // 1:4 prescalar
    T1CONbits.SOSCEN = 1;       // clock source Fosc
    T1CONbits.RD16 = 1;         // 16bit read/write
    PIE1bits.TMR1IE = 1;        // enable interrupt
    
    T3GCONbits.TMR3GE = 0;      // gating disabled
    T3CONbits.TMR3CS = 0;       // clock source Fosc/4
    T3CONbits.T3CKPS = 2;       // 1:4 prescalar
    T3CONbits.SOSCEN = 1;       // clock source Fosc
    T3CONbits.RD16 = 1;         // 16bit read/write
    PIE2bits.TMR3IE = 1;        // enable interrupt
    
    servoInBlock = io -1;
    /* 
     * This will produce 1 pulse per servo (if its STARTUP flag is set).
     * This should reduce the power-on jump with some servo types.
     */
    /* Actually doesn't seem to help
    for (io=0; io<SERVOS_IN_BLOCK; io++) { 
        if ((NV->io[io].flags & FLAG_STARTUP) || (NV->io[io+SERVOS_IN_BLOCK].flags & FLAG_STARTUP)) {// THIS IS WRONG
            if (io != 0) {
                while (tickTimeSince(lastServoStartTime) > 5*HALF_MILLI_SECOND) { ; }  // 2.5ms delay
            }
            lastServoStartTime.Val = tickGet();
            servoInBlock = io -1;
            startServos();  // call every 2.5ms
        }
    }
     */
}
/**
 * This gets called ever approx 2.5ms so start the next set of servo pulses.
 * Checks that the servo isn't OFF
 * @param io
 */
void startServos(void) {
    unsigned char type;
    // increment block before calling setup so that block is left as the current block whilst the
    // timers expire
    servoInBlock++;
    if (servoInBlock >= SERVOS_IN_BLOCK) {
        servoInBlock = 0;
        pollServos();
    }
    type = NV->io[servoInBlock].type;
    if ((type == TYPE_SERVO) || (type == TYPE_BOUNCE) || (type == TYPE_MULTI)) {
        if (servoState[servoInBlock] != OFF) setupTimer1(servoInBlock);
    }
    type = NV->io[servoInBlock+SERVOS_IN_BLOCK].type;
    if ((type == TYPE_SERVO) || (type == TYPE_BOUNCE) || (type == TYPE_MULTI)) {
        if (servoState[servoInBlock+SERVOS_IN_BLOCK] != OFF) setupTimer3(servoInBlock+SERVOS_IN_BLOCK);
    }
}

/**
 * The setupTimer start the Timer as a on-shot for the servo output pulse of
 * a width of that required for the required position angle.
 * @param io
 */
void setupTimer1(unsigned char io) {
    WORD ticks = 0xFFFF-(POS2TICK_OFFSET + (WORD)POS2TICK_MULTIPLIER * currentPos[io]);
#ifdef __XC8
    TMR1 = -(POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io]);     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
#else
    TMR1H = ticks >> 8;     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
    TMR1L = ticks & 0xFF;
#endif
    // turn on output
    setOutputPin(io, !(NV->io[io].flags & FLAG_RESULT_ACTION_INVERTED));
    T1CONbits.TMR1ON = 1;       // enable Timer1
}
void setupTimer3(unsigned char io) {
    WORD ticks = 0xFFFF -(POS2TICK_OFFSET + (WORD)POS2TICK_MULTIPLIER * currentPos[io]);
#ifdef __XC8
    TMR3 = -(POS2TICK_OFFSET + POS2TICK_MULTIPLIER * currentPos[io]);     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
#else
    TMR3H = ticks >> 8;
    TMR3L = ticks & 0xFF;     // set the duration. Negative to count up to 0x0000 when it generates overflow interrupt
#endif
    // turn on output
    setOutputPin(io, !(NV->io[io].flags & FLAG_RESULT_ACTION_INVERTED));
    T3CONbits.TMR3ON = 1;       // enable Timer3
}


/**
 * These TimerDone routines are called when the on-shot timer expires so we
 * disable the timer and turn the output pin off. 
 * Don't recheck IO type here as it shouldn't be necessary and we want to be as quick as possible.
 */
void timer1DoneInterruptHandler(void) {
    T1CONbits.TMR1ON = 0;       // disable Timer1
    setOutputPin(servoInBlock, NV->io[servoInBlock].flags & FLAG_RESULT_ACTION_INVERTED);    
}

void timer3DoneInterruptHandler(void) {
    T3CONbits.TMR3ON = 0;       // disable Timer3
    setOutputPin(servoInBlock+SERVOS_IN_BLOCK, NV->io[servoInBlock+SERVOS_IN_BLOCK].flags & FLAG_RESULT_ACTION_INVERTED);    
}

/**
 * This handles the servo state machine and moves the servo towards the required
 * position and generates the Produced events. Called approx every 100ms i.e. 10 times a second.
 * Therefore to move a servo through 250 positions using a speed of 5 will take 5 seconds.
 * Typical speeds might be:
 * For a turnout: 25 = 1 second
 * For a signal:  40 = 0.6 seconds
 * For a level crossing gate: 5 = 5 seconds
 */
void pollServos(void) {
    unsigned char midway;
    BOOL beforeMidway;
    unsigned char io;
    unsigned char target;
    
    for (io=0; io<NUM_IO; io++) {
        switch (NV->io[io].type) {
            case TYPE_SERVO:
                midway = (NV->io[io].nv_io.nv_servo.servo_end_pos)/2 + 
                    (NV->io[io].nv_io.nv_servo.servo_start_pos)/2;
                beforeMidway=FALSE;
                switch (servoState[io]) {
                    case STARTING:
                        if (currentPos[io]==NV->io[io].nv_io.nv_servo.servo_start_pos) {
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_START(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        } else {
                            sendProducedEvent(ACTION_IO_PRODUCER_SERVO_END(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        }
                        servoState[io] = MOVING;
                        // fall through
                    case MOVING:
                        if (targetPos[io] > currentPos[io]) {
                            if (currentPos[io] < midway) {
                                beforeMidway = TRUE;
                            }
                            
                            if (stepsPerPollSpeed[io]) {
                                if (currentPos[io] + stepsPerPollSpeed[io] < currentPos[io]) {
                                    // will wrap
                                    currentPos[io] =255;
                                } else {
                                    currentPos[io] += stepsPerPollSpeed[io];
                                }
                            } else {
                                pollCount[io]--;
                                if (pollCount[io] == 0) {
                                    currentPos[io]++;
                                    pollCount[io] = pollsPerStepSpeed[io];
                                }
                            }
                            
                            if (currentPos[io] > targetPos[io]) {
                                currentPos[io] = targetPos[io];
                            }
                            if ((currentPos[io] >= midway) && beforeMidway) {
                                // passed through midway point
                                // we send an ACON/ACOF depending upon direction servo was moving
                                // This can then be used to drive frog switching relays
                                sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                        } else if (targetPos[io] < currentPos[io]) {
                            if (currentPos[io] > midway) {
                                beforeMidway = TRUE;
                            }
                            
                            if (stepsPerPollSpeed[io]) {
                                if (currentPos[io] - stepsPerPollSpeed[io] > currentPos[io]) {
                                    // would under wrap
                                    currentPos[io] = 0;
                                } else {
                                    currentPos[io] -= stepsPerPollSpeed[io];
                                }
                            } else {
                                pollCount[io]--;
                                if (pollCount[io] == 0) {
                                    currentPos[io]--;
                                    pollCount[io] = pollsPerStepSpeed[io];
                                }
                            }

                            if (currentPos[io] < targetPos[io]) {
                                currentPos[io] = targetPos[io];
                            }
                            if ((currentPos[io] <= midway) && beforeMidway) {
                                // passed through midway point
                                sendProducedEvent(ACTION_IO_PRODUCER_SERVO_MID(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                            }
                        }
                        if (targetPos[io] == currentPos[io]) {
                            servoState[io] = STOPPED;
                            ticksWhenStopped[io].Val = tickGet();
                            // send ON event or OFF
                            if (currentPos[io] == NV->io[io].nv_io.nv_servo.servo_start_pos) { //ON means move to End
                                sendProducedEvent(ACTION_IO_PRODUCER_SERVO_START(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            } else {
                                sendProducedEvent(ACTION_IO_PRODUCER_SERVO_END(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                            ee_write(EE_OP_STATE+io, currentPos[io]);
                        }
                        break;
                }
                break;
            case TYPE_BOUNCE:
                switch (servoState[io]) {
                    case STARTING:
                        initBounce(io);
                        servoState[io] = MOVING;
                        loopCount[io] = 0;
                        
                        // fall through
                    case MOVING:
                        loopCount[io]++;
                        if (loopCount[io] >= MAX_BOUNCE_LOOPS) {
                            servoState[io] = STOPPED;
                            ticksWhenStopped[io].Val = tickGet();
                            currentPos[io] = targetPos[io];
                            sendProducedEvent(ACTION_IO_PRODUCER_BOUNCE(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            ee_write(EE_OP_STATE+io, currentPos[io]);
                            break;
                        }
                        // Implement the bounce algorithm here
                        target = NV->io[io].nv_io.nv_bounce.bounce_upper_pos;
//                        if (NV->io[io].flags & FLAG_RESULT_ACTION_INVERTED) {
//                            target = NV->io[io].nv_io.nv_bounce.bounce_lower_pos;
//                        }
                        if (targetPos[io] == target) {
                            if (bounceUp(io)) {
                                servoState[io] = STOPPED;
                                ticksWhenStopped[io].Val = tickGet();
                                currentPos[io] = targetPos[io];
                                sendProducedEvent(ACTION_IO_PRODUCER_BOUNCE(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                                ee_write(EE_OP_STATE+io, currentPos[io]);
                            }
                        } else {
                            if (bounceDown(io)) {
                                servoState[io] = STOPPED;
                                ticksWhenStopped[io].Val = tickGet();
                                currentPos[io] = targetPos[io];
                                sendProducedEvent(ACTION_IO_PRODUCER_BOUNCE(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                                ee_write(EE_OP_STATE+io, currentPos[io]);
                            }
                        }
                        break;
                }
                break;
            case TYPE_MULTI:
                switch (servoState[io]) {
                    case STARTING:
                        if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos1) {
                            sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT1(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        }
                        if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos2) {
                            sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT2(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        }
                        if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos3) {
                            sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT3(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        }
                        if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos4) {
                            sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT4(io), NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED);
                        }
                        servoState[io] = MOVING;
                        // fall through
                    case MOVING:
                        if (targetPos[io] > currentPos[io]) {
                            if (stepsPerPollSpeed[io]) {
                                currentPos[io] += stepsPerPollSpeed[io];
                            } else {
                                pollCount[io]--;
                                if (pollCount[io] == 0) {
                                    currentPos[io]++;
                                    pollCount[io] = pollsPerStepSpeed[io];
                                }
                            }
                            if (currentPos[io] > targetPos[io]) {
                                currentPos[io] = targetPos[io];
                            }
                        } else if (targetPos[io] < currentPos[io]) {
                              
                            if (stepsPerPollSpeed[io]) {
                                currentPos[io] -= stepsPerPollSpeed[io];
                            } else {
                                pollCount[io]--;
                                if (pollCount[io] == 0) {
                                    currentPos[io]--;
                                    pollCount[io] = pollsPerStepSpeed[io];
                                }
                            }
                            if (currentPos[io] < targetPos[io]) {
                                currentPos[io] = targetPos[io];
                            }
                        }
                        if (targetPos[io] == currentPos[io]) {
                            servoState[io] = STOPPED;
                            ticksWhenStopped[io].Val = tickGet();
                            // MULTI only sends ON events. Work out which event
                            if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos1) {
                                sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT1(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                            if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos2) {
                                sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT2(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                            if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos3) {
                                sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT3(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                            if (currentPos[io] == NV->io[io].nv_io.nv_multi.multi_pos4) {
                                sendProducedEvent(ACTION_IO_PRODUCER_MULTI_AT4(io), !(NV->io[io].flags & FLAG_RESULT_EVENT_INVERTED));
                            }
                            ee_write(EE_OP_STATE+io, currentPos[io]);
                        }
                        break;
                }
                break;
        }
        switch (servoState[io]) {
        case STOPPED:
            // if we have been stopped for more than 1 sec then change to OFF
            // If FLAG_CUTOFF isn't set then we never reach OFF
            if (NV->io[io].flags & FLAG_CUTOFF) {
                if (tickTimeSince(ticksWhenStopped[io]) > ONE_SECOND) {
                    servoState[io] = OFF;
                }
            }
            break;
        case OFF:
            // output off
            // no need to do anything since if output is OFF we don't start the timer in startServos
            break;
        }
    }
}


/**
 * Start a servo moving to the required state. 
 * Called for SERVO types.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void startServoOutput(unsigned char io, CONSUMER_ACTION_T action) {
    switch (action) {
        case ACTION_IO_CONSUMER_3:  // SERVO OFF
            stepsPerPollSpeed[io] = NV->io[io].nv_io.nv_servo.servo_es_speed;
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO ON
            stepsPerPollSpeed[io] = NV->io[io].nv_io.nv_servo.servo_se_speed;
            break;
    }
    if (stepsPerPollSpeed[io] > PIVOT) {
        stepsPerPollSpeed[io] -= PIVOT;
        pollsPerStepSpeed[io] = 0;
    } else {
        pollsPerStepSpeed[io] = PIVOT - stepsPerPollSpeed[io] +1;
        pollCount[io] = 1;
        stepsPerPollSpeed[io] = 0;
    }
    servoState[io] = STARTING;
}

/**
 * Start a servo moving to the required state. 
 * Called for BOUNCE types.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void startBounceOutput(unsigned char io, CONSUMER_ACTION_T action) {
    switch (action) {
        case ACTION_IO_CONSUMER_3:  // SERVO OFF
            speed[io] = 0;
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO ON
            speed[io] = PULL_SPEED;
            break;
    }
    servoState[io] = STARTING;
    loopCount[io] = 0;
}

/**
 * Sets a servo multi-position output. generates produced events.
 * @param io
 * @param action
 */
void startMultiOutput(unsigned char io, CONSUMER_ACTION_T action) {

    stepsPerPollSpeed[io] = NV->servo_speed;
    if (stepsPerPollSpeed[io] > PIVOT) {
        stepsPerPollSpeed[io] -= PIVOT;
        pollsPerStepSpeed[io] = 0;
    } else {
        pollsPerStepSpeed[io] = PIVOT - stepsPerPollSpeed[io] +1;
        pollCount[io] = 1;
        stepsPerPollSpeed[io] = 0;   
    }
    servoState[io] = STARTING;
}

/**
 * Set a servo moving to the required state. 
 * Called for SERVO types.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setServoState(unsigned char io, CONSUMER_ACTION_T action) {
    switch (action) {
        case ACTION_IO_CONSUMER_3:  // SERVO OFF
            if (NV->io[io].flags & FLAG_TRIGGER_INVERTED) {
                targetPos[io] = NV->io[io].nv_io.nv_servo.servo_end_pos;
            } else {
                targetPos[io] = NV->io[io].nv_io.nv_servo.servo_start_pos;
            }
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO ON
            if (NV->io[io].flags & FLAG_TRIGGER_INVERTED) {
                targetPos[io] = NV->io[io].nv_io.nv_servo.servo_start_pos;
            } else {
                targetPos[io] = NV->io[io].nv_io.nv_servo.servo_end_pos;
            }
            break;
    }
}

/**
 * Set a servo moving to the required state. 
 * Called for BOUNCE types.
 * Handles inverted outputs and generates Produced events.
 * 
 * @param io
 * @param action
 */
void setBounceState(unsigned char io, CONSUMER_ACTION_T action) {
    switch (action) {
        case ACTION_IO_CONSUMER_3:  // SERVO OFF
            if (NV->io[io].flags & FLAG_TRIGGER_INVERTED) {
                targetPos[io] = NV->io[io].nv_io.nv_bounce.bounce_upper_pos;
            } else {
                targetPos[io] = NV->io[io].nv_io.nv_bounce.bounce_lower_pos;
            }
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO ON
            if (NV->io[io].flags & FLAG_TRIGGER_INVERTED) {
                targetPos[io] = NV->io[io].nv_io.nv_bounce.bounce_lower_pos;
            } else {
                targetPos[io] = NV->io[io].nv_io.nv_bounce.bounce_upper_pos;
            }
            break;
    }
}

/**
 * Sets a servo multi-position output. generates produced events.
 * @param io
 * @param action
 */
void setMultiState(unsigned char io, CONSUMER_ACTION_T action) {
    switch (action) {
        case ACTION_IO_CONSUMER_1:  // SERVO Position 1
            targetPos[io] = NV->io[io].nv_io.nv_multi.multi_pos1;
            break;
        case ACTION_IO_CONSUMER_2:  // SERVO Position 2
            targetPos[io] = NV->io[io].nv_io.nv_multi.multi_pos2;
            break;
        case ACTION_IO_CONSUMER_3:  // SERVO Position 3
            if (NV->io[io].nv_io.nv_multi.multi_num_pos >= 3) {
                targetPos[io] = NV->io[io].nv_io.nv_multi.multi_pos3;
            }
            break;
        case ACTION_IO_CONSUMER_4:  // SERVO Position 4
            if (NV->io[io].nv_io.nv_multi.multi_num_pos >= 4) {
                targetPos[io] = NV->io[io].nv_io.nv_multi.multi_pos4;
            }
            break;
    }
}

void setServoPosition(unsigned char io, unsigned char pos) {
    targetPos[io] = pos;
    currentPos[io] = pos;
}

#endif

