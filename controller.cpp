/**
 * controller.cpp
 * 
 * Implementation of traffic controllers for F10 and F11 intersections.
 */

#include "controller.h"
#include "simulation_types.h"
#include "parking.h"
#include "vehicle.h"
#include <iostream>
#include <vector>
#include <unistd.h>
#include <cstdlib>

using namespace std;

void trafficControllerF10(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd) {
    ParkingLot parkingLot;
    TrafficLightState lightState = TrafficLightState::RED;
    pthread_mutex_t lightMutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<pthread_t> threads;
    std::vector<Vehicle*> vehicles;

    setNonBlocking(cmdPipeFd);

    int vehicleIdCounter = 0;
    int commuterIdCounter = 50;

    // Helper lambda to spawn a local vehicle
    auto spawnLocalVehicle = [&](VehicleType type, bool goToParking = true) {
        Vehicle* v = new Vehicle(vehicleIdCounter++, type, writePipeFd, &parkingLot);
        v->x = 0;
        v->y = 400;
        v->endX = 1200;
        v->endY = 400;

        vehicles.push_back(v);

        ThreadArgs* args = new ThreadArgs();
        args->vehicle = v;
        args->lightMutex = &lightMutex;
        args->lightState = &lightState;
        args->stopLineX = 240.0f;
        args->isCommuter = false;

        pthread_t tid;
        pthread_create(&tid, nullptr, vehicleThreadFunc, args);
        threads.push_back(tid);
    };

    // Helper lambda to spawn commuter vehicle
    auto spawnCommuterVehicle = [&](VehicleType type) {
        Vehicle* v = new Vehicle(commuterIdCounter++, type, writePipeFd, &parkingLot);
        v->x = 1200;
        v->y = 400;
        v->endX = 0;
        v->endY = 400;

        vehicles.push_back(v);

        ThreadArgs* args = new ThreadArgs();
        args->vehicle = v;
        args->lightMutex = &lightMutex;
        args->lightState = &lightState;
        args->stopLineX = 360.0f;
        args->isCommuter = true;

        pthread_t tid;
        pthread_create(&tid, nullptr, commuterThreadFunc, args);
        threads.push_back(tid);
    };

    // Spawn initial vehicles - 3 local + 2 commuters
    for (int i = 0; i < 3; ++i) {
        VehicleType type = (VehicleType)(rand() % 6);
        spawnLocalVehicle(type);
        usleep(rand() % 1000000 + 500000);
    }

    for (int i = 0; i < 2; ++i) {
        VehicleType type = (rand() % 2 == 0) ? VehicleType::CAR : VehicleType::BIKE;
        spawnCommuterVehicle(type);
        usleep(rand() % 1000000 + 500000);
    }

    // Traffic Light Cycle with command checking
    while (true) {
        // Check for commands (non-blocking)
        CommandMessage cmdMsg;
        if (read(cmdPipeFd, &cmdMsg, sizeof(cmdMsg)) == sizeof(cmdMsg) && cmdMsg.magic == CMD_MAGIC) {
            switch (cmdMsg.command) {
                case ScenarioCommand::GREEN_WAVE: {
                    cout << "[F10] Scenario A: Green Wave - Spawning Ambulance" << endl;
                    spawnLocalVehicle(VehicleType::AMBULANCE);

                    CoordinationMessage coordMsg;
                    coordMsg.type = CoordinationMessage::EMERGENCY_APPROACHING;
                    coordMsg.sourceIntersection = 10;
                    write(writeCoordFd, &coordMsg, sizeof(coordMsg));
                    break;
                }
                case ScenarioCommand::PARKING_FULL: {
                    cout << "[F10] Scenario B: Parking Saturation - Spawning 16 Cars" << endl;
                    for (int i = 0; i < 16; ++i) {
                        spawnLocalVehicle(VehicleType::CAR);
                        usleep(200000);
                    }
                    break;
                }
                case ScenarioCommand::GRIDLOCK: {
                    cout << "[F10] Scenario C: Gridlock - Spawning from all directions" << endl;
                    for (int i = 0; i < 5; ++i) {
                        VehicleType type = (VehicleType)(rand() % 4 + 2);
                        spawnLocalVehicle(type);
                        usleep(100000);
                    }
                    for (int i = 0; i < 5; ++i) {
                        VehicleType type = (rand() % 2 == 0) ? VehicleType::CAR : VehicleType::BIKE;
                        spawnCommuterVehicle(type);
                        usleep(100000);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Red phase
        pthread_mutex_lock(&lightMutex);
        lightState = TrafficLightState::RED;
        pthread_mutex_unlock(&lightMutex);

        PipeMessage msg;
        msg.magic = MSG_MAGIC;
        msg.type = PipeMessage::LIGHT_UPDATE;
        msg.data.light.intersectionId = 10;
        msg.data.light.state = TrafficLightState::RED;
        write(writePipeFd, &msg, sizeof(msg));

        // Split sleep to check commands more frequently
        for (int i = 0; i < 6; ++i) {
            usleep(500000);
            if (read(cmdPipeFd, &cmdMsg, sizeof(cmdMsg)) == sizeof(cmdMsg) && cmdMsg.magic == CMD_MAGIC) {
                if (cmdMsg.command == ScenarioCommand::GREEN_WAVE) {
                    spawnLocalVehicle(VehicleType::AMBULANCE);
                    CoordinationMessage coordMsg;
                    coordMsg.type = CoordinationMessage::EMERGENCY_APPROACHING;
                    coordMsg.sourceIntersection = 10;
                    write(writeCoordFd, &coordMsg, sizeof(coordMsg));
                } else if (cmdMsg.command == ScenarioCommand::PARKING_FULL) {
                    for (int j = 0; j < 16; ++j) {
                        spawnLocalVehicle(VehicleType::CAR);
                        usleep(200000);
                    }
                } else if (cmdMsg.command == ScenarioCommand::GRIDLOCK) {
                    for (int j = 0; j < 5; ++j) {
                        spawnLocalVehicle((VehicleType)(rand() % 4 + 2));
                        spawnCommuterVehicle((rand() % 2 == 0) ? VehicleType::CAR : VehicleType::BIKE);
                        usleep(100000);
                    }
                }
            }
        }

        // Green phase
        pthread_mutex_lock(&lightMutex);
        lightState = TrafficLightState::GREEN;
        pthread_mutex_unlock(&lightMutex);

        msg.data.light.state = TrafficLightState::GREEN;
        write(writePipeFd, &msg, sizeof(msg));

        for (int i = 0; i < 6; ++i) {
            usleep(500000);
        }

        // Send Parking Queue Update
        PipeMessage pMsg;
        pMsg.magic = MSG_MAGIC;
        pMsg.type = PipeMessage::PARKING_UPDATE;
        pMsg.data.parking.intersectionId = 10;
        pMsg.data.parking.waitingCount = parkingLot.getWaitingCount();
        write(writePipeFd, &pMsg, sizeof(pMsg));
    }

    for (auto tid : threads) {
        pthread_join(tid, nullptr);
    }
}

void trafficControllerF11(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd) {
    ParkingLot parkingLot; // Left-side parking lot for F11
    TrafficLightState lightState = TrafficLightState::RED;
    pthread_mutex_t lightMutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<pthread_t> threads;
    std::vector<Vehicle*> vehicles;

    setNonBlocking(cmdPipeFd);
    setNonBlocking(readCoordFd);

    int vehicleIdCounter = 100;
    int localIdCounter = 150; // For vehicles spawning from left side at F11
    bool emergencyMode = false;

    // Helper lambda to spawn vehicle from right (going left) - can use left parking
    auto spawnVehicle = [&](VehicleType type, float yPos = 400.0f) {
        Vehicle* v = new Vehicle(vehicleIdCounter++, type, writePipeFd, &parkingLot);
        v->x = 1200;
        v->y = yPos;
        v->endX = 0;
        v->endY = yPos;
        v->isLeftParking = true; // Will use left parking lot

        vehicles.push_back(v);

        ThreadArgs* args = new ThreadArgs();
        args->vehicle = v;
        args->lightMutex = &lightMutex;
        args->lightState = &lightState;
        args->stopLineX = 960.0f;
        args->isCommuter = false;

        pthread_t tid;
        pthread_create(&tid, nullptr, f11VehicleThreadFunc, args);
        threads.push_back(tid);
    };

    // Helper lambda to spawn vehicle from left (going right) at F11 - can use left parking
    auto spawnLocalVehicle = [&](VehicleType type) {
        Vehicle* v = new Vehicle(localIdCounter++, type, writePipeFd, &parkingLot);
        v->x = 0;
        v->y = 400;
        v->endX = 1200;
        v->endY = 400;
        v->isLeftParking = true; // Will use left parking lot

        vehicles.push_back(v);

        ThreadArgs* args = new ThreadArgs();
        args->vehicle = v;
        args->lightMutex = &lightMutex;
        args->lightState = &lightState;
        args->stopLineX = 840.0f;
        args->isCommuter = false;

        pthread_t tid;
        pthread_create(&tid, nullptr, f11LocalVehicleThreadFunc, args);
        threads.push_back(tid);
    };

    // Spawn initial vehicles - some from right, some from left
    for (int i = 0; i < 3; ++i) {
        VehicleType type = (VehicleType)(rand() % 6);
        spawnVehicle(type);
        usleep(rand() % 1500000 + 500000);
    }
    for (int i = 0; i < 2; ++i) {
        VehicleType type = (VehicleType)(rand() % 6);
        spawnLocalVehicle(type);
        usleep(rand() % 1500000 + 500000);
    }

    while (true) {
        // Check for emergency signal from F10
        CoordinationMessage coordMsg;
        if (read(readCoordFd, &coordMsg, sizeof(coordMsg)) == sizeof(coordMsg)) {
            if (coordMsg.type == CoordinationMessage::EMERGENCY_APPROACHING) {
                cout << "[F11] Emergency signal received! Switching to GREEN" << endl;
                emergencyMode = true;

                pthread_mutex_lock(&lightMutex);
                lightState = TrafficLightState::GREEN;
                pthread_mutex_unlock(&lightMutex);

                PipeMessage msg;
                msg.magic = MSG_MAGIC;
                msg.type = PipeMessage::LIGHT_UPDATE;
                msg.data.light.intersectionId = 11;
                msg.data.light.state = TrafficLightState::GREEN;
                write(writePipeFd, &msg, sizeof(msg));

                sleep(5);
                emergencyMode = false;
            }
        }

        // Check for commands from parent
        CommandMessage cmdMsg;
        if (read(cmdPipeFd, &cmdMsg, sizeof(cmdMsg)) == sizeof(cmdMsg) && cmdMsg.magic == CMD_MAGIC) {
            if (cmdMsg.command == ScenarioCommand::PARKING_FULL) {
                cout << "[F11] Scenario B: Parking Saturation - Spawning 16 Cars" << endl;
                for (int i = 0; i < 16; ++i) {
                    spawnVehicle(VehicleType::CAR);
                    usleep(200000);
                }
            } else if (cmdMsg.command == ScenarioCommand::GRIDLOCK) {
                cout << "[F11] Scenario C: Gridlock - Spawning vehicles" << endl;
                for (int i = 0; i < 5; ++i) {
                    VehicleType type = (VehicleType)(rand() % 4 + 2);
                    spawnVehicle(type, 400.0f);
                    usleep(100000);
                }
                for (int i = 0; i < 3; ++i) {
                    VehicleType type = (VehicleType)(rand() % 4 + 2);
                    spawnLocalVehicle(type);
                    usleep(100000);
                }
            }
        }

        if (!emergencyMode) {
            // Red phase
            pthread_mutex_lock(&lightMutex);
            lightState = TrafficLightState::RED;
            pthread_mutex_unlock(&lightMutex);

            PipeMessage msg;
            msg.magic = MSG_MAGIC;
            msg.type = PipeMessage::LIGHT_UPDATE;
            msg.data.light.intersectionId = 11;
            msg.data.light.state = TrafficLightState::RED;
            write(writePipeFd, &msg, sizeof(msg));

            for (int i = 0; i < 6; ++i) {
                usleep(500000);
                if (read(readCoordFd, &coordMsg, sizeof(coordMsg)) == sizeof(coordMsg)) {
                    if (coordMsg.type == CoordinationMessage::EMERGENCY_APPROACHING) {
                        cout << "[F11] Emergency during RED! Switching to GREEN" << endl;
                        pthread_mutex_lock(&lightMutex);
                        lightState = TrafficLightState::GREEN;
                        pthread_mutex_unlock(&lightMutex);

                        msg.data.light.state = TrafficLightState::GREEN;
                        write(writePipeFd, &msg, sizeof(msg));
                        sleep(5);
                        break;
                    }
                }
            }

            // Green phase
            pthread_mutex_lock(&lightMutex);
            lightState = TrafficLightState::GREEN;
            pthread_mutex_unlock(&lightMutex);

            msg.data.light.state = TrafficLightState::GREEN;
            write(writePipeFd, &msg, sizeof(msg));

            for (int i = 0; i < 6; ++i) {
                usleep(500000);
            }

            // Send Parking Queue Update for F11
            PipeMessage pMsg;
            pMsg.magic = MSG_MAGIC;
            pMsg.type = PipeMessage::PARKING_UPDATE;
            pMsg.data.parking.intersectionId = 11;
            pMsg.data.parking.waitingCount = parkingLot.getWaitingCount();
            write(writePipeFd, &pMsg, sizeof(pMsg));
        }
    }

    for (auto tid : threads) {
        pthread_join(tid, nullptr);
    }
}
