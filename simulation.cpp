/**
 * Traffic Simulation Project
 * 
 * Features:
 * - Multi-threading (pthreads) for Vehicles
 * - Multi-processing (fork) for Traffic Controllers (F10, F11)
 * - IPC (Pipes) for communication
 * - Synchronization (Mutexes, Semaphores)
 * - Visualization (SFML)
 */

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <vector>
#include <iostream>
#include <cmath>
#include <random>
#include <deque>

// ==========================================
// Constants & Configuration
// ==========================================

const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 800;
const char* WINDOW_TITLE = "Traffic Simulation: F10 & F11";

// Parking Configuration
const int PARKING_CAPACITY = 10;
const int PARKING_QUEUE_SIZE = 5;

// Simulation Constants
const int NUM_VEHICLES_PER_CONTROLLER = 8; // Total ~16 vehicles
const int VEHICLE_SPEED_MS = 50; // Sleep time in ms for movement

// Pipe Magic Number for validation
const uint32_t MSG_MAGIC = 0xCAFEBABE;

// ==========================================
// Enums & Data Structures
// ==========================================

enum class VehicleType {
    AMBULANCE,  // High Priority
    FIRETRUCK,  // High Priority
    BUS,        // Medium Priority
    CAR,        // Low Priority, Can Park
    BIKE,       // Low Priority, Can Park
    TRACTOR     // Low Priority
};

enum class TrafficLightState {
    RED,
    GREEN
};

// Direction of travel
enum class Direction {
    NORTH_SOUTH,
    EAST_WEST,
    WEST_EAST
};

// Structure sent over the pipe from Controller -> Visualizer
struct VehicleState {
    int id;
    float x;
    float y;
    int colorR, colorG, colorB;
    bool isActive;
    bool isParked;
    bool isInQueue;
    int queueIndex; // 0-4, or -1 if not in queue
    VehicleType type;
};

// Structure for Traffic Light updates
struct TrafficLightUpdate {
    int intersectionId; // 10 for F10, 11 for F11
    TrafficLightState state;
};

// Wrapper for all pipe messages to Visualizer
struct PipeMessage {
    uint32_t magic;
    enum Type { VEHICLE_UPDATE, LIGHT_UPDATE, PARKING_UPDATE } type;
    
    union {
        VehicleState vehicle;
        TrafficLightUpdate light;
        int waitingCount;
    } data;
};

// Coordination message between F10 <-> F11
struct CoordinationMessage {
    enum Type { EMERGENCY_APPROACHING, CLEAR_INTERSECTION } type;
    int sourceIntersection;
};

// Command message from Parent to Controllers
enum class ScenarioCommand {
    NONE = 0,
    GREEN_WAVE = 1,      // Scenario A: Spawn ambulance, signal F11
    PARKING_FULL = 2,    // Scenario B: Spawn 16 cars to fill parking
    GRIDLOCK = 3         // Scenario C: Spawn cars from all directions
};

struct CommandMessage {
    uint32_t magic;
    ScenarioCommand command;
};

const uint32_t CMD_MAGIC = 0xDEADBEEF;

// ==========================================
// Global Synchronization Primitives (Process Local)
// ==========================================

// Note: These are initialized in the child processes
// We don't need them in global scope for the parent, but good to declare types.

// ==========================================
// Forward Declarations
// ==========================================

