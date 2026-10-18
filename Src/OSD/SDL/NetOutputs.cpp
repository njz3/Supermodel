/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "NetOutputs.h"


CNetOutputs::CNetOutputs()
{
}

CNetOutputs::~CNetOutputs()
{
    // Stop the server thread first: it uses the clients and the server socket.
    m_running.store(false);
    if (m_tcpServerThread.joinable()) {
        m_tcpServerThread.join();
    }

    // Unregister all clients and destroy the TCP server socket.
    UnregisterAllClients();
    DestroyTcpServerSocket();

    // Free up resources.
    if (m_tcpSocketSet) {
        SDLNet_FreeSocketSet(m_tcpSocketSet);
    }

    SDLNet_Quit();
}

bool CNetOutputs::Initialize()
{
    // Create the TCP server socket.
    if (!CreateTcpServerSocket()) {
        ErrorLog("Unable to create TCP server socket.");
        return false;
    }

    // Run TCP server thread.
    if (!CreateServerThread()) {
        ErrorLog("Unable to start thread for TCP server.");
        return false;
    }

    return true;
}

void CNetOutputs::Attached()
{
    // Broadcast a startup message.
    if (!SendUdpBroadcastWithId()) {
        ErrorLog("Unable to notify with udp broadcast.");
    }
}

void CNetOutputs::SendOutput(EOutputs output, UINT8 prevValue, UINT8 value)
{
    // Loop through all registered clients and send them new output values (those that fail unregistered after).
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::string line = FormatOutput(output, value);
    std::vector<TCPsocket> failed;
    for (auto& client : m_clients) {
        if (!SendTcpData(client, line)) {
            failed.push_back(client.ClientSocket);
        }
    }
    for (TCPsocket socket : failed) {
        ErrorLog("Failed to send output data to client. Unregistering client.");
        UnregisterClient(socket, false);
    }
}

std::string CNetOutputs::FormatOutput(EOutputs output, UINT8 value) const
{
    std::string strvalue;
    if (output == OutputM3ffb) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", value);
        strvalue = hex;
    } else {
        strvalue = std::to_string(value);
    }
    return std::string(GetOutputName(output)) + m_separatorIdAndValue + strvalue + m_frameEnding;
}

bool CNetOutputs::GetDriveBoardReply(UINT8 &value)
{
    if (!m_replyActive.load()) {
        return false;
    }
    value = m_reply.load();
    return true;
}

void CNetOutputs::ReceiveFromClients()
{
    // What the clients sent, without waiting (the emulation never waits for it)
    if (m_clients.empty() || SDLNet_CheckSockets(m_tcpSocketSet, 0) <= 0) {
        return;
    }
    std::vector<TCPsocket> gone;
    for (auto& client : m_clients) {
        if (!SDLNet_SocketReady(client.ClientSocket)) {
            continue;
        }
        char buf[512];
        int n = SDLNet_TCP_Recv(client.ClientSocket, buf, sizeof(buf));
        if (n <= 0) {
            gone.push_back(client.ClientSocket);
            continue;
        }
        client.Received.append(buf, n);
        size_t start = 0;
        for (size_t i = 0; i < client.Received.size(); i++) {
            if (client.Received[i] == '\r' || client.Received[i] == '\n') {
                if (i > start) {
                    ParseLine(client, client.Received.substr(start, i - start));
                }
                start = i + 1;
            }
        }
        client.Received.erase(0, start);
        if (client.Received.size() > 1024) {
            client.Received.clear();   // no line ending in sight: not a client of ours
        }
    }
    for (TCPsocket socket : gone) {
        InfoLog("Client disconnected.");
        UnregisterClient(socket, false);
    }
}

