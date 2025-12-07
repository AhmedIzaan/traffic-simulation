/**
 * vehicle.cpp
 * 
 * Implementation of Vehicle class and thread functions.
 */

#include "vehicle.h"
#include <unistd.h>
#include <cmath>
#include <cstdlib>

Vehicle::Vehicle(int id, VehicleType type, int pipeFd, ParkingLot* lot)
    : id(id), type(type), pipeFd(pipeFd), parkingLot(lot), active(true),
      isInQueue(false), queueIndex(-1), isLeftParking(false) {
    speed = 2.0f;
    if (type == VehicleType::AMBULANCE || type == VehicleType::FIRETRUCK) {
        speed = 4.0f;
    } else if (type == VehicleType::TRACTOR) {
        speed = 1.0f;
    }
    x = 0;
    y = 0;
}

void Vehicle::getColor(int& r, int& g, int& b) {
    switch (type) {
        case VehicleType::AMBULANCE: r = 255; g = 255; b = 255; break; // White
        case VehicleType::FIRETRUCK: r = 255; g = 0;   b = 0;   break; // Red
        case VehicleType::BUS:       r = 0;   g = 0;   b = 255; break; // Blue
        case VehicleType::CAR:       r = 0;   g = 255; b = 0;   break; // Green
        case VehicleType::BIKE:      r = 255; g = 255; b = 0;   break; // Yellow
        case VehicleType::TRACTOR:   r = 100; g = 100; b = 100; break; // Grey
    }
}

void Vehicle::sendUpdate(bool parked) {
    PipeMessage msg;
    msg.magic = MSG_MAGIC;
    msg.type = PipeMessage::VEHICLE_UPDATE;
    msg.data.vehicle.id = id;
    msg.data.vehicle.x = x;
    msg.data.vehicle.y = y;
    msg.data.vehicle.isActive = active;
    msg.data.vehicle.isParked = parked;
    msg.data.vehicle.isInQueue = isInQueue;
    msg.data.vehicle.queueIndex = queueIndex;
    msg.data.vehicle.isLeftParking = isLeftParking;
    msg.data.vehicle.type = type;

    int r, g, b;
    getColor(r, g, b);
    msg.data.vehicle.colorR = r;
    msg.data.vehicle.colorG = g;
    msg.data.vehicle.colorB = b;

    write(pipeFd, &msg, sizeof(msg));

    // Also send parking queue update if this vehicle has a parking lot reference
    if (parkingLot != nullptr) {
        PipeMessage pMsg;
        pMsg.magic = MSG_MAGIC;
        pMsg.type = PipeMessage::PARKING_UPDATE;
        pMsg.data.parking.intersectionId = isLeftParking ? 11 : 10;
        pMsg.data.parking.waitingCount = parkingLot->getWaitingCount();
        write(pipeFd, &pMsg, sizeof(pMsg));
    }
}

bool moveTowards(float& currX, float& currY, float targetX, float targetY, float speed) {
    float dx = targetX - currX;
    float dy = targetY - currY;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < speed) {
        currX = targetX;
        currY = targetY;
        return true; // Reached
    }

    float ratio = speed / dist;
    currX += dx * ratio;
    currY += dy * ratio;
    return false;
}

