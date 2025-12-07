/**
 * controller.h
 * 
 * Traffic controller function declarations for F10 and F11.
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

// F10 Controller - Manages intersection F10 and parking lot
void trafficControllerF10(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd);

// F11 Controller - Manages intersection F11 and emergency handling
void trafficControllerF11(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd);

#endif // CONTROLLER_H