void CNetOutputs::ParseLine(RegisteredClientTcp& client, const std::string& line)
{
    // "m3ffb_rx = XX": the byte the real drive board replied (BackForceFeeder), in hexadecimal; spaces and case free
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
        return;
    }
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    std::string name = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    for (auto& c : name) {
        c = (char)tolower((unsigned char)c);
    }
    if (name != "m3ffb_rx" || value.empty() || value.size() > 2 || !m_driveBoardReply) {
        return;
    }
    char* end = nullptr;
    long v = strtol(value.c_str(), &end, 16);
    if (*end != '\0' || v < 0 || v > 255) {
        return;
    }
    m_reply.store((UINT8)v);
    if (!m_replyActive.load() || m_replySource != client.ClientSocket) {
        InfoLog("Drive board: the game now reads the replies of the outputs' client (m3ffb_rx).");
    }
    m_replySource = client.ClientSocket;
    m_replyActive.store(true);
}

bool CNetOutputs::CreateServerThread()
{
    m_running.store(true);
    m_tcpServerThread = std::thread([this] {
        while (m_running.load()) {
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                TCPsocket clientSocket = SDLNet_TCP_Accept(m_serverSocket);
                if (clientSocket) {
                    // Handle new client connection (e.g., add to list of clients, send initial state, etc.).
                    IPaddress* clientIP = SDLNet_TCP_GetPeerAddress(clientSocket);
                    if (clientIP) {
                        const char* host = SDLNet_ResolveIP(clientIP);
                        InfoLog("Client connected from %s:%d", host, clientIP->port);
                    } else {
                        ErrorLog("Failed to get client IP address: %s", SDLNet_GetError());
                    }
                    RegisterClient(clientSocket);
                }
                ReceiveFromClients();
            }
            // Sleep to prevent busy waiting (short: a drive board's reply is wanted within a few milliseconds).
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    if (!m_tcpServerThread.joinable()) {
        ErrorLog("Failed to create thread for TCP server.");
        return false;
    }

    return true;

}

bool CNetOutputs::CreateTcpServerSocket()
{
    SDLNet_Init();

	// Resolve the host (NULL means any host) and port for the server to listen on.
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, NULL, m_tcpPort) == -1) {
        ErrorLog("ResolveHost error: %s", SDLNet_GetError());
        return false;
    }

    // Create the TCP socket and bind it to the specified port.
    m_serverSocket = SDLNet_TCP_Open(&ip);
    if (!m_serverSocket) {
        ErrorLog("TCP_Open error: %s", SDLNet_GetError());
        return false;
    }

    // Create the socket set to manage multiple clients.
    // It is +1 to include the server socket itself.
    m_tcpSocketSet = SDLNet_AllocSocketSet(m_maxClients + 1);
    SDLNet_TCP_AddSocket(m_tcpSocketSet, m_serverSocket);

    return true;

}

void CNetOutputs::DestroyTcpServerSocket()
{
    if (m_serverSocket) {
        SDLNet_TCP_Close(m_serverSocket);
        m_serverSocket = nullptr;
    }
}

bool CNetOutputs::SendUdpBroadcastWithId()
{
    // Open a UDP socket on any available port.
    UDPsocket udpSocket = SDLNet_UDP_Open(0);
    if (!udpSocket) {
        ErrorLog("UDP_Open error: %s", SDLNet_GetError());
        return false;
    }

    // Set up the broadcast address.
    IPaddress broadcastAddr;
    if (SDLNet_ResolveHost(&broadcastAddr, "255.255.255.255", m_udpBroadcastPort) == -1) {
        ErrorLog("ResolveHost error: %s", SDLNet_GetError());
        return false;
    }

    // Build the broadcast packet.
    UDPpacket* packet = SDLNet_AllocPacket(512);
    std::string lines = "mame_start" + m_separatorIdAndValue + GetGame().name + m_frameEnding + "tcp" + m_separatorIdAndValue + std::to_string(m_tcpPort) + m_frameEnding;

    memcpy(packet->data, lines.c_str(), lines.length());
    packet->len = (int)lines.length();
    packet->address = broadcastAddr;

    // Send the broadcast packet.
    if (SDLNet_UDP_Send(udpSocket, -1, packet) == 0) {
        ErrorLog("UDP_Send error: %s", SDLNet_GetError());
    } else {
        DebugLog("Broadcast sent!");
    }

    // Clean up.
    SDLNet_FreePacket(packet);
    SDLNet_UDP_Close(udpSocket);
    return true;
}

