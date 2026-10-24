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

#ifndef INCLUDED_SIMNETBOARD_H
#define INCLUDED_SIMNETBOARD_H

#include <cstdint>
#include "TCPSend.h"
#include "TCPReceive.h"
#include "INetBoard.h"

enum class State
{
	start,
	init,
	testing,
	ready,
	error
};

enum class GameType
{
	unknown,
	one,		// all games except those listed below
	two			// Le Mans 24, Virtual-On, Dirt Devils
};

class CSimNetBoard : public INetBoard
{
public:
	CSimNetBoard(const Util::Config::Node& config);
	~CSimNetBoard(void);

	void SaveState(CBlockFile* SaveState);
	void LoadState(CBlockFile* SaveState);

	Result Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr);
	void RunFrame(void);
	void Reset(void);

	bool IsAttached(void);
	bool IsRunning(void);

	void GetGame(const Game& gameInfo);

	uint8_t ReadCommRAM8(unsigned addr);
	uint16_t ReadCommRAM16(unsigned addr);
	uint32_t ReadCommRAM32(unsigned addr);

	void WriteCommRAM8(unsigned addr, uint8_t data);
	void WriteCommRAM16(unsigned addr, uint16_t data);
	void WriteCommRAM32(unsigned addr, uint32_t data);

	uint16_t ReadIORegister(unsigned reg);
	void WriteIORegister(unsigned reg, uint16_t data);

private:
	// Config
	const Util::Config::Node& m_config;

	uint8_t* RAM = nullptr;
	uint8_t* Buffer = nullptr;
	uint8_t* CommRAM = nullptr;
	uint8_t* externalCommRAM = nullptr;

	// netsock
	uint16_t port_in = 0;
	uint16_t port_out = 0;
	std::string addr_out;
	std::thread m_connectThread;
	std::atomic_bool m_quit = false;
	std::atomic_bool m_connected = false;
	std::atomic_bool m_connectDone = true;		// ConnectProc has returned

	// Link handling (see Handshake)
	enum class Role { master, slave, relay };
	enum class Received { packet, nothing, lost, alert };	// alert: an empty packet, another machine left the link
	std::vector<char> m_packet;
	bool m_handshakeSent = false;				// the master's GUID is going round
	uint32_t m_handshakeStart = 0;
	uint32_t m_round = 0;					// the master's handshake attempt
	uint32_t m_testingSince = 0;			// when the testing state (the handshake) began
	bool m_waitLogged = false;

	void EnsureConnecting(void);
	bool SendPacket(const void* data, int length);
	Received ReceivePacket(int timeoutMS);
	void SetSegmentSize(uint16_t size);
	Received ReceiveSegment(int timeoutMS);
	bool SendGUID(uint64_t guid, uint32_t round);
	bool SendIndex(uint32_t round, const uint8_t value[2], size_t indexSize);
	bool ReceiveHandshake(size_t size, uint32_t round, int timeoutMS);
	void Flush(void);
	void LinkBroken(const char* reason);
	bool Handshake(Role role, size_t indexSize, uint8_t index[2], uint8_t count[2]);

	std::unique_ptr<TCPSend> nets = nullptr;
	std::unique_ptr<TCPReceive> netr = nullptr;

	Game m_gameInfo;
	GameType m_gameType = GameType::unknown;
	State m_state = State::start;

	uint8_t m_numMachines = 0;
	uint16_t m_counter = 0;

	uint16_t m_segmentSize = 0;

	bool m_attached = false;
	bool m_running = false;

	uint16_t m_IRQ2ack = 0;
	uint16_t m_status0 = 0;	// ioreg 0x88
	uint16_t m_status1 = 0;	// ioreg 0x8a
	bool m_commbank = false;

	inline bool IsGame(const char* gameName);
	void ConnectProc(void);
};

#endif