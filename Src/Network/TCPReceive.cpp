/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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

#include "TCPReceive.h"
#include "OSD/Logger.h"
#include "OSD/Thread.h"

#if defined(_DEBUG)
#include <cstdio>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

// Larger than any packet the net boards send (a machine's share of the communication RAM): a length beyond it means
// the stream is out of step
static const int kMaxPacketSize = 0x20000;

TCPReceive::TCPReceive(int port) :
	m_listenSocket(nullptr),
	m_receiveSocket(nullptr),
	m_socketSet(nullptr),
	m_running(false)
{
	SDLNet_Init();

	m_socketSet = SDLNet_AllocSocketSet(1);

	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, nullptr, port);

	if (result == 0) {
		m_listenSocket = SDLNet_TCP_Open(&ip);
		if (m_listenSocket) {
			m_running = true;
			m_listenThread = std::thread(&TCPReceive::ListenFunc, this);
		}
	}

	if (!m_listenSocket) {
		ErrorLog("Network: unable to listen on port %d (is it used by another program?): %s", port, SDLNet_GetError());
	}
}

TCPReceive::~TCPReceive()
{
	m_running = false;

	if (m_listenThread.joinable()) {
		m_listenThread.join();
	}

	if (m_listenSocket) {
		SDLNet_TCP_Close(m_listenSocket);
		m_listenSocket = nullptr;
	}

	CloseReceiveSocket();

	if (m_socketSet) {
		SDLNet_FreeSocketSet(m_socketSet);
		m_socketSet = nullptr;
	}

	SDLNet_Quit();
}

bool TCPReceive::CheckDataAvailable(int timeoutMS)
{
	if (!m_receiveSocket) {
		return false;
	}

	return SDLNet_CheckSockets(m_socketSet, timeoutMS) > 0;
}

// The connection dropped (lost, or out of step): out of the set first, so that it is never checked once freed, and a
// new connection from the previous machine can take its place
void TCPReceive::CloseReceiveSocket()
{
	std::lock_guard<std::mutex> lock(m_socketMutex);

	TCPsocket socket = m_receiveSocket.exchange(nullptr);
	if (socket) {
		SDLNet_TCP_DelSocket(m_socketSet, socket);
		SDLNet_TCP_Close(socket);
	}
}

// Exactly size bytes (a TCP receive can return fewer); false if the connection was lost
bool TCPReceive::ReceiveBytes(void* data, int size)
{
	char* p = static_cast<char*>(data);

	while (size > 0) {

		TCPsocket socket = m_receiveSocket;
		int result = socket ? SDLNet_TCP_Recv(socket, p, size) : 0;
		DPRINTF("Received %i bytes\n", result);
		if (result <= 0) {
			CloseReceiveSocket();
			return false;
		}

		p += result;
		size -= result;
	}

	return true;
}

std::vector<char>& TCPReceive::Receive(int timeoutMS)
{
	m_recBuffer.clear();
	m_timedOut = false;

	if (!m_receiveSocket) {
		DPRINTF("Can't receive because no socket.\n");
		return m_recBuffer;
	}

	if (timeoutMS >= 0 && !CheckDataAvailable(timeoutMS)) {
		m_timedOut = m_receiveSocket != nullptr;		// nothing yet, still connected
		return m_recBuffer;
	}

	int32_t size = 0;
	if (!ReceiveBytes(&size, sizeof(size))) {
		return m_recBuffer;
	}

	if (size < 0 || size > kMaxPacketSize) {
		ErrorLog("Network: packet of %d bytes received, the connection is out of step: dropped.", int(size));
		CloseReceiveSocket();
		return m_recBuffer;
	}

	// reserve our space
	m_recBuffer.resize(size);

	if (size && !ReceiveBytes(m_recBuffer.data(), size)) {
		m_recBuffer.clear();
	}

	return m_recBuffer;
}

void TCPReceive::ListenFunc()
{
	while (m_running) {

		CThread::Sleep(16);
		if (m_receiveSocket) continue;

		auto socket = SDLNet_TCP_Accept(m_listenSocket);

		if (socket) {

			// the new connection in the socket set (a lost one was taken out of it when closed)
			std::lock_guard<std::mutex> lock(m_socketMutex);

			if (m_receiveSocket || SDLNet_TCP_AddSocket(m_socketSet, socket) < 0) {
				SDLNet_TCP_Close(socket);
				continue;
			}

			m_receiveSocket = socket;

			DPRINTF("Accepted connection.\n");
		}

	}
}

bool TCPReceive::Connected()
{
	return (m_receiveSocket != 0);
}

bool TCPReceive::TimedOut()
{
	return m_timedOut;
}

bool TCPReceive::Listening()
{
	return m_listenSocket != nullptr;
}
