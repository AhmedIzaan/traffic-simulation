/**
 * parking.cpp
 * 
 * Implementation of ParkingLot class with semaphore-based synchronization.
 */

#include "parking.h"

ParkingLot::ParkingLot() {
    sem_init(&spots, 0, PARKING_CAPACITY);
    sem_init(&queue, 0, PARKING_QUEUE_SIZE);
    pthread_mutex_init(&lock, nullptr);
    occupiedSpots = 0;
    waitingCount = 0;
    for (int i = 0; i < PARKING_CAPACITY; i++) spotOccupied[i] = false;
    for (int i = 0; i < PARKING_QUEUE_SIZE; i++) queueSlotOccupied[i] = false;
}

ParkingLot::~ParkingLot() {
    sem_destroy(&spots);
    sem_destroy(&queue);
    pthread_mutex_destroy(&lock);
}

int ParkingLot::enterQueue() {
    // Try to enter queue (non-blocking)
    if (sem_trywait(&queue) != 0) {
        return -1; // Queue full, skip parking
    }

    pthread_mutex_lock(&lock);
    waitingCount++;
    int queueIndex = -1;
    for (int i = 0; i < PARKING_QUEUE_SIZE; i++) {
        if (!queueSlotOccupied[i]) {
            queueSlotOccupied[i] = true;
            queueIndex = i;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
    return queueIndex;
}

int ParkingLot::waitForSpot(int queueIndex) {
    // Wait for spot (Blocking)
    sem_wait(&spots);

    // Leaving queue, entering spot
    sem_post(&queue);

    int spotIndex = -1;
    pthread_mutex_lock(&lock);
    waitingCount--;
    if (queueIndex >= 0 && queueIndex < PARKING_QUEUE_SIZE) {
        queueSlotOccupied[queueIndex] = false;
    }
    occupiedSpots++;
    
    // Find first free spot
    for (int i = 0; i < PARKING_CAPACITY; i++) {
        if (!spotOccupied[i]) {
            spotOccupied[i] = true;
            spotIndex = i;
            break;
        }
    }
    pthread_mutex_unlock(&lock);

    return spotIndex;
}

void ParkingLot::leave(int spotIndex) {
    pthread_mutex_lock(&lock);
    if (spotIndex >= 0 && spotIndex < PARKING_CAPACITY) {
        spotOccupied[spotIndex] = false;
    }
    occupiedSpots--;
    pthread_mutex_unlock(&lock);
    sem_post(&spots);
}

int ParkingLot::getOccupiedCount() {
    int count;
    pthread_mutex_lock(&lock);
    count = occupiedSpots;
    pthread_mutex_unlock(&lock);
    return count;
}

int ParkingLot::getWaitingCount() {
    int count;
    pthread_mutex_lock(&lock);
    count = waitingCount;
    pthread_mutex_unlock(&lock);
    return count;
}
