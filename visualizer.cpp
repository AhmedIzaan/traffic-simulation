/**
 * visualizer.cpp
 * 
 * SFML-based visualization implementation.
 */

#include "visualizer.h"
#include "simulation_types.h"

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <unistd.h>

using namespace std;

void visualizerProcess(int pipeF10, int pipeF11, int cmdPipeF10, int cmdPipeF11) {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), WINDOW_TITLE);
    window.setFramerateLimit(60);

    setNonBlocking(pipeF10);
    setNonBlocking(pipeF11);

    std::map<int, VehicleState> vehicles;
    TrafficLightState lightF10 = TrafficLightState::RED;
    TrafficLightState lightF11 = TrafficLightState::RED;
    int parkingQueueCountF10 = 0;
    int parkingQueueCountF11 = 0;

    // Notification system
    std::string notificationTitle = "";
    std::string notificationDesc = "";
    sf::Clock notificationClock;
    float notificationDuration = 8.0f;
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

    // Button 1: Green Wave
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

    // Button 2: Full Parking
    Button btn2;
    btn2.shape.setSize(sf::Vector2f(160, 50));
    btn2.shape.setPosition(350, 520);
    btn2.shape.setFillColor(sf::Color(180, 180, 0));
    btn2.shape.setOutlineColor(sf::Color::White);
    btn2.shape.setOutlineThickness(3);
    btn2.label = "2. Full Parking";
    btn2.command = ScenarioCommand::PARKING_FULL;
    btn2.sendToF10 = true;
    btn2.sendToF11 = true;
    buttons.push_back(btn2);

    // Button 3: Chaos Mode
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
                        cout << "[UI] Button clicked: " << btn.label << endl;

                        CommandMessage cmdMsg;
                        cmdMsg.magic = CMD_MAGIC;
                        cmdMsg.command = btn.command;

                        if (btn.sendToF10) {
                            write(cmdPipeF10, &cmdMsg, sizeof(cmdMsg));
                        }
                        if (btn.sendToF11) {
                            write(cmdPipeF11, &cmdMsg, sizeof(cmdMsg));
                        }

                        // Set notification
                        showNotification = true;
                        notificationClock.restart();

                        if (btn.command == ScenarioCommand::GREEN_WAVE) {
                            notificationTitle = "Scenario A: The Green Wave";
                            notificationDesc = "Spawning Ambulance at F10 destined for F11.\nF10 signals F11 via Pipe. F11 preempts light to GREEN.";
                        } else if (btn.command == ScenarioCommand::PARKING_FULL) {
                            notificationTitle = "Scenario B: Parking Saturation";
                            notificationDesc = "Spawning 16 Cars at F10 & F11 for parking.\nFilling both lots: 10 Spots + 5 Queue each.";
                        } else if (btn.command == ScenarioCommand::GRIDLOCK) {
                            notificationTitle = "Scenario C: Intersection Gridlock";
                            notificationDesc = "Spawning cars from all directions at F10 & F11.\nMutex locks prevent collisions.";
                        }

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

        while ((bytesRead = read(pipeF10, &msg, sizeof(msg))) > 0) {
            if (bytesRead == sizeof(msg) && msg.magic == MSG_MAGIC) {
                if (msg.type == PipeMessage::VEHICLE_UPDATE) {
                    vehicles[msg.data.vehicle.id] = msg.data.vehicle;
                } else if (msg.type == PipeMessage::LIGHT_UPDATE) {
                    lightF10 = msg.data.light.state;
                } else if (msg.type == PipeMessage::PARKING_UPDATE) {
                    if (msg.data.parking.intersectionId == 10) {
                        parkingQueueCountF10 = msg.data.parking.waitingCount;
                    }
                }
            }
        }

        while ((bytesRead = read(pipeF11, &msg, sizeof(msg))) > 0) {
            if (bytesRead == sizeof(msg) && msg.magic == MSG_MAGIC) {
                if (msg.type == PipeMessage::VEHICLE_UPDATE) {
                    vehicles[msg.data.vehicle.id] = msg.data.vehicle;
                } else if (msg.type == PipeMessage::LIGHT_UPDATE) {
                    lightF11 = msg.data.light.state;
                } else if (msg.type == PipeMessage::PARKING_UPDATE) {
                    if (msg.data.parking.intersectionId == 11) {
                        parkingQueueCountF11 = msg.data.parking.waitingCount;
                    }
                }
            }
        }

        window.clear(sf::Color(50, 50, 50));

        // Draw Roads
        sf::RectangleShape road(sf::Vector2f(WINDOW_WIDTH, 100));
        road.setPosition(0, 350);
        road.setFillColor(sf::Color(30, 30, 30));
        window.draw(road);

        // Draw Intersections
        sf::RectangleShape intersectionF10(sf::Vector2f(100, 100));
        intersectionF10.setPosition(250, 350);
        intersectionF10.setFillColor(sf::Color(20, 20, 20));
        window.draw(intersectionF10);

        sf::RectangleShape intersectionF11(sf::Vector2f(100, 100));
        intersectionF11.setPosition(850, 350);
        intersectionF11.setFillColor(sf::Color(20, 20, 20));
        window.draw(intersectionF11);

        // Draw Parking Lot (Right - F10)
        sf::RectangleShape parkingLot(sf::Vector2f(200, 150));
        parkingLot.setPosition(200, 150);
        parkingLot.setFillColor(sf::Color(40, 40, 40));
        parkingLot.setOutlineColor(sf::Color::White);
        parkingLot.setOutlineThickness(2);
        window.draw(parkingLot);

        // Draw Parking Spots (Right - F10)
        for (int i = 0; i < 10; i++) {
            int row = i / 5;
            int col = i % 5;
            sf::RectangleShape spot(sf::Vector2f(30, 50));
            spot.setPosition(215 + col * 40, 160 + row * 60);
            spot.setFillColor(sf::Color(60, 60, 60));
            spot.setOutlineColor(sf::Color::White);
            spot.setOutlineThickness(1);
            window.draw(spot);
        }

        // Draw Waiting Queue (Right - F10)
        for (int i = 0; i < PARKING_QUEUE_SIZE; i++) {
            sf::RectangleShape queueSlot(sf::Vector2f(35, 25));
            queueSlot.setPosition(410 + i * 40, 312);
            queueSlot.setFillColor(sf::Color(80, 40, 40));
            queueSlot.setOutlineColor(sf::Color::White);
            queueSlot.setOutlineThickness(1);
            window.draw(queueSlot);
        }

        // Draw Queue Label (Right - F10)
        if (fontLoaded) {
            sf::Text queueLabel("Queue (" + std::to_string(parkingQueueCountF10) + "/5):", font, 14);
            queueLabel.setPosition(320, 315);
            queueLabel.setFillColor(sf::Color::White);
            window.draw(queueLabel);
        }

        // Draw Parking Lot (Left - F11)
        sf::RectangleShape parkingLotLeft(sf::Vector2f(200, 150));
        parkingLotLeft.setPosition(800, 150);
        parkingLotLeft.setFillColor(sf::Color(40, 40, 40));
        parkingLotLeft.setOutlineColor(sf::Color::White);
        parkingLotLeft.setOutlineThickness(2);
        window.draw(parkingLotLeft);

        // Draw Parking Spots (Left - F11)
        for (int i = 0; i < 10; i++) {
            int row = i / 5;
            int col = i % 5;
            sf::RectangleShape spotLeft(sf::Vector2f(30, 50));
            spotLeft.setPosition(955 - col * 40, 160 + row * 60);
            spotLeft.setFillColor(sf::Color(60, 60, 60));
            spotLeft.setOutlineColor(sf::Color::White);
            spotLeft.setOutlineThickness(1);
            window.draw(spotLeft);
        }

        // Draw Waiting Queue (Left - F11)
        for (int i = 0; i < PARKING_QUEUE_SIZE; i++) {
            sf::RectangleShape queueSlotLeft(sf::Vector2f(35, 25));
            queueSlotLeft.setPosition(760 - i * 40, 312);
            queueSlotLeft.setFillColor(sf::Color(40, 40, 80));
            queueSlotLeft.setOutlineColor(sf::Color::White);
            queueSlotLeft.setOutlineThickness(1);
            window.draw(queueSlotLeft);
        }

        // Draw Queue Label (Left - F11)
        if (fontLoaded) {
            sf::Text queueLabelLeft(":(" + std::to_string(parkingQueueCountF11) + "/5) Queue", font, 14);
            queueLabelLeft.setPosition(805, 315);
            queueLabelLeft.setFillColor(sf::Color::White);
            window.draw(queueLabelLeft);
        }

        // Draw Traffic Lights
        sf::CircleShape lightShape(15);

        lightShape.setPosition(260, 320);
        lightShape.setFillColor(lightF10 == TrafficLightState::GREEN ? sf::Color::Green : sf::Color::Red);
        window.draw(lightShape);

        lightShape.setPosition(860, 320);
        lightShape.setFillColor(lightF11 == TrafficLightState::GREEN ? sf::Color::Green : sf::Color::Red);
        window.draw(lightShape);

        // Draw Vehicles
        for (auto& pair : vehicles) {
            VehicleState& v = pair.second;
            if (!v.isActive) continue;

            sf::RectangleShape vehicleShape(sf::Vector2f(40, 20));
            vehicleShape.setFillColor(sf::Color(v.colorR, v.colorG, v.colorB));
            vehicleShape.setOrigin(20, 10);

            if (v.isInQueue && v.queueIndex >= 0 && v.queueIndex < PARKING_QUEUE_SIZE) {
                vehicleShape.setSize(sf::Vector2f(30, 18));
                vehicleShape.setOrigin(15, 9);
                if (v.isLeftParking) {
                    // Left parking queue (F11)
                    vehicleShape.setPosition(777.0f - v.queueIndex * 40.0f, 325.0f);
                } else {
                    // Right parking queue (F10)
                    vehicleShape.setPosition(427.0f + v.queueIndex * 40.0f, 325.0f);
                }
                vehicleShape.setRotation(0);
            } else {
                vehicleShape.setPosition(v.x, v.y);
                if (v.isParked) {
                    vehicleShape.setRotation(90);
                } else {
                    vehicleShape.setRotation(0);
                }
            }

            window.draw(vehicleShape);

            // Draw ambulance cross
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

            sf::RectangleShape legendBg(sf::Vector2f(150, 140));
            legendBg.setPosition(5, 5);
            legendBg.setFillColor(sf::Color(0, 0, 0, 150));
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

        // Draw Notification Box
        if (showNotification && notificationClock.getElapsedTime().asSeconds() < notificationDuration) {
            float alpha = 255.0f;
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
                sf::Text titleText(notificationTitle, font, 18);
                titleText.setPosition(WINDOW_WIDTH - 390, 15);
                titleText.setFillColor(sf::Color(100, 255, 100, (int)alpha));
                titleText.setStyle(sf::Text::Bold);
                window.draw(titleText);

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

        // Draw Control Panel
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