// Thread function for commuter vehicles (start at F11, want to park at F10)
void* commuterThreadFunc(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Vehicle* v = args->vehicle;

    float f11StopLine = 960.0f;
    float f10StopLine = 360.0f;

    // Phase 1: Drive to F11 stop line
    while (!moveTowards(v->x, v->y, f11StopLine, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    // Phase 2: Brief pause at F11
    usleep(500000);

    // Phase 3: Cross F11 and drive to F10
    while (!moveTowards(v->x, v->y, f10StopLine, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    // Phase 4: Wait for F10's green light
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);

        if (state == TrafficLightState::GREEN || 
            v->type == VehicleType::AMBULANCE || 
            v->type == VehicleType::FIRETRUCK) {
            break;
        }
        v->sendUpdate();
        usleep(100000);
    }

    // Phase 5: Try to park
    bool willPark = (v->parkingLot != nullptr) && 
                    (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);

    if (willPark) {
        float queueX = 300.0f;
        float queueY = 320.0f;
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        int queueIdx = v->parkingLot->enterQueue();

        if (queueIdx != -1) {
            v->isInQueue = true;
            v->queueIndex = queueIdx;

            float queueBoxX = 425.0f + queueIdx * 40.0f;
            float queueBoxY = 325.0f;

            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate();

            int spotIndex = v->parkingLot->waitForSpot(queueIdx);

            v->isInQueue = false;
            v->queueIndex = -1;

            int row = spotIndex / 5;
            int col = spotIndex % 5;
            float parkX = 230.0f + col * 40.0f;
            float parkY = 185.0f + row * 60.0f;

            while (!moveTowards(v->x, v->y, parkX, parkY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }

            v->sendUpdate(true);
            sleep(PARKING_DURATION_SECONDS);

            v->parkingLot->leave(spotIndex);

            while (!moveTowards(v->x, v->y, 300.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
    }

    // Phase 6: Exit to the left
    while (!moveTowards(v->x, v->y, 0.0f, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    v->active = false;
    v->sendUpdate();

    delete args;
    return nullptr;
}

void* vehicleThreadFunc(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Vehicle* v = args->vehicle;

    float targetX = v->endX;
    float targetY = v->endY;
    float stopLineX = args->stopLineX;

    // Phase 1: Move to Stop Line
    while (!moveTowards(v->x, v->y, stopLineX, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    // Phase 2: Check Light
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);

        if (state == TrafficLightState::GREEN || 
            v->type == VehicleType::AMBULANCE || 
            v->type == VehicleType::FIRETRUCK) {
            break;
        }
        usleep(100000);
    }

    // Phase 3: Cross Intersection or Park
    bool willPark = (v->parkingLot != nullptr) && 
                    (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);

    if (willPark) {
        float queueX = 300.0f;
        float queueY = 320.0f;
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        int queueIdx = v->parkingLot->enterQueue();

        if (queueIdx != -1) {
            v->isInQueue = true;
            v->queueIndex = queueIdx;

            float queueBoxX = 425.0f + queueIdx * 40.0f;
            float queueBoxY = 325.0f;

            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate();

            int spotIndex = v->parkingLot->waitForSpot(queueIdx);

            v->isInQueue = false;
            v->queueIndex = -1;

            int row = spotIndex / 5;
            int col = spotIndex % 5;
            float parkX = 230.0f + col * 40.0f;
            float parkY = 185.0f + row * 60.0f;

            while (!moveTowards(v->x, v->y, parkX, parkY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }

            v->sendUpdate(true);
            sleep(PARKING_DURATION_SECONDS);

            v->parkingLot->leave(spotIndex);

            while (!moveTowards(v->x, v->y, 300.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
    }

    // Phase 4: Move to End
    while (!moveTowards(v->x, v->y, targetX, targetY, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    v->active = false;
    v->sendUpdate();

    delete args;
    return nullptr;
}

// Thread function for F11 vehicles (start at right, can use left parking lot)
void* f11VehicleThreadFunc(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Vehicle* v = args->vehicle;

    float stopLineX = args->stopLineX; // F11 stop line at 960
    float targetX = v->endX;
    float targetY = v->endY;

    // Phase 1: Move to Stop Line
    while (!moveTowards(v->x, v->y, stopLineX, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    // Phase 2: Check Light
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);

        if (state == TrafficLightState::GREEN || 
            v->type == VehicleType::AMBULANCE || 
            v->type == VehicleType::FIRETRUCK) {
            break;
        }
        usleep(100000);
    }

    // Phase 3: Cross Intersection or Park at left parking lot
    bool willPark = (v->parkingLot != nullptr) && 
                    (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);

    if (willPark) {
        // Move towards left parking queue area
        float queueX = 900.0f;
        float queueY = 320.0f;
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        int queueIdx = v->parkingLot->enterQueue();

        if (queueIdx != -1) {
            v->isInQueue = true;
            v->queueIndex = queueIdx;

            // Left parking queue position (mirrored from right)
            float queueBoxX = 775.0f - queueIdx * 40.0f;
            float queueBoxY = 325.0f;

            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate();

            int spotIndex = v->parkingLot->waitForSpot(queueIdx);

            v->isInQueue = false;
            v->queueIndex = -1;

            // Left parking lot spot positions (mirrored)
            int row = spotIndex / 5;
            int col = spotIndex % 5;
            float parkX = 970.0f - col * 40.0f;
            float parkY = 185.0f + row * 60.0f;

            while (!moveTowards(v->x, v->y, parkX, parkY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }

            v->sendUpdate(true);
            sleep(PARKING_DURATION_SECONDS);

            v->parkingLot->leave(spotIndex);

            // Exit from parking back to road
            while (!moveTowards(v->x, v->y, 900.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
    }

    // Phase 4: Move to End (left side)
    while (!moveTowards(v->x, v->y, targetX, targetY, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    v->active = false;
    v->sendUpdate();

    delete args;
    return nullptr;
}

// Thread function for F11 local vehicles (start at left, going right, can use left parking lot)
void* f11LocalVehicleThreadFunc(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    Vehicle* v = args->vehicle;

    float stopLineX = args->stopLineX; // F11 stop line at 840
    float targetX = v->endX;
    float targetY = v->endY;

    // Phase 1: Move to Stop Line (before F11 intersection)
    while (!moveTowards(v->x, v->y, stopLineX, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    // Phase 2: Check Light
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);

        if (state == TrafficLightState::GREEN || 
            v->type == VehicleType::AMBULANCE || 
            v->type == VehicleType::FIRETRUCK) {
            break;
        }
        usleep(100000);
    }

    // Phase 3: Cross Intersection or Park at left parking lot
    bool willPark = (v->parkingLot != nullptr) && 
                    (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);

    if (willPark) {
        // Move towards left parking queue area (above F11 intersection)
        float queueX = 900.0f;
        float queueY = 320.0f;
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        int queueIdx = v->parkingLot->enterQueue();

        if (queueIdx != -1) {
            v->isInQueue = true;
            v->queueIndex = queueIdx;

            // Left parking queue position
            float queueBoxX = 775.0f - queueIdx * 40.0f;
            float queueBoxY = 325.0f;

            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate();

            int spotIndex = v->parkingLot->waitForSpot(queueIdx);

            v->isInQueue = false;
            v->queueIndex = -1;

            // Left parking lot spot positions
            int row = spotIndex / 5;
            int col = spotIndex % 5;
            float parkX = 970.0f - col * 40.0f;
            float parkY = 185.0f + row * 60.0f;

            while (!moveTowards(v->x, v->y, parkX, parkY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }

            v->sendUpdate(true);
            sleep(PARKING_DURATION_SECONDS);

            v->parkingLot->leave(spotIndex);

            // Exit from parking back to road
            while (!moveTowards(v->x, v->y, 900.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
    }

    // Phase 4: Move to End (right side)
    while (!moveTowards(v->x, v->y, targetX, targetY, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }

    v->active = false;
    v->sendUpdate();

    delete args;
    return nullptr;
}