bool CNetOutputs::RegisterClient(TCPsocket socket)
{
    // Check that given client is not already registered.
    for (auto& client : m_clients) {
        if (client.ClientSocket == socket) {
            // If so, just send it current state of all outputs.
            if (!SendAllDataToClient(client)) {
                ErrorLog("Failed to send all output data to client. Unregistering client.");
                UnregisterClient(socket, false);
            }
            return false;
        }
    }

    // Add the new client socket to the socket set.
    SDLNet_TCP_AddSocket(m_tcpSocketSet, socket);

    // If not, store details about client and send it current state of all outputs.
    RegisteredClientTcp client;
    client.ClientSocket = socket;
    m_clients.push_back(client);

    // Send the data to the new client.
    if (!SendGameIdString(client)) {
        ErrorLog("Failed to send game id string to client. Unregistering client.");
        UnregisterClient(socket, false);
        return false;
    }
    if (!SendAllDataToClient(client)) {
        ErrorLog("Failed to send all output data to client. Unregistering client.");
        UnregisterClient(socket, false);
        return false;
    }

    return true;
}

bool CNetOutputs::SendAllDataToClient(RegisteredClientTcp& client)
{
    // Loop through all known outputs and send their current state to given client.
    for (unsigned i = 0; i < NUM_OUTPUTS; i++) {
        EOutputs output = (EOutputs)i;
        if (HasValue(output)) {
            if (!SendTcpData(client, FormatOutput(output, GetValue(output)))) {
                return false;
            }
        }
    }
    return true;
}

bool CNetOutputs::UnregisterAllClients()
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    while (!m_clients.empty()) {
        UnregisterClient(m_clients.front().ClientSocket);
    }
    return true;
}

bool CNetOutputs::UnregisterClient(TCPsocket socket, bool sendStop)
{
    // The drive board's reply came from this client: back to the emulated board's
    if (socket == m_replySource) {
        if (m_replyActive.exchange(false)) {
            InfoLog("Drive board: the outputs' client is gone, the game reads the emulated board's replies again.");
        }
        m_replySource = nullptr;
    }

    // Find any matching clients and remove them.
    bool found = false;
    std::vector<RegisteredClientTcp>::iterator it = m_clients.begin();
    while (it != m_clients.end()) {
        if (it->ClientSocket == socket) {
            // Client found, send stop message (unless it has gone) and remove it.
            if (sendStop) {
                SendStopString(*it, true);
            }
            it = m_clients.erase(it);
            found = true;
            // Remove from socket set and close the socket.
            SDLNet_TCP_DelSocket(m_tcpSocketSet, socket);
            SDLNet_TCP_Close(socket);
        } else {
            it++;
        }
    }

    // Return error if no matches found.
    return found;
}

bool CNetOutputs::SendGameIdString(RegisteredClientTcp& client)
{
    std::string line = "mame_start" + m_separatorIdAndValue + GetGame().name + m_frameEnding;
    return SendTcpData(client, line);
}

bool CNetOutputs::SendStopString(RegisteredClientTcp& client, bool stopped)
{
    std::string line = "mame_stop" + m_separatorIdAndValue + std::string((stopped ? "1" : "0")) + m_frameEnding;
    return SendTcpData(client, line);
}

bool CNetOutputs::SendPauseString(RegisteredClientTcp& client, bool paused)
{
    std::string line = "pause" + m_separatorIdAndValue + std::string((paused ? "1" : "0")) + m_frameEnding;
    return SendTcpData(client, line);
}

bool CNetOutputs::SendTcpData(RegisteredClientTcp& client, const std::string& data)
{
    auto sentDataLen = SDLNet_TCP_Send(client.ClientSocket, data.c_str(), (int)data.length());
    DebugLog("Sent to client: %s", data.c_str());
    DebugLog("Bytes sent: %d", sentDataLen);
    DebugLog("Expected bytes: %zu", data.length());
    // Check for errors in sending data.
    if (sentDataLen < static_cast<int>(data.length())) {
        ErrorLog("Failed to send TCP data to client: %s (tried to send %zu bytes, sent %d bytes).",
            SDLNet_GetError(),
            data.length(),
            sentDataLen
        );
        return false;
    }
    return true;
}
