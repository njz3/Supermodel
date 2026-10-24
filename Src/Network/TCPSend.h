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

#ifndef _TCPSEND_H_
#define _TCPSEND_H_

#include <string>
#include "SDLIncludes.h"

class TCPSend
{
public:
	TCPSend(std::string& ip, int port);
	~TCPSend();

	bool Send(const void* data, int length);	// false if not connected or the connection was lost (it is then closed)
	bool Connect();								// again after a lost connection
	bool Connected();
	void Disconnect();
private:

	std::string	m_ip;
	int			m_port;
	TCPsocket	m_socket;		// sdl socket

};

#endif
