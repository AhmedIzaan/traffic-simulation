/**
 * simulation_types.h
 * 
 * Shared types, structs, enums, and constants for the Traffic Simulation.
 */

#ifndef SIMULATION_TYPES_H
#define SIMULATION_TYPES_H

#include <cstdint>

// ==========================================
// Constants & Configuration
// ==========================================

const int WINDOW_WIDTH = 1200;
const int WINDOW_HEIGHT = 800;
const char* const WINDOW_TITLE = "Traffic Simulation: F10 & F11";

// Parking Configuration
const int PARKING_CAPACITY = 10;
const int PARKING_QUEUE_SIZE = 5;

// Simulation Constants
const int NUM_VEHICLES_PER_CONTROLLER = 8;
const int VEHICLE_SPEED_MS = 50; // Sleep time in ms for movement
const int PARKING_DURATION_SECONDS = 12;

// Pipe Magic Numbers for validation
const uint32_t MSG_MAGIC = 0xCAFEBABE;
const uint32_t CMD_MAGIC = 0xDEADBEEF;

// ==========================================
// Enums
// ==========================================

enum class VehicleType {
    AMBULANCE,  // High Priority - White
    FIRETRUCK,  // High Priority - Red
    BUS,        // Medium Priority - Blue
    CAR,        // Low Priority, Can Park - Green
    BIKE,       // Low Priority, Can Park - Yellow
    TRACTOR     // Low Priority - Grey
};

enum class TrafficLightState {
    RED,
    GREEN
};

enum class Direction {
    NORTH_SOUTH,
    EAST_WEST,
    WEST_EAST
};

enum class ScenarioCommand {
    NONE = 0,
    GREEN_WAVE = 1,      // Scenario A: Spawn ambulance, signal F11
    PARKING_FULL = 2,    // Scenario B: Spawn 16 cars to fill parking
    GRIDLOCK = 3         // Scenario C: Spawn cars from all directions
};

// ==========================================
// Data Structures for IPC
// ==========================================

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
    bool isLeftParking; // true if using left (F11) parking lot
    VehicleType type;
};

// Structure for Traffic Light updates
struct TrafficLightUpdate {
    int intersectionId; // 10 for F10, 11 for F11
    TrafficLightState state;
};

// Parking update structure
struct ParkingUpdate {
    int intersectionId; // 10 for F10 (right), 11 for F11 (left)
    int waitingCount;
};

// Wrapper for all pipe messages to Visualizer
struct PipeMessage {
    uint32_t magic;
    enum Type { VEHICLE_UPDATE, LIGHT_UPDATE, PARKING_UPDATE } type;
    
    union {
        VehicleState vehicle;
        TrafficLightUpdate light;
        ParkingUpdate parking;
    } data;
};

// Coordination message between F10 <-> F11
struct CoordinationMessage {
    enum Type { EMERGENCY_APPROACHING, CLEAR_INTERSECTION } type;
    int sourceIntersection;
};

// Command message from Parent to Controllers
struct CommandMessage {
    uint32_t magic;
    ScenarioCommand command;
};

// ==========================================
// Utility Functions
// ==========================================

#include <fcntl.h>

inline void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

#endif // SIMULATION_TYPES_H