void* vehicleThreadFunc(void* arg);
void trafficControllerF10(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd);
void trafficControllerF11(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd);

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
// ==========================================
// Classes
// ==========================================

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
    ParkingLot() {
        sem_init(&spots, 0, PARKING_CAPACITY);
        sem_init(&queue, 0, PARKING_QUEUE_SIZE);
        pthread_mutex_init(&lock, nullptr);
        occupiedSpots = 0;
        waitingCount = 0;
        for(int i=0; i<PARKING_CAPACITY; i++) spotOccupied[i] = false;
        for(int i=0; i<PARKING_QUEUE_SIZE; i++) queueSlotOccupied[i] = false;
    }

    ~ParkingLot() {
        sem_destroy(&spots);
        sem_destroy(&queue);
        pthread_mutex_destroy(&lock);
    }

    // Try to enter the queue. Returns queue index (0-4) or -1 if queue full
    int enterQueue() {
        // Try to enter queue
        if (sem_trywait(&queue) != 0) {
            return -1; // Queue full, skip parking
        }

        pthread_mutex_lock(&lock);
        waitingCount++;
        int queueIndex = -1;
        for(int i=0; i<PARKING_QUEUE_SIZE; i++) {
            if(!queueSlotOccupied[i]) {
                queueSlotOccupied[i] = true;
                queueIndex = i;
                break;
            }
        }
        pthread_mutex_unlock(&lock);
        return queueIndex;
    }

    // Wait for a parking spot (blocking). Returns spot index (0-9)
    int waitForSpot(int queueIndex) {
        // Wait for spot (Blocking)
        sem_wait(&spots);

        // Leaving queue, entering spot
        sem_post(&queue);

        int spotIndex = -1;
        pthread_mutex_lock(&lock);
        waitingCount--;
        if(queueIndex >= 0 && queueIndex < PARKING_QUEUE_SIZE) {
            queueSlotOccupied[queueIndex] = false;
        }
        occupiedSpots++;
        
        // Find first free spot
        for(int i=0; i<PARKING_CAPACITY; i++) {
            if(!spotOccupied[i]) {
                spotOccupied[i] = true;
                spotIndex = i;
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        return spotIndex;
    }

    void leave(int spotIndex) {
        pthread_mutex_lock(&lock);
        if(spotIndex >= 0 && spotIndex < PARKING_CAPACITY) {
            spotOccupied[spotIndex] = false;
        }
        occupiedSpots--;
        pthread_mutex_unlock(&lock);
        sem_post(&spots);
    }
    
    int getOccupiedCount() {
        int count;
        pthread_mutex_lock(&lock);
        count = occupiedSpots;
        pthread_mutex_unlock(&lock);
        return count;
    }

    int getWaitingCount() {
        int count;
        pthread_mutex_lock(&lock);
        count = waitingCount;
        pthread_mutex_unlock(&lock);
        return count;
    }
};

class Vehicle {
public:
    int id;
    VehicleType type;
    float x, y;
    float speed;
    int pipeFd;
    bool active;
    ParkingLot* parkingLot; // Only for F10 vehicles
    int startX, startY;
    int endX, endY;
    bool isInQueue;
    int queueIndex;

    Vehicle(int id, VehicleType type, int pipeFd, ParkingLot* lot = nullptr) 
        : id(id), type(type), pipeFd(pipeFd), parkingLot(lot), active(true),
          isInQueue(false), queueIndex(-1) {
        speed = 2.0f;
        if (type == VehicleType::AMBULANCE || type == VehicleType::FIRETRUCK) {
            speed = 4.0f;
        } else if (type == VehicleType::TRACTOR) {
            speed = 1.0f;
        }
        
        // Initial positions will be set by the controller before running
        x = 0; y = 0;
    }

    void getColor(int& r, int& g, int& b) {
        switch(type) {
            case VehicleType::AMBULANCE: r=255; g=255; b=255; break; // White
            case VehicleType::FIRETRUCK: r=255; g=0; b=0; break; // Red
            case VehicleType::BUS: r=0; g=0; b=255; break; // Blue
            case VehicleType::CAR: r=0; g=255; b=0; break; // Green
            case VehicleType::BIKE: r=255; g=255; b=0; break; // Yellow
            case VehicleType::TRACTOR: r=100; g=100; b=100; break; // Grey
        }
    }

    void sendUpdate(bool parked = false) {
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
        msg.data.vehicle.type = type;
        
        int r, g, b;
        getColor(r, g, b);
        msg.data.vehicle.colorR = r;
        msg.data.vehicle.colorG = g;
        msg.data.vehicle.colorB = b;

        write(pipeFd, &msg, sizeof(msg));

        // Also send parking queue update if this vehicle is managed by F10 (has parking lot)
        if (parkingLot != nullptr) {
            PipeMessage pMsg;
            pMsg.magic = MSG_MAGIC;
            pMsg.type = PipeMessage::PARKING_UPDATE;
            pMsg.data.waitingCount = parkingLot->getWaitingCount();
            write(pipeFd, &pMsg, sizeof(pMsg));
        }
    }
};

struct ThreadArgs {
    Vehicle* vehicle;
    pthread_mutex_t* lightMutex;
    TrafficLightState* lightState;
    float stopLineX;
    bool isCommuter; // Vehicle starts from F11 side but wants to park at F10
};

// ==========================================
// Implementation
// ==========================================

bool moveTowards(float& currX, float& currY, float targetX, float targetY, float speed) {
    float dx = targetX - currX;
    float dy = targetY - currY;
    float dist = std::sqrt(dx*dx + dy*dy);
    
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
    
    // Commuter starts at right side (x=1200), drives left
    // First, drive to F11 intersection and cross it
    // Then continue to F10 where parking decision is made
    
    float f11StopLine = 960.0f; // Stop before F11
    float f10StopLine = 360.0f; // Stop before F10 (coming from right)
    
    // Phase 1: Drive to F11 stop line
    while (!moveTowards(v->x, v->y, f11StopLine, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }
    
    // Phase 2: Wait for F11 Green (simplified - we just wait a bit since we don't have F11's state)
    // In real scenario, we'd need IPC to get F11's light state
    // For now, just pause briefly to simulate waiting
    usleep(500000); // 0.5 second pause
    
    // Phase 3: Cross F11 and drive to F10
    while (!moveTowards(v->x, v->y, f10StopLine, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }
    
    // Phase 4: Now at F10 - wait for F10's green light
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);
        
        if (state == TrafficLightState::GREEN || v->type == VehicleType::AMBULANCE || v->type == VehicleType::FIRETRUCK) {
            break;
        }
        v->sendUpdate();
        usleep(100000); // Wait 100ms
    }
    
    // Phase 5: Try to park (only Cars and Bikes)
    bool willPark = (v->parkingLot != nullptr) && (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);
    
    if (willPark) {
        // Move to Queue Entrance
        float queueX = 300.0f;
        float queueY = 320.0f;
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        // Try to enter queue
        int queueIdx = v->parkingLot->enterQueue();
        
        if (queueIdx != -1) {
            // Successfully entered queue
            v->isInQueue = true;
            v->queueIndex = queueIdx;
            
            float queueBoxX = 425.0f + queueIdx * 40.0f;
            float queueBoxY = 325.0f;
            
            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate();
            
            // Wait for a parking spot
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
            sleep(12); // Stay parked for 12 seconds
            
            v->parkingLot->leave(spotIndex);
            
            // Return to road and exit left
            while (!moveTowards(v->x, v->y, 300.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
        // If queue full, just continue through
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
    
    // Define path points
    // F10 Intersection Center: (300, 400)
    // F11 Intersection Center: (900, 400)
    // Parking Entrance: (300, 350) -> Parking Spot (300, 200)
    
    float targetX = v->endX;
    float targetY = v->endY;
    
    // Use the stop line provided in args
    float stopLineX = args->stopLineX; 
    
    // Phase 1: Move to Stop Line
    while (!moveTowards(v->x, v->y, stopLineX, v->y, v->speed)) {
        v->sendUpdate();
        usleep(VEHICLE_SPEED_MS * 1000);
    }
    
    // Phase 2: Check Light
    // We need to check the light state. 
    // For simplicity, we'll just poll the mutex/state provided in args
    while (true) {
        pthread_mutex_lock(args->lightMutex);
        TrafficLightState state = *(args->lightState);
        pthread_mutex_unlock(args->lightMutex);
        
        if (state == TrafficLightState::GREEN || v->type == VehicleType::AMBULANCE || v->type == VehicleType::FIRETRUCK) {
            break;
        }
        usleep(100000); // Wait 100ms
    }
    
    // Phase 3: Cross Intersection or Park
    bool willPark = (v->parkingLot != nullptr) && (v->type == VehicleType::CAR || v->type == VehicleType::BIKE);
    
    // Random chance to park
    if (willPark && (rand() % 2 == 0)) {
        // Move to Queue Entrance
        float queueX = 300.0f;
        float queueY = 320.0f; // Just above road
        while (!moveTowards(v->x, v->y, queueX, queueY, v->speed)) {
            v->sendUpdate();
            usleep(VEHICLE_SPEED_MS * 1000);
        }

        // Try to enter queue first
        int queueIdx = v->parkingLot->enterQueue();
        
        if (queueIdx != -1) {
            // Successfully entered queue - move to queue box position
            v->isInQueue = true;
            v->queueIndex = queueIdx;
            
            // Queue boxes are drawn at y=310, x starting at 420
            // Each box is 35 wide with 5 spacing
            float queueBoxX = 425.0f + queueIdx * 40.0f;
            float queueBoxY = 325.0f;
            
            while (!moveTowards(v->x, v->y, queueBoxX, queueBoxY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            v->sendUpdate(); // Show in queue position
            
            // Now wait for a parking spot (this blocks until spot available)
            int spotIndex = v->parkingLot->waitForSpot(queueIdx);
            
            // Got a spot! Leave queue
            v->isInQueue = false;
            v->queueIndex = -1;
            
            // Calculate spot position
            // Lot is at (200, 150), size (200, 150)
            // 2 rows of 5 spots
            int row = spotIndex / 5;
            int col = spotIndex % 5;
            
            float parkX = 230.0f + col * 40.0f;
            float parkY = 185.0f + row * 60.0f;
            
            while (!moveTowards(v->x, v->y, parkX, parkY, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
            
            // Parked
            v->sendUpdate(true); // Parked state
            sleep(12); // Stay parked for 12 seconds
            
            // Leave
            v->parkingLot->leave(spotIndex);
            
            // Return to road
            while (!moveTowards(v->x, v->y, 300.0f, 400.0f, v->speed)) {
                v->sendUpdate();
                usleep(VEHICLE_SPEED_MS * 1000);
            }
        }
        // If queue was full (queueIdx == -1), vehicle just continues without parking
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

void trafficControllerF10(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd) {
    // Setup
    ParkingLot parkingLot;
    TrafficLightState lightState = TrafficLightState::RED;
    pthread_mutex_t lightMutex = PTHREAD_MUTEX_INITIALIZER;
    
    std::vector<pthread_t> threads;
    std::vector<Vehicle*> vehicles;
    
    // Set command pipe non-blocking
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
                    std::cout << "[F10] Scenario A: Green Wave - Spawning Ambulance" << std::endl;
                    // Spawn ambulance
                    spawnLocalVehicle(VehicleType::AMBULANCE);
                    
                    // Signal F11 to prepare for emergency
                    CoordinationMessage coordMsg;
                    coordMsg.type = CoordinationMessage::EMERGENCY_APPROACHING;
                    coordMsg.sourceIntersection = 10;
                    write(writeCoordFd, &coordMsg, sizeof(coordMsg));
                    break;
                }
                case ScenarioCommand::PARKING_FULL: {
                    std::cout << "[F10] Scenario B: Parking Saturation - Spawning 16 Cars" << std::endl;
                    // Spawn 16 cars rapidly (10 spots + 5 queue + 1 rejected)
                    for (int i = 0; i < 16; ++i) {
                        spawnLocalVehicle(VehicleType::CAR);
                        usleep(200000); // 200ms between spawns
                    }
                    break;
                }
                case ScenarioCommand::GRIDLOCK: {
                    std::cout << "[F10] Scenario C: Gridlock - Spawning from all directions" << std::endl;
                    // Spawn 5 from left, 5 commuters from right
                    for (int i = 0; i < 5; ++i) {
                        VehicleType type = (VehicleType)(rand() % 4 + 2); // CAR, BIKE, BUS, TRACTOR
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
        
        // Red
        pthread_mutex_lock(&lightMutex);
        lightState = TrafficLightState::RED;
        pthread_mutex_unlock(&lightMutex);
        
        // Send Light Update
        PipeMessage msg;
        msg.magic = MSG_MAGIC;
        msg.type = PipeMessage::LIGHT_UPDATE;
        msg.data.light.intersectionId = 10;
        msg.data.light.state = TrafficLightState::RED;
        write(writePipeFd, &msg, sizeof(msg));
        
        // Split sleep to check commands more frequently
        for (int i = 0; i < 6; ++i) {
            usleep(500000); // 0.5s x 6 = 3s total
            // Check for commands during red phase too
            if (read(cmdPipeFd, &cmdMsg, sizeof(cmdMsg)) == sizeof(cmdMsg) && cmdMsg.magic == CMD_MAGIC) {
                // Handle command same as above (simplified - just spawn ambulance for green wave)
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
        
        // Green
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
        pMsg.data.waitingCount = parkingLot.getWaitingCount();
        write(writePipeFd, &pMsg, sizeof(pMsg));
    }
    
    for (auto tid : threads) {
        pthread_join(tid, nullptr);
    }
}

void trafficControllerF11(int writePipeFd, int readCoordFd, int writeCoordFd, int cmdPipeFd) {
    // F11 manages vehicles and responds to emergency signals from F10
    
    TrafficLightState lightState = TrafficLightState::RED;
    pthread_mutex_t lightMutex = PTHREAD_MUTEX_INITIALIZER;
    
    std::vector<pthread_t> threads;
    std::vector<Vehicle*> vehicles;
    
    // Set pipes non-blocking
    setNonBlocking(cmdPipeFd);
    setNonBlocking(readCoordFd);
    
    int vehicleIdCounter = 100;
    bool emergencyMode = false;
    
    // Helper to spawn vehicle
    auto spawnVehicle = [&](VehicleType type, float yPos = 450.0f) {
        Vehicle* v = new Vehicle(vehicleIdCounter++, type, writePipeFd, nullptr);
        v->x = 1200; 
        v->y = yPos;
        v->endX = 0;
        v->endY = yPos;
        
        vehicles.push_back(v);
        
        ThreadArgs* args = new ThreadArgs();
        args->vehicle = v;
        args->lightMutex = &lightMutex;
        args->lightState = &lightState;
        args->stopLineX = 960.0f;
        args->isCommuter = false;
        
        pthread_t tid;
        pthread_create(&tid, nullptr, vehicleThreadFunc, args);
        threads.push_back(tid);
    };
    
    // Spawn initial vehicles - 4 vehicles
    for (int i = 0; i < 4; ++i) {
        VehicleType type = (VehicleType)(rand() % 6);
        spawnVehicle(type);
        usleep(rand() % 1500000 + 500000);
    }
    
    while (true) {
        // Check for emergency signal from F10
        CoordinationMessage coordMsg;
        if (read(readCoordFd, &coordMsg, sizeof(coordMsg)) == sizeof(coordMsg)) {
            if (coordMsg.type == CoordinationMessage::EMERGENCY_APPROACHING) {
                std::cout << "[F11] Emergency signal received! Switching to GREEN" << std::endl;
                emergencyMode = true;
                
                // Immediately switch to green for emergency vehicle
                pthread_mutex_lock(&lightMutex);
                lightState = TrafficLightState::GREEN;
                pthread_mutex_unlock(&lightMutex);
                
                PipeMessage msg;
                msg.magic = MSG_MAGIC;
                msg.type = PipeMessage::LIGHT_UPDATE;
                msg.data.light.intersectionId = 11;
                msg.data.light.state = TrafficLightState::GREEN;
                write(writePipeFd, &msg, sizeof(msg));
                
                // Stay green for 5 seconds for emergency
                sleep(5);
                emergencyMode = false;
            }
        }
        
        // Check for commands from parent
        CommandMessage cmdMsg;
        if (read(cmdPipeFd, &cmdMsg, sizeof(cmdMsg)) == sizeof(cmdMsg) && cmdMsg.magic == CMD_MAGIC) {
            if (cmdMsg.command == ScenarioCommand::GRIDLOCK) {
                std::cout << "[F11] Scenario C: Gridlock - Spawning vehicles" << std::endl;
                // Spawn vehicles from F11 side
                for (int i = 0; i < 5; ++i) {
                    VehicleType type = (VehicleType)(rand() % 4 + 2);
                    spawnVehicle(type, 450.0f);
                    usleep(100000);
                }
                // Also spawn some going opposite direction (different lane)
                for (int i = 0; i < 3; ++i) {
                    VehicleType type = (VehicleType)(rand() % 4 + 2);
                    spawnVehicle(type, 430.0f);
                    usleep(100000);
                }
            }
        }
        
        if (!emergencyMode) {
            // Red
            pthread_mutex_lock(&lightMutex);
            lightState = TrafficLightState::RED;
            pthread_mutex_unlock(&lightMutex);
            
            PipeMessage msg;
            msg.magic = MSG_MAGIC;
            msg.type = PipeMessage::LIGHT_UPDATE;
            msg.data.light.intersectionId = 11;
            msg.data.light.state = TrafficLightState::RED;
            write(writePipeFd, &msg, sizeof(msg));
            
            // Check coordination during red phase
            for (int i = 0; i < 6; ++i) {
                usleep(500000);
                if (read(readCoordFd, &coordMsg, sizeof(coordMsg)) == sizeof(coordMsg)) {
                    if (coordMsg.type == CoordinationMessage::EMERGENCY_APPROACHING) {
                        std::cout << "[F11] Emergency during RED! Switching to GREEN" << std::endl;
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
            
            // Green
            pthread_mutex_lock(&lightMutex);
            lightState = TrafficLightState::GREEN;
            pthread_mutex_unlock(&lightMutex);
            
            msg.data.light.state = TrafficLightState::GREEN;
            write(writePipeFd, &msg, sizeof(msg));
            
            for (int i = 0; i < 6; ++i) {
                usleep(500000);
            }
        }
    }
    
    for (auto tid : threads) {
        pthread_join(tid, nullptr);
    }
}

void visualizerProcess(int pipeF10, int pipeF11, int cmdPipeF10, int cmdPipeF11) {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), WINDOW_TITLE);
    window.setFramerateLimit(60);

    setNonBlocking(pipeF10);
    setNonBlocking(pipeF11);

    std::map<int, VehicleState> vehicles;
    TrafficLightState lightF10 = TrafficLightState::RED;
    TrafficLightState lightF11 = TrafficLightState::RED;
    int parkingQueueCount = 0;

    // Notification system
    std::string notificationTitle = "";
    std::string notificationDesc = "";
    sf::Clock notificationClock;
    float notificationDuration = 8.0f; // Show for 8 seconds
    bool showNotification = false;

    // Font for text
    sf::Font font;
    bool fontLoaded = font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    
    // UI Button definitions
    struct Button {
        sf::RectangleShape shape;
        std::string label;
        ScenarioCommand command;
        bool sendToF10;
        bool sendToF11;
    };
    
    std::vector<Button> buttons;
    
    // Button 1: Green Wave (F10 only)
    Button btn1;
    btn1.shape.setSize(sf::Vector2f(160, 50));
    btn1.shape.setPosition(150, 520);
    btn1.shape.setFillColor(sf::Color(0, 150, 0));
    btn1.shape.setOutlineColor(sf::Color::White);
    btn1.shape.setOutlineThickness(3);
    btn1.label = "1. Green Wave";
    btn1.command = ScenarioCommand::GREEN_WAVE;
    btn1.sendToF10 = true;
    btn1.sendToF11 = false;
    buttons.push_back(btn1);
    
    // Button 2: Full Parking (F10 only)
    Button btn2;
    btn2.shape.setSize(sf::Vector2f(160, 50));
    btn2.shape.setPosition(350, 520);
    btn2.shape.setFillColor(sf::Color(180, 180, 0));
    btn2.shape.setOutlineColor(sf::Color::White);
    btn2.shape.setOutlineThickness(3);
    btn2.label = "2. Full Parking";
    btn2.command = ScenarioCommand::PARKING_FULL;
    btn2.sendToF10 = true;
    btn2.sendToF11 = false;
    buttons.push_back(btn2);
    
    // Button 3: Chaos Mode (Both F10 and F11)
    Button btn3;
    btn3.shape.setSize(sf::Vector2f(160, 50));
    btn3.shape.setPosition(550, 520);
    btn3.shape.setFillColor(sf::Color(180, 0, 0));
    btn3.shape.setOutlineColor(sf::Color::White);
    btn3.shape.setOutlineThickness(3);
    btn3.label = "3. Chaos Mode";
    btn3.command = ScenarioCommand::GRIDLOCK;
    btn3.sendToF10 = true;
    btn3.sendToF11 = true;
    buttons.push_back(btn3);

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            
            // Handle button clicks
            if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
                sf::Vector2f mousePos(event.mouseButton.x, event.mouseButton.y);
                
                for (auto& btn : buttons) {
                    if (btn.shape.getGlobalBounds().contains(mousePos)) {
                        std::cout << "[UI] Button clicked: " << btn.label << std::endl;
                        
                        CommandMessage cmdMsg;
                        cmdMsg.magic = CMD_MAGIC;
                        cmdMsg.command = btn.command;
                        
                        if (btn.sendToF10) {
                            write(cmdPipeF10, &cmdMsg, sizeof(cmdMsg));
                        }
                        if (btn.sendToF11) {
                            write(cmdPipeF11, &cmdMsg, sizeof(cmdMsg));
                        }
                        
                        // Set notification based on scenario
                        showNotification = true;
                        notificationClock.restart();
                        
                        if (btn.command == ScenarioCommand::GREEN_WAVE) {
                            notificationTitle = "Scenario A: The Green Wave";
                            notificationDesc = "Spawning Ambulance at F10 destined for F11.\nF10 signals F11 via Pipe. F11 preempts light to GREEN.";
                        } else if (btn.command == ScenarioCommand::PARKING_FULL) {
                            notificationTitle = "Scenario B: Parking Saturation";
                            notificationDesc = "Rapidly spawning 16 Cars at F10 for parking.\nFilling 10 Spots + 5 Queue. 16th car rejected.";
                        } else if (btn.command == ScenarioCommand::GRIDLOCK) {
                            notificationTitle = "Scenario C: Intersection Gridlock";
                            notificationDesc = "Spawning cars from all directions at F10 & F11.\nMutex locks prevent collisions.";
                        }
                        
                        // Visual feedback - flash button
                        btn.shape.setFillColor(sf::Color::White);
                    }
                }
            }
        }
        
        // Reset button colors
        buttons[0].shape.setFillColor(sf::Color(0, 150, 0));
        buttons[1].shape.setFillColor(sf::Color(180, 180, 0));
        buttons[2].shape.setFillColor(sf::Color(180, 0, 0));

        // Read from pipes
        PipeMessage msg;
        int bytesRead;
        
        // Read F10
        while ((bytesRead = read(pipeF10, &msg, sizeof(msg))) > 0) {
            if (bytesRead == sizeof(msg) && msg.magic == MSG_MAGIC) {
                if (msg.type == PipeMessage::VEHICLE_UPDATE) {
                    vehicles[msg.data.vehicle.id] = msg.data.vehicle;
                } else if (msg.type == PipeMessage::LIGHT_UPDATE) {
                    lightF10 = msg.data.light.state;
                } else if (msg.type == PipeMessage::PARKING_UPDATE) {
                    parkingQueueCount = msg.data.waitingCount;
                }
            }
        }

        // Read F11
        while ((bytesRead = read(pipeF11, &msg, sizeof(msg))) > 0) {
            if (bytesRead == sizeof(msg) && msg.magic == MSG_MAGIC) {
                if (msg.type == PipeMessage::VEHICLE_UPDATE) {
                    vehicles[msg.data.vehicle.id] = msg.data.vehicle;
                } else if (msg.type == PipeMessage::LIGHT_UPDATE) {
                    lightF11 = msg.data.light.state;
                }
            }
        }

        window.clear(sf::Color(50, 50, 50)); // Dark Grey Road

        // Draw Roads
        sf::RectangleShape road(sf::Vector2f(WINDOW_WIDTH, 100));
        road.setPosition(0, 350);
        road.setFillColor(sf::Color(30, 30, 30));
        window.draw(road);

        // Draw Intersections
        // F10
        sf::RectangleShape intersectionF10(sf::Vector2f(100, 100));
        intersectionF10.setPosition(250, 350);
        intersectionF10.setFillColor(sf::Color(20, 20, 20));
        window.draw(intersectionF10);

        // F11
        sf::RectangleShape intersectionF11(sf::Vector2f(100, 100));
        intersectionF11.setPosition(850, 350);
        intersectionF11.setFillColor(sf::Color(20, 20, 20));
        window.draw(intersectionF11);

        // Draw Parking Lot (Near F10)
        sf::RectangleShape parkingLot(sf::Vector2f(200, 150));
        parkingLot.setPosition(200, 150);
        parkingLot.setFillColor(sf::Color(40, 40, 40));
        parkingLot.setOutlineColor(sf::Color::White);
        parkingLot.setOutlineThickness(2);
        window.draw(parkingLot);

        // Draw Parking Spots
        for(int i=0; i<10; i++) {
            int row = i / 5;
            int col = i % 5;
            sf::RectangleShape spot(sf::Vector2f(30, 50));
            spot.setPosition(215 + col * 40, 160 + row * 60);
            spot.setFillColor(sf::Color(60, 60, 60));
            spot.setOutlineColor(sf::Color::White);
            spot.setOutlineThickness(1);
            window.draw(spot);
        }

        // Draw Waiting Queue - 5 individual boxes
        for(int i=0; i<PARKING_QUEUE_SIZE; i++) {
            sf::RectangleShape queueSlot(sf::Vector2f(35, 25));
            queueSlot.setPosition(410 + i * 40, 312);
            queueSlot.setFillColor(sf::Color(80, 40, 40));
            queueSlot.setOutlineColor(sf::Color::White);
            queueSlot.setOutlineThickness(1);
            window.draw(queueSlot);
        }

        // Draw Queue Label
        if (fontLoaded) {
            sf::Text queueLabel("Queue (" + std::to_string(parkingQueueCount) + "/5):", font, 14);
            queueLabel.setPosition(320, 315);
            queueLabel.setFillColor(sf::Color::White);
            window.draw(queueLabel);
        }

        // Draw Traffic Lights
        sf::CircleShape lightShape(15);
        
        // F10 Light
        lightShape.setPosition(260, 320);
        lightShape.setFillColor(lightF10 == TrafficLightState::GREEN ? sf::Color::Green : sf::Color::Red);
        window.draw(lightShape);

        // F11 Light
        lightShape.setPosition(860, 320);
        lightShape.setFillColor(lightF11 == TrafficLightState::GREEN ? sf::Color::Green : sf::Color::Red);
        window.draw(lightShape);

        // Draw Vehicles
        for (auto& pair : vehicles) {
            VehicleState& v = pair.second;
            if (!v.isActive) continue;

            sf::RectangleShape vehicleShape(sf::Vector2f(40, 20));
            vehicleShape.setFillColor(sf::Color(v.colorR, v.colorG, v.colorB));
            vehicleShape.setOrigin(20, 10); // Center
            
            // If vehicle is in queue, position it in the queue box
            if (v.isInQueue && v.queueIndex >= 0 && v.queueIndex < PARKING_QUEUE_SIZE) {
                vehicleShape.setSize(sf::Vector2f(30, 18));
                vehicleShape.setOrigin(15, 9);
                vehicleShape.setPosition(427.0f + v.queueIndex * 40.0f, 325.0f);
                vehicleShape.setRotation(0);
            } else {
                vehicleShape.setPosition(v.x, v.y);
                // Simple rotation based on direction
                if (v.isParked) {
                    vehicleShape.setRotation(90);
                } else {
                    vehicleShape.setRotation(0);
                }
            }

            window.draw(vehicleShape);
            
            // Draw flashing effect for ambulance
            if (v.type == VehicleType::AMBULANCE) {
                sf::RectangleShape cross1(sf::Vector2f(20, 6));
                sf::RectangleShape cross2(sf::Vector2f(6, 20));
                cross1.setFillColor(sf::Color::Red);
                cross2.setFillColor(sf::Color::Red);
                cross1.setOrigin(10, 3);
                cross2.setOrigin(3, 10);
                cross1.setPosition(v.x, v.y);
                cross2.setPosition(v.x, v.y);
                window.draw(cross1);
                window.draw(cross2);
            }
        }

        // Draw Legend
        if (fontLoaded) {
            struct LegendItem {
                std::string label;
                sf::Color color;
            };
            std::vector<LegendItem> legend = {
                {"Ambulance", sf::Color::White},
                {"Firetruck", sf::Color::Red},
                {"Bus", sf::Color::Blue},
                {"Car", sf::Color::Green},
                {"Bike", sf::Color::Yellow},
                {"Tractor", sf::Color(100, 100, 100)}
            };

            float legendY = 10.0f;
            
            // Draw background for legend
            sf::RectangleShape legendBg(sf::Vector2f(150, 140));
            legendBg.setPosition(5, 5);
            legendBg.setFillColor(sf::Color(0, 0, 0, 150)); // Semi-transparent black
            window.draw(legendBg);

            for (const auto& item : legend) {
                sf::RectangleShape box(sf::Vector2f(20, 10));
                box.setPosition(15, legendY + 5);
                box.setFillColor(item.color);
                window.draw(box);

                sf::Text text(item.label, font, 14);
                text.setPosition(45, legendY);
                text.setFillColor(sf::Color::White);
                window.draw(text);

                legendY += 20.0f;
            }
        }

        // Draw Notification Box (top right)
        if (showNotification && notificationClock.getElapsedTime().asSeconds() < notificationDuration) {
            float alpha = 255.0f;
            // Fade out in last 2 seconds
            float elapsed = notificationClock.getElapsedTime().asSeconds();
            if (elapsed > notificationDuration - 2.0f) {
                alpha = 255.0f * (notificationDuration - elapsed) / 2.0f;
            }
            
            sf::RectangleShape notifBg(sf::Vector2f(380, 90));
            notifBg.setPosition(WINDOW_WIDTH - 400, 10);
            notifBg.setFillColor(sf::Color(0, 50, 100, (int)alpha));
            notifBg.setOutlineColor(sf::Color(100, 200, 255, (int)alpha));
            notifBg.setOutlineThickness(3);
            window.draw(notifBg);
            
            if (fontLoaded) {
                // Title
                sf::Text titleText(notificationTitle, font, 18);
                titleText.setPosition(WINDOW_WIDTH - 390, 15);
                titleText.setFillColor(sf::Color(100, 255, 100, (int)alpha));
                titleText.setStyle(sf::Text::Bold);
                window.draw(titleText);
                
                // Description (split by newline)
                size_t newlinePos = notificationDesc.find('\n');
                std::string line1 = notificationDesc.substr(0, newlinePos);
                std::string line2 = (newlinePos != std::string::npos) ? notificationDesc.substr(newlinePos + 1) : "";
                
                sf::Text descText1(line1, font, 14);
                descText1.setPosition(WINDOW_WIDTH - 390, 42);
                descText1.setFillColor(sf::Color(255, 255, 255, (int)alpha));
                window.draw(descText1);
                
                sf::Text descText2(line2, font, 14);
                descText2.setPosition(WINDOW_WIDTH - 390, 62);
                descText2.setFillColor(sf::Color(255, 255, 255, (int)alpha));
                window.draw(descText2);
            }
        } else if (notificationClock.getElapsedTime().asSeconds() >= notificationDuration) {
            showNotification = false;
        }

        // Draw Control Panel Background
        sf::RectangleShape panelBg(sf::Vector2f(WINDOW_WIDTH, 100));
        panelBg.setPosition(0, 500);
        panelBg.setFillColor(sf::Color(20, 20, 50));
        panelBg.setOutlineColor(sf::Color::White);
        panelBg.setOutlineThickness(2);
        window.draw(panelBg);
        
        // Draw Buttons
        for (auto& btn : buttons) {
            window.draw(btn.shape);
            
            if (fontLoaded) {
                sf::Text btnText(btn.label, font, 16);
                btnText.setPosition(btn.shape.getPosition().x + 15, btn.shape.getPosition().y + 15);
                btnText.setFillColor(sf::Color::White);
                window.draw(btnText);
            }
        }
        
        // Draw Panel Title
        if (fontLoaded) {
            sf::Text panelTitle("SCENARIOS:", font, 18);
            panelTitle.setPosition(30, 530);
            panelTitle.setFillColor(sf::Color::White);
            panelTitle.setStyle(sf::Text::Bold);
            window.draw(panelTitle);
        }

        window.display();
    }
}

int main() {
    // Create Pipes
    int pipeF10ToVis[2];      // Pipe 1: F10 -> Parent (Data)
    int pipeF11ToVis[2];      // Pipe 2: F11 -> Parent (Data)
    int pipeCoordF10ToF11[2]; // Pipe 3: F10 -> F11 (Coordination/Emergency)
    int pipeCmdToF10[2];      // Pipe 4: Parent -> F10 (Commands)
    int pipeCmdToF11[2];      // Pipe 5: Parent -> F11 (Commands)

    if (pipe(pipeF10ToVis) == -1 || pipe(pipeF11ToVis) == -1 || 
        pipe(pipeCoordF10ToF11) == -1 || pipe(pipeCmdToF10) == -1 ||
        pipe(pipeCmdToF11) == -1) {
        perror("Pipe creation failed");
        return 1;
    }

    std::cout << "=== Traffic Simulation Started ===" << std::endl;
    std::cout << "Click scenario buttons to trigger events" << std::endl;

    pid_t pidF10 = fork();
    if (pidF10 == 0) {
        // Child F10
        close(pipeF10ToVis[0]); // Close Read end
        close(pipeF11ToVis[0]); close(pipeF11ToVis[1]); // Close F11's pipe
        close(pipeCoordF10ToF11[0]); // Close Read end of coord pipe
        close(pipeCmdToF10[1]); // Close Write end of command pipe (parent writes)
        close(pipeCmdToF11[0]); close(pipeCmdToF11[1]); // Close F11's command pipe

        trafficControllerF10(pipeF10ToVis[1], -1, pipeCoordF10ToF11[1], pipeCmdToF10[0]);
        return 0;
    }

    pid_t pidF11 = fork();
    if (pidF11 == 0) {
        // Child F11
        close(pipeF11ToVis[0]); // Close Read end
        close(pipeF10ToVis[0]); close(pipeF10ToVis[1]); // Close F10's pipe
        close(pipeCoordF10ToF11[1]); // Close Write end of coord pipe (F10 writes)
        close(pipeCmdToF11[1]); // Close Write end of command pipe
        close(pipeCmdToF10[0]); close(pipeCmdToF10[1]); // Close F10's command pipe

        trafficControllerF11(pipeF11ToVis[1], pipeCoordF10ToF11[0], -1, pipeCmdToF11[0]);
        return 0;
    }

    // Parent (Visualizer)
    close(pipeF10ToVis[1]); // Close Write end
    close(pipeF11ToVis[1]); // Close Write end
    close(pipeCoordF10ToF11[0]); close(pipeCoordF10ToF11[1]); // Close coord pipe
    close(pipeCmdToF10[0]); // Close Read end of command pipes
    close(pipeCmdToF11[0]);

    visualizerProcess(pipeF10ToVis[0], pipeF11ToVis[0], pipeCmdToF10[1], pipeCmdToF11[1]);

    // Cleanup
    wait(NULL);
    wait(NULL);

    return 0;
}




