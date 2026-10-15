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

/*
 * TelemetryServer.h
 *
 * Serves the racing games' telemetry (Telemetry/Telemetry.h): a TCP server
 * sending each client one JSON line a frame (TelemetryPort, -telemetry=PORT),
 * and Forza Data Out datagrams to a listener (TelemetryForza,
 * -telemetry-forza=HOST:PORT). A client's lines, each answered with one JSON
 * line ({"ok":...} or {"error":...}):
 *
 *   fields all | fields PATH,PATH...   the keys sent to this client
 *   others N | others all              the other cars, the nearest first
 *   every N                            one frame in N
 *   udp forza HOST:PORT | udp off      the Forza datagrams
 *   list                               the keys the game fills
 *
 * An empty line, or "go", acknowledges a frame (sm2-emu's synchronous mode)
 * and gets no answer. The same protocol as sm2-emu's, the Model 2 emulator.
 */

#ifndef INCLUDED_TELEMETRYSERVER_H
#define INCLUDED_TELEMETRYSERVER_H

#include "Telemetry/Telemetry.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class IEmulator;
class CModel3;

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

namespace Telemetry
{
  class CServer
  {
  public:
    bool Open(unsigned port);
    void Close();

    // Forza Data Out to HOST:PORT; false if it does not resolve
    bool SetForza(const std::string &hostPort);
    void StopForza();

    // Accepts clients and answers their lines
    void Poll(const Frame *last);

    void Publish(const Frame &frame);

    // Anyone listening: a TCP client or the Forza datagrams
    bool Wanted() const;

    ~CServer();

  private:
    struct Client;
    std::string Command(Client &client, const std::string &line, const Frame *last);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
  };

  // The telemetry of the game running: its decoder, the derived values, the server
  class CService
  {
  public:
    // From the settings (TelemetryPort, TelemetryForza); always decodes in agent mode, for its tel command
    void Init(const Util::Config::Node &config, IEmulator *emulator, bool always);
    void Shutdown();

    // After each frame run
    void OnFrame(uint64_t frame);

    // Each pass of the loop while Supermodel is paused: the last frame again, its state pause
    void OnPause();

    // The last frame as JSON, or "tel none" when the game is not decoded or says nothing
    std::string Json() const;

  private:
    CModel3 *m_model3 = nullptr;
    std::unique_ptr<IDecoder> m_decoder;
    CDeriver m_deriver;
    Frame m_last;
    bool m_haveLast = false;
    bool m_always = false;
    uint32_t m_lastPauseTicks = 0;
    CServer m_server;
  };

  CService &Service();
}

#endif  // INCLUDED_TELEMETRYSERVER_H
