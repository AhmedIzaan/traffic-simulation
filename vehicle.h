/**
 * vehicle.h
 * 
 * Vehicle class and thread function declarations.
 */

#ifndef VEHICLE_H
#define VEHICLE_H

#include "simulation_types.h"
#include "parking.h"
#include <pthread.h>

class Vehicle {
public:
    int id;
    VehicleType type;
    float x, y;
    float speed;
    int pipeFd;
    ParkingLot* parkingLot;
    bool active;
    int startX, startY;
    int endX, endY;
    bool isInQueue;
    int queueIndex;
    bool isLeftParking; // true if using left (F11) parking lot

    Vehicle(int id, VehicleType type, int pipeFd, ParkingLot* lot = nullptr);

    void getColor(int& r, int& g, int& b);
    void sendUpdate(bool parked = false);
};

// Thread arguments structure
struct ThreadArgs {
    Vehicle* vehicle;
    pthread_mutex_t* lightMutex;
    TrafficLightState* lightState;
    float stopLineX;
    bool isCommuter;
};

// Movement helper function
bool moveTowards(float& currX, float& currY, float targetX, float targetY, float speed);

// Thread functions
void* vehicleThreadFunc(void* arg);
void* commuterThreadFunc(void* arg);
void* f11VehicleThreadFunc(void* arg);      // For F11 vehicles from right, using left parking
void* f11LocalVehicleThreadFunc(void* arg); // For F11 vehicles from left, using left parking

#endif // VEHICLE_H
