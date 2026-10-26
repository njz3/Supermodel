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

#include <thread>
#include <algorithm>
#include "Supermodel.h"
#include "SimNetBoard.h"
#include <OSD/Thread.h>

 // these make 16-bit read/writes much neater
#define RAM16 *(uint16_t*)&RAM
#define CommRAM16 *(uint16_t*)&CommRAM

// Checked by the handshake: all the machines must run the same build. Changed with the link's handling (the
// handshake retried, the connections made again), so that a ring never mixes it with the older one
static const uint64_t netGUID = 0x5bf177da34873;

static const int kLinkTimeoutMS = 10000;			// a linked machine silent this long: the link is broken
static const int kHandshakeStepMS = 3000;			// each step of a handshake round, once started
static const uint32_t kHandshakeRetryMS = 2000;		// the master's GUID sent again when it has not come round
static const uint32_t kRoleSettleMS = 500;			// the game's role read this long after the handshake can start

inline bool CSimNetBoard::IsGame(const char* gameName)
{
	return (m_gameInfo.name == gameName) || (m_gameInfo.parent == gameName);
}

CSimNetBoard::CSimNetBoard(const Util::Config::Node& config, std::shared_ptr<CNetServer> server)
	: m_config(config),
	  m_server(std::move(server))
{
}

CSimNetBoard::~CSimNetBoard(void)
{
	m_quit = true;

	if (m_connectThread.joinable())
		m_connectThread.join();
}

void CSimNetBoard::SaveState(CBlockFile* SaveState)
{
	
}

void CSimNetBoard::LoadState(CBlockFile* SaveState)
{

}

Result CSimNetBoard::Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr)
{
	RAM = netRAMPtr;
	Buffer = netBufferPtr;

	CommRAM = Buffer;
	externalCommRAM = Buffer + 0x10000;

	m_attached = m_gameInfo.netboard_present && m_config["Network"].ValueAs<bool>();

	if (!m_attached)
		return Result::OKAY;

	if (IsGame("daytona2") || IsGame("harley") || IsGame("scud") || IsGame("srally2") ||
		IsGame("skichamp") || IsGame("spikeout") || IsGame("spikeofe"))
		m_gameType = GameType::one;
	else if (IsGame("lemans24") || IsGame("von2") || IsGame("dirtdvls"))
		m_gameType = GameType::two;
	else
		return ErrorLog("Game not recognized or supported");

	m_state = State::start;

	//netsocks
	port_in = m_config["PortIn"].ValueAs<unsigned>();
	port_out = m_config["PortOut"].ValueAs<unsigned>();
	addr_out = m_config["AddressOut"].ValueAs<std::string>();

	m_round = (CThread::GetTicks() << 8) ^ (uint32_t(port_in) << 16) ^ port_out;	// rounds differ from machine to machine
	if (!m_server)
		return ErrorLog("Net board requires the network server.");
	m_link = std::make_unique<CNetBoardLink>(m_server, addr_out, port_out);

	return Result::OKAY;
}

