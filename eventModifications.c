/*
 * File:   eventModifications.c
 * Author: Jan Boen
 * 
 * This has been added to support the 1Track code.
 * Idea is however that it can be used as a generic enhancement to the CANMIO functionality
 * What it does is add a behavioural modifier for producing and consuming an event
 * For an event to be consumed the modifier can e.g. be used to NOT consume the event after all.
 * For an event to be produced it can be used to modify the event ID that will be used.
 * The above 2 examples are what is done in the 1Track use case.
 * The creativity of the developer limits what you can do.
 * 
 * Created on 14 Feb 2019, 16:26
 */
#include "cbus1Track.h"

/*
 * To Do
 * 
 * Optimise the data structure so it consumes less memory
 * 
*/

//Populate the eventModifiers data structure



//Handle produced event modifier
//If the modifier is less than 255 then use the value in this field as EN Lo


//Handle consumed event modifier
//If the modifier is larger than zero then don't consume the event
