# Traffic Simulation Makefile

CXX = g++
CXXFLAGS = -Wall -std=c++17
LDFLAGS = -lsfml-graphics -lsfml-window -lsfml-system -lpthread

# Source files
SRCS = main.cpp parking.cpp vehicle.cpp controller.cpp visualizer.cpp
OBJS = $(SRCS:.cpp=.o)

# Header files
HEADERS = simulation_types.h parking.h vehicle.h controller.h visualizer.h

# Output executable
TARGET = traffic_sim

# Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile source files to object files
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(OBJS) $(TARGET)

# Rebuild everything
rebuild: clean all

# Run the simulation
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean rebuild run