void CSimNetBoard::RunFrame(void)
{
	if (!IsRunning())
		return;

	switch (m_state)
	{
	case State::start:
		EnsureConnecting();
		m_status0 = 0;
		m_status1 = IsGame("dirtdvls") ? 0x4004 : 0xe000;
		m_state = State::init;
		break;

	case State::init:
		memset(CommRAM, 0, 0x20000);
		if (m_gameType == GameType::one)
		{
			if (m_status0 & 0x8000)				// has main board changed this register?
			{
				m_IRQ2ack |= 0x01;				// simulate IRQ 2 ack
				if (m_status0 == 0xf000)
				{
					// initialization complete
					m_status1 = 0;
					CommRAM16[0x72] = FLIPENDIAN16(0x1); // is this necessary?
					m_state = State::testing;
					m_testingSince = CThread::GetTicks();
				}
				m_status0 = 0;					// 0 should work for all init subroutines
			}
		}
		else
		{
			// type 2 performs initialization on its own
			m_status1 = 0;
			m_state = State::testing;
			m_testingSince = CThread::GetTicks();
			m_counter = 0;
		}
		break;

	case State::testing:
		if (m_gameType == GameType::one)
			m_status0 += 1; // type 1 games require this to be incremented every frame

		if (!m_connected)
		{
			EnsureConnecting();
			break;
		}

		if (m_gameType == GameType::one)
		{
			uint8_t index[2] = {}, count[2] = {};
			if (!Handshake(RAM16[0x400] == 0 ? Role::master : Role::slave, 1, index, count) || m_state == State::error)
				break;
			uint8_t numMachines = count[0], machineIndex = index[0];

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines + 1;

			m_status0 = 0;		// supposed to cycle between 0 and 1 (also 2 for Daytona 2); doesn't seem to matter
			m_status1 = 0x2021 + (numMachines * 0x20) + machineIndex;

			CommRAM16[0x0] = RAM16[0x400];	// 0 if master, 1 if slave
			CommRAM16[0x2] = numMachines;
			CommRAM16[0x4] = machineIndex;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			SetSegmentSize(RAM16[0x404]);

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize - 1);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x402] - m_segmentSize + 0x200);

			InfoLog("Network board linked: machine %u of %u (%s), %u bytes a machine.", machineIndex + 1u, numMachines + 1u, RAM16[0x400] ? "slave" : "master", unsigned(m_segmentSize));
			m_waitLogged = false;
			m_state = State::ready;
		}
		else
		{
			// we have to track both playable and non-playable machines for type 2
			struct
			{
				uint8_t total;
				uint8_t playable;
			} numMachines, machineIndex;

			Role role = RAM16[0x200] == 0 ? Role::master : (RAM16[0x200] < 0x8000 ? Role::slave : Role::relay);
			uint8_t index[2] = {}, count[2] = {};
			if (!Handshake(role, 2, index, count) || m_state == State::error)
				break;
			machineIndex.total = index[0]; machineIndex.playable = index[1];
			numMachines.total = count[0]; numMachines.playable = count[1];

			// indicate that this machine is a relay/satellite
			if (role == Role::relay && !IsGame("dirtdvls"))
				machineIndex.playable |= 0x80;

			// if there are no other linked machines, only continue if Supermodel is linked to itself
			// there might be more than one machine set to master which would cause glitches
			if ((numMachines.total == 0) && ((port_in != port_out) || (addr_out != "127.0.0.1")))
			{
				ErrorLog("no slave machines detected. Make sure only one machine is set to master!");
				if (IsGame("dirtdvls"))
					m_status1 = 0x8085;	// seems like the netboard code writers really liked their CPU model numbers
				m_state = State::error;
				break;
			}

			m_numMachines = numMachines.total + 1;

			m_status0 = 5;			// probably not necessary
			if (IsGame("dirtdvls"))
				m_status1 = (numMachines.playable << 4) | machineIndex.playable | 0x7400;
			else
				m_status1 = (numMachines.playable << 8) | machineIndex.playable;

			CommRAM16[0x0] = RAM16[0x200];	// master/slave/relay status
			CommRAM16[0x2] = (numMachines.playable << 8) | numMachines.total;
			CommRAM16[0x4] = (machineIndex.playable << 8) | machineIndex.total;

			m_counter = 0;
			CommRAM16[0x6] = 0;

			SetSegmentSize(RAM16[0x204]);

			// don't know if these are actually required, but it never hurts to include them
			CommRAM16[0x8] = FLIPENDIAN16(0x100 + m_segmentSize);
			CommRAM16[0xa] = FLIPENDIAN16(RAM16[0x206]);
			CommRAM16[0xc] = FLIPENDIAN16(0x100);
			CommRAM16[0xe] = FLIPENDIAN16(RAM16[0x206] + 0x80);

			InfoLog("Network board linked: machine %u of %u, player %u of %u (status %u: %s), %u bytes a machine.", machineIndex.total + 1u, numMachines.total + 1u,
				(machineIndex.playable & 0x7Fu) + 1u, numMachines.playable + 1u, unsigned(RAM16[0x200]), RAM16[0x200] == 0 ? "master" : "slave or relay", unsigned(m_segmentSize));
			m_waitLogged = false;
			m_state = State::ready;
		}
		break;

	case State::ready:
		m_counter++;
		CommRAM16[0x6] = FLIPENDIAN16(m_counter);
		
		// we only send what we need to; helps cut down on bandwidth
		// each machine has to receive back its own data (TODO: copy this data manually?)
		for (int i = 0; i < m_numMachines; i++)
		{
			if (!SendPacket(CommRAM + 0x100 + i * m_segmentSize, m_segmentSize))
			{
				LinkBroken("the next machine is gone");
				break;
			}
			Received received = ReceiveSegment(kLinkTimeoutMS);
			if (received != Received::packet)
			{
				LinkBroken(received == Received::nothing ? "no data from the previous machine for 10 seconds" :
					received == Received::lost ? "the previous machine is gone" : "another machine left the link");
				break;
			}
			// within this machine's communication RAM, whatever was received
			size_t offset = 0x100 + size_t(i + 1) * m_segmentSize;
			size_t size = std::min(m_packet.size(), size_t(m_segmentSize));
			if (offset + size <= 0x10000)
				memcpy(CommRAM + offset, m_packet.data(), size);
		}

		// swap CommRAM banks
		if (m_commbank)
		{
			m_commbank = false;
			CommRAM = Buffer;
			externalCommRAM = Buffer + 0x10000;
		}
		else
		{
			m_commbank = true;
			CommRAM = Buffer + 0x10000;
			externalCommRAM = Buffer;
		}
		
		break;

	case State::error:
		// do nothing
		break;
	}
}

