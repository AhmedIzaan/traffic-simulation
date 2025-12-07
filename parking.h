/**
 * parking.h
 * 
 * ParkingLot class with semaphore-based synchronization.
 */

#ifndef PARKING_H
#define PARKING_H

#include "simulation_types.h"
#include <semaphore.h>
#include <pthread.h>

class ParkingLot {
private:
    sem_t spots;
    sem_t queue;
    pthread_mutex_t lock;
    int occupiedSpots;
    int waitingCount;
    bool spotOccupied[PARKING_CAPACITY];
    bool queueSlotOccupied[PARKING_QUEUE_SIZE];

public:
    ParkingLot();
    ~ParkingLot();

    // Try to enter the queue. Returns queue index (0-4) or -1 if queue full
    int enterQueue();

    // Wait for a parking spot (blocking). Returns spot index (0-9)
    int waitForSpot(int queueIndex);

    // Leave a parking spot
    void leave(int spotIndex);

    // Getters
    int getOccupiedCount();
    int getWaitingCount();
};

#endif // PARKING_H
