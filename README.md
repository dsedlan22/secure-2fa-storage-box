# Secure Storage Box with Two-Factor Authentication

## Overview

Secure Storage Box is an IoT-based security system developed during my undergraduate studies as part of the *Network Systems Development* course.

The project demonstrates the integration of embedded systems, cloud services, and web technologies into a unified security solution. It combines hardware components with a cloud-based authentication mechanism to implement a two-factor authentication (2FA) system for protecting a physical storage container.

The system leverages ESP32 microcontroller capabilities together with Microsoft Azure services to provide secure access control, monitoring, and remote management functionality.

---

## Project Goals

- Design and implement a physical access control system using two-factor authentication  
- Integrate embedded hardware components with cloud-based services  
- Implement speech-to-text authentication using Azure Speech Services  
- Develop a web application for monitoring and remote interaction  
- Ensure secure communication between hardware and cloud infrastructure  
- Deliver a complete IoT solution suitable for academic evaluation and portfolio presentation  

---

## System Concept

The secure storage system operates using a two-step authentication process:

1. **NFC Card Verification** – The user authenticates using a registered NFC card.  
2. **Voice Phrase Verification** – A randomly generated three-word phrase must be spoken and verified through Azure Speech-to-Text services.

Only after successful validation of both factors does the system unlock the storage mechanism via a servo motor.

In the case of three consecutive failed authentication attempts, the system automatically sends an email notification to the administrator.

The system also includes access logging and a remote emergency unlock feature available through the web interface.

---

## Key Features

### Two-Factor Authentication (2FA)
- NFC-based identity verification  
- Cloud-generated three-word phrase  
- Speech-to-text validation using Azure  

### Embedded System Integration
- ESP32-WROVER-E microcontroller  
- PN7150 NFC module  
- I2S microphone module  
- OLED display for real-time feedback  
- Servo motor locking mechanism  

### Cloud Integration
- Azure Functions for backend logic  
- Azure Speech Service (REST API) for voice recognition  
- Cloud-based session management  
- Access history logging  
- Automated email alert system  

### Web Application
- Display of active authentication phrase  
- Access history overview  
- Remote emergency unlock capability  
- Administrative monitoring interface  

### Security Logic
- Limited phrase dictionary to improve recognition reliability  
- Session-based authentication control  
- Lockout after multiple failed attempts  
- Administrator alert notification  

---

## Technologies Used

### Embedded Systems
- ESP32 (C++ / Arduino framework or PlatformIO)
- I2S audio communication
- NFC communication protocol

### Cloud & Backend
- Microsoft Azure Functions  
- Azure Speech Services (REST API)  
- Node.js  

### Front-End
- HTML5  
- CSS3  
- JavaScript (ES6+)  

---

## Project Structure

- **ESP/** – Firmware and embedded logic  
- **backend/** – Cloud-based authentication and session management  
- **frontend/** – Web application interface  
- **docs/** – Technical documentation and architecture overview  

---

## Academic Context

This project was developed as part of the *Network Systems Development* course during my undergraduate studies.

It represents a practical implementation of IoT architecture, combining hardware-level programming, cloud infrastructure, and full-stack web development. The project demonstrates the ability to design and implement a secure, multi-layered authentication system integrating physical and digital security components.

---

## Learning Outcomes

Through this project, I gained experience in:

- Designing IoT system architecture  
- Integrating hardware with cloud-based services  
- Implementing REST API communication  
- Using speech recognition services in authentication workflows  
- Managing authentication sessions and access logs  
- Developing secure, multi-component systems  
- Coordinating embedded, backend, and frontend development  

---