void CSimNetBoard::Reset(void)
{
	// if netboard was active, send an "empty" packet so the other machines don't get stuck waiting for data. Nothing
	// is waited for here: the game resets the board from its own thread, which would freeze the whole emulation
	// (what is left to read is dropped by the next handshake)
	if (m_state == State::ready)
	{
		InfoLog("Network board reset by the game: the other machines are told the link is broken.");
		m_link->Send(nullptr, 0);
	}

	m_running = false;
	m_state = State::start;
	m_handshakeSent = false;
	m_waitLogged = false;
}

bool CSimNetBoard::IsAttached(void)
{
	return m_attached;
}

bool CSimNetBoard::IsRunning(void)
{
	return m_attached && m_running;
}

void CSimNetBoard::GetGame(const Game& gameInfo)
{
	m_gameInfo = gameInfo;
}

void CSimNetBoard::ConnectProc(void)
{
	if (m_connected)
	{
		m_connectDone = true;
		return;
	}

	printf("Connecting to %s:%i ..\n", addr_out.c_str(), port_out);
	InfoLog("Network board: connecting to the next machine (%s:%u) and waiting for the previous one (port %u).", addr_out.c_str(), port_out, port_in);

	// wait until the output connection to the next machine is open
	while (!m_link->Connect())
	{
		if (m_quit)
		{
			m_connectDone = true;
			return;
		}
		CThread::Sleep(100);
	}

	// wait until the previous machine has linked
	while (!m_link->Connected())
	{
		if (m_quit)
		{
			m_connectDone = true;
			return;
		}
		CThread::Sleep(1);
	}

	printf("Successfully connected.\n");
	InfoLog("Network board: connected.");

	m_connected = true;
	m_connectDone = true;
}

// The connections to the next machine and from the previous one, made again (in the background) if lost
void CSimNetBoard::EnsureConnecting(void)
{
	if (m_connected)
		return;
	if (m_connectThread.joinable())
	{
		if (!m_connectDone)
			return;			// still trying
		m_connectThread.join();
	}
	m_connectDone = false;
	m_connectThread = std::thread(&CSimNetBoard::ConnectProc, this);
}

bool CSimNetBoard::SendPacket(const void* data, int length)
{
	if (m_link->Connected())
	{
		m_link->Send(data, length);
		return true;
	}
	if (m_connected)
		ErrorLog("Network board: the connection to the next machine (%s:%u) or from the previous one is lost.", addr_out.c_str(), port_out);
	m_connected = false;
	return false;
}

// A packet from the previous machine, in m_packet, waited for at most timeoutMS (0: not at all)
CSimNetBoard::Received CSimNetBoard::ReceivePacket(int timeoutMS)
{
	if (m_link->WaitForData(timeoutMS > 0 ? timeoutMS : 0) && m_link->CheckDataAvailable())
	{
		std::vector<uint8_t>& data = m_link->Receive();
		m_packet.assign(data.begin(), data.end());
		return m_packet.empty() ? Received::alert : Received::packet;
	}
	if (!m_link->Connected())
	{
		if (m_connected)
			ErrorLog("Network board: the connection from the previous machine or to the next one is lost.");
		m_connected = false;
		return Received::lost;
	}
	return Received::nothing;
}

// The link is broken: the other machines are told (an empty packet), the game too (type 1), and the board waits for
// the game to reset it
void CSimNetBoard::LinkBroken(const char* reason)
{
	ErrorLog("Network board: link broken, %s.", reason);
	m_link->Send(nullptr, 0);
	m_state = State::error;
	if (m_gameType == GameType::one)
		m_status1 = 0x40;			// send "link broken" message to mainboard
	if (!m_link->Connected())
		m_connected = false;
}

