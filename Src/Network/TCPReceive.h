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

#ifndef _TCPRECEIVE_H_
#define _TCPRECEIVE_H_

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include "SDLIncludes.h"

class TCPReceive
{
public:
	TCPReceive(int port);
	~TCPReceive();

	bool CheckDataAvailable(int timeoutMS = 0);		// timeoutMS -1 = wait forever until data arrives, 0 = no waiting, 1+ wait time in milliseconds
	// A packet: empty if none came within timeoutMS (-1: wait for it), if the connection was lost (Connected() then
	// false) or if the packet itself is empty
	std::vector<char>& Receive(int timeoutMS = -1);
	bool TimedOut();								// the last Receive found nothing within its time
	bool Connected();
	bool Listening();								// the port could be opened

private:

	void ListenFunc();
	bool ReceiveBytes(void* data, int size);
	void CloseReceiveSocket();

	TCPsocket m_listenSocket;
	std::atomic<TCPsocket> m_receiveSocket;
	SDLNet_SocketSet m_socketSet;
	std::mutex m_socketMutex;						// the receive socket and its set, also changed by the listening thread
	std::thread m_listenThread;
	std::atomic_bool m_running;
	bool m_timedOut = false;
	std::vector<char> m_recBuffer;
};

#endif