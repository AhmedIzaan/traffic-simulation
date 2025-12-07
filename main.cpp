/**
 * Traffic Simulation Project - Main Entry Point
 * 
 * This simulation demonstrates OS concepts:
 * - Multi-threading (pthreads) for Vehicles
 * - Multi-processing (fork) for Traffic Controllers
 * - IPC (Pipes) for communication
 * - Synchronization (Mutexes, Semaphores)
 * - Visualization (SFML)
 * 
 * Architecture:
 * - Parent Process: Visualizer & Director (sends commands via pipes)
 * - Child Process A: F10 Controller (manages F10 intersection + parking)
 * - Child Process B: F11 Controller (manages F11 intersection + parking)
 * 
 * Pipes (5 total):
 * - Pipe 1: F10 -> Parent (vehicle/light data)
 * - Pipe 2: F11 -> Parent (vehicle/light data)
 * - Pipe 3: F10 -> F11 (emergency coordination)
 * - Pipe 4: Parent -> F10 (scenario commands)
 * - Pipe 5: Parent -> F11 (scenario commands)
 */

#include "simulation_types.h"
#include "controller.h"
#include "visualizer.h"

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

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

    cout << "=== Traffic Simulation Started ===" << endl;
    cout << "Click scenario buttons to trigger events" << endl;
    cout << endl;
    cout << "Scenarios:" << endl;
    cout << "  1. Green Wave    - Ambulance with emergency signal" << endl;
    cout << "  2. Full Parking  - 16 cars to saturate parking" << endl;
    cout << "  3. Chaos Mode    - Gridlock from all directions" << endl;
    cout << endl;

    pid_t pidF10 = fork();
    if (pidF10 == 0) {
        // Child F10 Process
        close(pipeF10ToVis[0]);
        close(pipeF11ToVis[0]);
        close(pipeF11ToVis[1]);
        close(pipeCoordF10ToF11[0]);
        close(pipeCmdToF10[1]);
        close(pipeCmdToF11[0]);
        close(pipeCmdToF11[1]);

        trafficControllerF10(pipeF10ToVis[1], -1, pipeCoordF10ToF11[1], pipeCmdToF10[0]);
        return 0;
    }

    pid_t pidF11 = fork();
    if (pidF11 == 0) {
        // Child F11 Process
        close(pipeF11ToVis[0]);
        close(pipeF10ToVis[0]);
        close(pipeF10ToVis[1]);
        close(pipeCoordF10ToF11[1]);
        close(pipeCmdToF11[1]);
        close(pipeCmdToF10[0]);
        close(pipeCmdToF10[1]);

        trafficControllerF11(pipeF11ToVis[1], pipeCoordF10ToF11[0], -1, pipeCmdToF11[0]);
        return 0;
    }

    // Parent Process (Visualizer)
    close(pipeF10ToVis[1]);
    close(pipeF11ToVis[1]);
    close(pipeCoordF10ToF11[0]);
    close(pipeCoordF10ToF11[1]);
    close(pipeCmdToF10[0]);
    close(pipeCmdToF11[0]);

    visualizerProcess(pipeF10ToVis[0], pipeF11ToVis[0], pipeCmdToF10[1], pipeCmdToF11[1]);

    // Cleanup
    wait(NULL);
    wait(NULL);

    cout << "=== Traffic Simulation Ended ===" << endl;

    return 0;
}