// What has arrived so far, dropped (stale data of a previous link)
void CSimNetBoard::Flush(void)
{
	while (m_link->CheckDataAvailable() && ReceivePacket(0) != Received::lost)
		;
}

// The size of each machine's data, which the game writes in the board's RAM. A game restarting its board after a
// broken link may not write it again (its RAM then reads 0): it is the game's, the same on every machine, so the one
// of the previous link is kept
void CSimNetBoard::SetSegmentSize(uint16_t size)
{
	if (size)
		m_segmentSize = size;
	if (!m_segmentSize)
		ErrorLog("Network board: the game has not given the size of each machine's data.");
}

// The ready state's data from the previous machine: a packet of the segment's size, anything left of a handshake
// dropped
CSimNetBoard::Received CSimNetBoard::ReceiveSegment(int timeoutMS)
{
	uint32_t start = CThread::GetTicks();
	for (;;)
	{
		int left = std::max(0, timeoutMS - int(CThread::GetTicks() - start));
		Received received = ReceivePacket(left);
		if (received != Received::packet || m_packet.size() == m_segmentSize)
			return received;
	}
}

// The handshake's packets carry the number of the master's attempt (its round), so that what is left of an earlier
// attempt or of a previous link is told apart: a GUID packet is the GUID then the round, an index packet the round
// then the index (or count)
static const size_t kGUIDPacketSize = 12;

static uint32_t RoundOf(const std::vector<char>& packet)
{
	uint32_t round;
	memcpy(&round, packet.data() + (packet.size() == kGUIDPacketSize ? 8 : 0), sizeof(round));
	return round;
}

bool CSimNetBoard::SendGUID(uint64_t guid, uint32_t round)
{
	uint8_t packet[kGUIDPacketSize];
	memcpy(packet, &guid, 8);
	memcpy(packet + 8, &round, 4);
	return SendPacket(packet, int(kGUIDPacketSize));
}

bool CSimNetBoard::SendIndex(uint32_t round, const uint8_t value[2], size_t indexSize)
{
	uint8_t packet[6];
	memcpy(packet, &round, 4);
	memcpy(packet + 4, value, indexSize);
	return SendPacket(packet, int(4 + indexSize));
}

// A handshake packet of this size and round from the previous machine within timeoutMS (0: only what has arrived),
// anything else dropped; false otherwise (the handshake is then tried again later)
bool CSimNetBoard::ReceiveHandshake(size_t size, uint32_t round, int timeoutMS)
{
	uint32_t start = CThread::GetTicks();
	for (;;)
	{
		int left = std::max(0, timeoutMS - int(CThread::GetTicks() - start));
		Received received = ReceivePacket(left);
		if (received == Received::lost || received == Received::nothing)
			return false;
		if (received == Received::packet && m_packet.size() == size && RoundOf(m_packet) == round)
			return true;
	}
}

/*
 * Handshake():
 *
 * One attempt at the ring's handshake. The master sends the build's GUID
 * round the ring twice (all the machines must run the same build), then the
 * machine indices (each machine adds one: index[0] all, index[1] the playable
 * ones) and the number of machines found (count, likewise), of indexSize
 * bytes. Nothing waits for machines that are not there yet: the master sends
 * its GUID again every 2 seconds, in a new round; the others take the latest
 * GUID that has come and drop anything else (an earlier round, data of a
 * previous link). Once a round has started, each step waits 3 seconds at
 * most. False while the ring is not complete, true with the indices or with
 * the state set to error.
 */
