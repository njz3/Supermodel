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

#include "TCPSend.h"
#include "OSD/Logger.h"

#ifndef _WIN32
#include <csignal>
#endif

#if defined(_DEBUG)
#include <stdio.h>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

TCPSend::TCPSend(std::string& ip, int port) :
	m_ip(ip),
	m_port(port),
	m_socket(nullptr)
{
	SDLNet_Init();

#ifndef _WIN32
	// Sending to a machine that went away must fail, not end Supermodel without a word
	std::signal(SIGPIPE, SIG_IGN);
#endif
}

TCPSend::~TCPSend()
{
	Disconnect();

	SDLNet_Quit();	// unload lib (winsock dll for windows)
}

bool TCPSend::Send(const void * data, int length)
{
	// If we failed bail out
	if (!Connected()) {
		DPRINTF("Not connected\n");
		return false;
	}

	DPRINTF("Sending %i bytes\n", length);

	int sent = SDLNet_TCP_Send(m_socket, &length, sizeof(int));		// pack the length at the start of transmission.

	if (sent < int(sizeof(int))) {
		Disconnect();
		return false;
	}

	if (!length)
		return true;		// 0 sized packet will blow our connex

	sent = SDLNet_TCP_Send(m_socket, data, length);

	if (sent < length) {
		Disconnect();
		return false;
	}

	return true;
}

void TCPSend::Disconnect()
{
	if (m_socket) {
		SDLNet_TCP_Close(m_socket);
		m_socket = nullptr;
	}
}

bool TCPSend::Connected()
{
	return m_socket != 0;
}

bool TCPSend::Connect()
{
	if (Connected())
		return true;

	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, m_ip.c_str(), m_port);

	if (result == 0) {
		m_socket = SDLNet_TCP_Open(&ip);
	}

	return Connected();
}
