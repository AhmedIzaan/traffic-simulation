# 🚦 Traffic Simulation - OS Concepts Demonstration

A real-time traffic simulation that demonstrates core **Operating System concepts** using C++ and SFML graphics library.

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![SFML](https://img.shields.io/badge/SFML-2.5+-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

---

## 📋 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [OS Concepts Implemented](#-os-concepts-implemented)
- [Project Architecture](#-project-architecture)
- [File Structure](#-file-structure)
- [Dependencies](#-dependencies)
- [Installation & Build](#-installation--build)
- [Usage](#-usage)
- [Scenarios](#-scenarios)

---

## 🎯 Overview

This project simulates a traffic system with two intersections (F10 and F11), parking lots, and various vehicle types. It serves as a practical demonstration of OS synchronization primitives, inter-process communication, and concurrent programming.

---

## ✨ Features

- **Real-time visualization** of traffic flow using SFML
- **Two intersections** (F10 and F11) with traffic lights
- **Two parking lots** with queue management
- **Six vehicle types**: Ambulance, Firetruck, Bus, Car, Bike, Tractor
- **Emergency vehicle priority** (ambulances bypass red lights)
- **Interactive UI** with scenario buttons
- **Bi-directional traffic** flow

---

## 🧠 OS Concepts Implemented

### 1. Multi-Processing (`fork()`)

**File:** `main.cpp`

The simulation uses three processes:
- **Parent Process**: Visualizer and UI handler
- **Child Process 1**: F10 intersection controller
- **Child Process 2**: F11 intersection controller

```cpp
pid_t pidF10 = fork();
if (pidF10 == 0) {
    // Child F10 Process
    trafficControllerF10(...);
    return 0;
}

pid_t pidF11 = fork();
if (pidF11 == 0) {
    // Child F11 Process
    trafficControllerF11(...);
    return 0;
}
// Parent continues as Visualizer
```

**Why?** Each intersection runs independently. If one crashes, others continue operating.

---

### 2. Multi-Threading (`pthreads`)

**Files:** `controller.cpp`, `vehicle.cpp`

Each vehicle runs in its own thread, allowing concurrent movement:

```cpp
pthread_t tid;
pthread_create(&tid, nullptr, vehicleThreadFunc, args);
```

**Thread Functions:**
| Function | Purpose |
|----------|---------|
| `vehicleThreadFunc` | F10 local vehicles (left → right) |
| `commuterThreadFunc` | F10 commuter vehicles (right → left) |
| `f11VehicleThreadFunc` | F11 vehicles from right |
| `f11LocalVehicleThreadFunc` | F11 vehicles from left |

---

### 3. Inter-Process Communication (Pipes)

**Files:** `main.cpp`, `controller.cpp`, `visualizer.cpp`

Five unidirectional pipes enable communication between processes:

| Pipe | Direction | Purpose |
|------|-----------|---------|
| `pipeF10ToVis` | F10 → Parent | Vehicle/light data |
| `pipeF11ToVis` | F11 → Parent | Vehicle/light data |
| `pipeCoordF10ToF11` | F10 → F11 | Emergency coordination |
| `pipeCmdToF10` | Parent → F10 | Scenario commands |
| `pipeCmdToF11` | Parent → F11 | Scenario commands |

```cpp
// Creating a pipe
int pipeF10ToVis[2];
pipe(pipeF10ToVis);
// pipeF10ToVis[0] = read end
// pipeF10ToVis[1] = write end

// Writing to pipe
write(pipeFd, &msg, sizeof(msg));

// Reading from pipe
read(pipeFd, &msg, sizeof(msg));
```

---

### 4. Mutex (Mutual Exclusion)

**Files:** `controller.cpp`, `vehicle.cpp`, `parking.cpp`

Mutexes protect shared resources from race conditions:

```cpp
pthread_mutex_t lightMutex = PTHREAD_MUTEX_INITIALIZER;

// Changing traffic light (controller)
pthread_mutex_lock(&lightMutex);
lightState = TrafficLightState::GREEN;
pthread_mutex_unlock(&lightMutex);

// Reading traffic light (vehicle thread)
pthread_mutex_lock(args->lightMutex);
TrafficLightState state = *(args->lightState);
pthread_mutex_unlock(args->lightMutex);
```

**Protected Resources:**
- Traffic light state (read by vehicles, written by controller)
- Parking lot internal counters

---

### 5. Semaphores

**Files:** `parking.cpp`, `parking.h`

Counting semaphores manage parking lot capacity:

```cpp
sem_t spots;  // 10 parking spots
sem_t queue;  // 5 queue slots

sem_init(&spots, 0, PARKING_CAPACITY);   // Initialize to 10
sem_init(&queue, 0, PARKING_QUEUE_SIZE); // Initialize to 5
```

**Operations:**

| Function | Behavior | Use Case |
|----------|----------|----------|
| `sem_wait()` | Blocking - waits until resource available | Waiting for parking spot |
| `sem_trywait()` | Non-blocking - returns immediately | Trying to enter queue |
| `sem_post()` | Releases resource | Leaving parking/queue |

```cpp
// Non-blocking queue entry (prevents deadlock)
if (sem_trywait(&queue) != 0) {
    return -1;  // Queue full, skip parking
}

// Blocking wait for parking spot
sem_wait(&spots);  // Will wait here until spot available

// Release spot when leaving
sem_post(&spots);
```

---

### 6. Non-Blocking I/O

**File:** `simulation_types.h`

Pipes are set to non-blocking mode so processes can check for data without hanging:

```cpp
inline void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

**Why?** The visualizer needs to continuously render frames while checking for updates. Blocking reads would freeze the display.

---

### 7. Deadlock Prevention

The project implements several deadlock prevention techniques:

| Technique | Implementation |
|-----------|----------------|
| **Single lock per resource** | Each controller has one mutex |
| **Non-blocking semaphore** | `sem_trywait()` for queue entry |
| **Resource ordering** | Queue → Spot (always same order) |
| **Short critical sections** | Minimal code between lock/unlock |

---

## 🏗️ Project Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        PARENT PROCESS                               │
│                    (Visualizer + UI)                                │
│                 visualizerProcess()                                 │
└───────────────────────────┬─────────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
   Pipe (read)         Pipe (write)        Pipe (read)
   Vehicle/Light       Commands            Vehicle/Light
   from F10                                from F11
        │                   │                   │
        ▼                   ▼                   ▼
┌───────────────┐    ┌───────────┐    ┌───────────────┐
│ CHILD PROCESS │    │  Emergency│    │ CHILD PROCESS │
│     F10       │◄───┤   Pipe    ├───►│     F11       │
│  Controller   │    │           │    │  Controller   │
└───────┬───────┘    └───────────┘    └───────┬───────┘
        │                                     │
   [Threads]                             [Threads]
   Vehicle 1                             Vehicle 1
   Vehicle 2                             Vehicle 2
   ...                                   ...
```

---

## 📁 File Structure

| File | Description |
|------|-------------|
| `main.cpp` | Entry point, creates processes and pipes |
| `simulation_types.h` | Shared structs, enums, constants |
| `controller.cpp/h` | Traffic controller logic for F10 & F11 |
| `vehicle.cpp/h` | Vehicle class and thread functions |
| `parking.cpp/h` | Parking lot with semaphore synchronization |
| `visualizer.cpp/h` | SFML-based graphical display |
| `Makefile` | Build configuration |

---

## 📦 Dependencies

### Required Libraries

| Library | Purpose | Installation (Ubuntu/Debian) |
|---------|---------|------------------------------|
| **SFML 2.5+** | Graphics, windowing | `sudo apt install libsfml-dev` |
| **pthreads** | Multi-threading | Built into Linux |
| **POSIX** | Pipes, fork, semaphores | Built into Linux |

### Compiler

- **g++** with C++17 support
- Install: `sudo apt install build-essential`

---

## 🔧 Installation & Build

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/traffic-simulation.git
cd traffic-simulation
```

### 2. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential libsfml-dev

# Arch Linux
sudo pacman -S sfml base-devel
```

### 3. Build the Project

```bash
make
```

### 4. Run the Simulation

```bash
./traffic_sim
# or
make run
```

### 5. Clean Build Files

```bash
make clean
```

---

## 🎮 Usage

Once running, you'll see:
- **Two intersections** (F10 on left, F11 on right)
- **Two parking lots** (above each intersection)
- **Traffic lights** (red/green circles)
- **Vehicles** moving along the road
- **Control panel** at the bottom with scenario buttons

### Vehicle Colors

| Color | Vehicle Type |
|-------|--------------|
| ⬜ White | Ambulance |
| 🟥 Red | Firetruck |
| 🟦 Blue | Bus |
| 🟩 Green | Car |
| 🟨 Yellow | Bike |
| ⬛ Grey | Tractor |

---

## 🎬 Scenarios

Click the buttons at the bottom to trigger scenarios:

### 1. 🟢 Green Wave (Scenario A)
- Spawns an **ambulance** at F10
- F10 sends emergency signal to F11 via pipe
- F11 **preemptively switches to GREEN** to let ambulance pass

**OS Concepts:** IPC (pipes), process coordination

### 2. 🟡 Full Parking (Scenario B)
- Spawns **16 cars** at both F10 and F11
- Fills parking lots (10 spots) and queues (5 slots)
- 16th car is **rejected** (queue full)

**OS Concepts:** Semaphores, capacity management, `sem_trywait()`

### 3. 🔴 Chaos Mode (Scenario C)
- Spawns vehicles from **all directions**
- Creates heavy traffic at both intersections
- Mutex prevents race conditions

**OS Concepts:** Mutex, thread synchronization, concurrent access

---

## 📊 Data Structures

### Message Types (IPC)

```cpp
// Controller → Visualizer
struct PipeMessage {
    uint32_t magic;  // 0xCAFEBABE validation
    enum Type { VEHICLE_UPDATE, LIGHT_UPDATE, PARKING_UPDATE } type;
    union {
        VehicleState vehicle;
        TrafficLightUpdate light;
        ParkingUpdate parking;
    } data;
};

// Visualizer → Controller
struct CommandMessage {
    uint32_t magic;  // 0xDEADBEEF validation
    ScenarioCommand command;
};
```

### Enum Classes

```cpp
enum class VehicleType { AMBULANCE, FIRETRUCK, BUS, CAR, BIKE, TRACTOR };
enum class TrafficLightState { RED, GREEN };
enum class ScenarioCommand { NONE, GREEN_WAVE, PARKING_FULL, GRIDLOCK };
```

---

## 📄 License

This project is created as part of a semester project of the operating system course.

---