bool CSimNetBoard::Handshake(Role role, size_t indexSize, uint8_t index[2], uint8_t count[2])
{
	uint64_t testGUID;
	if (role == Role::master)
	{
		uint32_t now = CThread::GetTicks();
		// the game writes its role (master or not) in the board's RAM a little after starting it: until then, every
		// machine looks like the master (after a restart, the connections being made already, the handshake would
		// start at once)
		if (now - m_testingSince < kRoleSettleMS)
			return false;
		if (!m_handshakeSent || now - m_handshakeStart > kHandshakeRetryMS)
		{
			// flush receive buffer
			Flush();
			// check all linked instances have the same GUID
			if (!SendGUID(netGUID, ++m_round))
				return false;
			if (!m_waitLogged)
				InfoLog("Network board: waiting for the other machines.");
			m_waitLogged = true;
			m_handshakeSent = true;
			m_handshakeStart = now;
		}
		if (!ReceiveHandshake(kGUIDPacketSize, m_round, 0))
			return false;	// the GUID has not come round yet
		m_handshakeSent = false;
		memcpy(&testGUID, m_packet.data(), sizeof(testGUID));
		if (testGUID != netGUID)
			testGUID = 0;

		// send the GUID for one more loop
		if (!SendGUID(testGUID, m_round) || !ReceiveHandshake(kGUIDPacketSize, m_round, kHandshakeStepMS))
			return false;

		if (testGUID != netGUID)
		{
			ErrorLog("unable to verify connection. Make sure all machines are using same build!");
			m_state = State::error;
			return true;
		}

		// master has indices set to zero; receive back the number of other linked machines and send it round
		index[0] = index[1] = 0;
		if (!SendIndex(m_round, index, indexSize) || !ReceiveHandshake(4 + indexSize, m_round, kHandshakeStepMS))
			return false;
		memcpy(count, m_packet.data() + 4, indexSize);
		return SendIndex(m_round, count, indexSize) && ReceiveHandshake(4 + indexSize, m_round, kHandshakeStepMS);
	}

	// slave or relay: the latest GUID from the previous machine, anything else dropped
	std::vector<char> guidPacket;
	while (m_link->CheckDataAvailable())
	{
		Received received = ReceivePacket(0);
		if (received == Received::lost)
			return false;
		if (received == Received::packet && m_packet.size() == kGUIDPacketSize)
			guidPacket = m_packet;
	}
	if (guidPacket.empty())
		return false;
	uint32_t round = RoundOf(guidPacket);
	memcpy(&testGUID, guidPacket.data(), sizeof(testGUID));
	if (testGUID != netGUID)
		testGUID = 0;
	if (!SendGUID(testGUID, round))
		return false;

	// one more time, in case a later machine has a GUID mismatch
	if (!ReceiveHandshake(kGUIDPacketSize, round, kHandshakeStepMS))
		return false;
	memcpy(&testGUID, m_packet.data(), sizeof(testGUID));
	if (testGUID != netGUID)
		testGUID = 0;
	if (!SendGUID(testGUID, round))
		return false;

	if (testGUID != netGUID)
	{
		ErrorLog("unable to verify connection. Make sure all machines are using same build!");
		m_state = State::error;
		return true;
	}

	// receive the indices of the previous machine, increment them (a relay doesn't play), send them to the next one
	if (!ReceiveHandshake(4 + indexSize, round, kHandshakeStepMS))
		return false;
	memcpy(index, m_packet.data() + 4, indexSize);
	index[0]++;
	if (role == Role::slave)
		index[1]++;
	if (!SendIndex(round, index, indexSize))
		return false;

	// receive the number of machines and forward it
	if (!ReceiveHandshake(4 + indexSize, round, kHandshakeStepMS))
		return false;
	memcpy(count, m_packet.data() + 4, indexSize);
	return SendIndex(round, count, indexSize);
}

uint8_t CSimNetBoard::ReadCommRAM8(unsigned addr)
{
	return externalCommRAM[addr];
}

uint16_t CSimNetBoard::ReadCommRAM16(unsigned addr)
{
	return *(uint16_t*)&externalCommRAM[addr];
}

uint32_t CSimNetBoard::ReadCommRAM32(unsigned addr)
{
	return *(uint32_t*)&externalCommRAM[addr];
}

void CSimNetBoard::WriteCommRAM8(unsigned addr, uint8_t data)
{
	externalCommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM16(unsigned addr, uint16_t data)
{
	*(uint16_t*)&externalCommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM32(unsigned addr, uint32_t data)
{
	*(uint32_t*)&externalCommRAM[addr] = data;
}

uint16_t CSimNetBoard::ReadIORegister(unsigned reg)
{
	if (!IsRunning())
		return 0;

	switch (reg)
	{
	case 0x00:
		return m_IRQ2ack;
	case 0x88:
		return m_status0;
	case 0x8a:
		return m_status1;
	default:
		ErrorLog("read from unknown IO register 0x%02x", reg);
		return 0;
	}
}

void CSimNetBoard::WriteIORegister(unsigned reg, uint16_t data)
{
	switch (reg)
	{
	case 0x00:
		m_IRQ2ack = data;
		break;
	case 0x88:
		m_status0 = data;
		break;
	case 0x8a:
		m_status1 = data;
		break;
	case 0xc0:
		if (data == 0)
			Reset();
		m_running = (data != 0);
		break;
	default:
		ErrorLog("write to unknown IO register 0x%02x", reg);
	}
}
