/****************************************************************************************
* File: main.cpp
* Description: This is a Proof of Concept (POC) that demonstrates how 
*              to send and receive payloads using screen pixels. The
*              data is chunked and encoded into 3-byte pixels arranged
*              in rows, creating a full-duplex communication pipe
*              between instances. 
*              
*              TLDR; This method leverages screen pixel manipulation
*              as a tranmissio medium, allowing bidirectional data
*              exchange between two systems.
* 
* Author: Santiago Bugnón
* Created: December 19, 2024
* Updated: N/A
* Version: 1.0
* 
* License: MIT License
*               
****************************************************************************************/

#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <cstring> // for memcpy

#define PREFIX 0xCAFEBABE

struct Packet {
    uint32_t prefix;
    uint32_t size;
    std::vector<unsigned char> payload;

    Packet(const std::vector<unsigned char>& data) : prefix(PREFIX), size(data.size()), payload(data) {}
    Packet() : prefix(0), size(0) {}
};

std::mutex screenMutex;

/*
* encode a packet into screen pixels
*/
void encodePacket(HDC hdc, int startX, int startY, const Packet& packet) {
    std::lock_guard<std::mutex> lock(screenMutex);

    int x = startX;
    int y = startY;

    // serialize the packet
    size_t totalSize = sizeof(packet.prefix) + sizeof(packet.size) + packet.payload.size();
    std::vector<unsigned char> buffer(totalSize);
    memcpy(buffer.data(), &packet.prefix, sizeof(packet.prefix));
    memcpy(buffer.data() + sizeof(packet.prefix), &packet.size, sizeof(packet.size));
    memcpy(buffer.data() + sizeof(packet.prefix) + sizeof(packet.size), packet.payload.data(), packet.payload.size());

    // encode into pixels
    for (size_t i = 0; i < buffer.size(); i += 3) {
        unsigned char r = buffer[i];
        unsigned char g = (i + 1 < buffer.size()) ? buffer[i + 1] : 0;
        unsigned char b = (i + 2 < buffer.size()) ? buffer[i + 2] : 0;

        COLORREF color = RGB(r, g, b);
        SetPixel(hdc, x, y, color);

        if (++x >= GetDeviceCaps(hdc, HORZRES)) { // wrap to the next line if reaching screen width (?) might delete later
            x = 0;
            y++;
        }
    }
}
/*
* decode a packet from screen pixels
*/
Packet decodePacket(HDC hdc, int startX, int startY) {
    std::lock_guard<std::mutex> lock(screenMutex);

    int x = startX;
    int y = startY;

    // read and deserialize the packet
    Packet packet;
    std::vector<unsigned char> buffer;

    // read prefix and size first
    for (int i = 0; i < sizeof(packet.prefix) + sizeof(packet.size); i += 3) {
        COLORREF color = GetPixel(hdc, x, y);
        buffer.push_back(GetRValue(color));
        buffer.push_back(GetGValue(color));
        buffer.push_back(GetBValue(color));

        if (++x >= GetDeviceCaps(hdc, HORZRES)) { // wrap to the next line
            x = 0;
            y++;
        }
    }

    memcpy(&packet.prefix, buffer.data(), sizeof(packet.prefix));
    memcpy(&packet.size, buffer.data() + sizeof(packet.prefix), sizeof(packet.size));

    // validate prefix
    if (packet.prefix != PREFIX) {
        return Packet();
    }

    // read the payload
    buffer.clear();
    for (uint32_t i = 0; i < packet.size; i += 3) {
        COLORREF color = GetPixel(hdc, x, y);
        buffer.push_back(GetRValue(color));
        buffer.push_back(GetGValue(color));
        buffer.push_back(GetBValue(color));

        if (++x >= GetDeviceCaps(hdc, HORZRES)) {
            x = 0;
            y++;
        }
    }

    packet.payload.assign(buffer.begin(), buffer.begin() + packet.size);
    return packet;
}

void senderLoop(HDC hdc, int txStartX, int txStartY, int rxStartX, int rxStartY) {
    while (true) {
        
        std::string message;
        std::cout << "You: ";
        std::getline(std::cin, message);

        // send message
        std::vector<unsigned char> payload(message.begin(), message.end());
        Packet packet(payload);
        encodePacket(hdc, txStartX, txStartY, packet);

        // wait for reply
        while (true) {
            Packet reply = decodePacket(hdc, rxStartX, rxStartY);
            if (reply.prefix == PREFIX) {
                std::string replyMessage(reply.payload.begin(), reply.payload.end());
                std::cout << "Them: " << replyMessage << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void receiverLoop(HDC hdc, int rxStartX, int rxStartY, int txStartX, int txStartY) {
    while (true) {
        
        // wait for message
        Packet received = decodePacket(hdc, rxStartX, rxStartY);
        if (received.prefix == PREFIX) {
            std::string message(received.payload.begin(), received.payload.end());
            std::cout << "Them: " << message << std::endl;

            // got reply
            std::cout << "You: ";
            std::string reply;
            std::getline(std::cin, reply);

            // send reply
            std::vector<unsigned char> payload(reply.begin(), reply.end());
            Packet packet(payload);
            encodePacket(hdc, txStartX, txStartY, packet);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    
    // get the screen device context
    HDC hdc = GetDC(NULL);

    if (!hdc) {
        std::cerr << "Failed to get device context." << std::endl;
        return 1;
    }

    // instance index to change the pixel row, so the instances don't overlap with eachother
    int instance;
    std::cout << "Enter instance (1 or 2): ";
    std::cin >> instance;
    std::cin.ignore();

    if (instance == 1) {
        std::thread sender(senderLoop, hdc, 0, 0, 0, 10);
        sender.join();
    }
    else if (instance == 2) {
        std::thread receiver(receiverLoop, hdc, 0, 0, 0, 10);
        receiver.join();
    }
    else {
        std::cerr << "Invalid instance number." << std::endl;
    }

    // release device context
    ReleaseDC(NULL, hdc);
    return 0;
}
