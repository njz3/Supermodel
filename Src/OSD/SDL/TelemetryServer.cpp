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
 * TelemetryServer.cpp
 *
 * The telemetry's TCP server and Forza datagrams, over SDL_net, in the
 * emulation's own thread: polled each frame, never waiting.
 */

#include "TelemetryServer.h"

#include "Supermodel.h"
#include "SDLIncludes.h"
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "Util/NewConfig.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace Telemetry
{
  static const int kMaxClients = 16;

  struct CServer::Client
  {
    TCPsocket socket = nullptr;
    std::string pending;      // what came in since the last line feed
    Selection selection;
    unsigned every = 1;
  };

  struct CServer::Impl
  {
    TCPsocket listener = nullptr;
    SDLNet_SocketSet set = nullptr;
    std::vector<Client> clients;
    UDPsocket udp = nullptr;
    UDPpacket *packet = nullptr;
    IPaddress forza{};
    bool forzaOn = false;
    bool netInit = false;
  };

  CServer::~CServer()
  {
    Close();
  }

  static bool InitNet(bool &done)
  {
    if (!done)
    {
      if (SDLNet_Init() < 0)
      {
        ErrorLog("Telemetry: SDLNet_Init failed: %s", SDLNet_GetError());
        return false;
      }
      done = true;
    }
    return true;
  }

  bool CServer::Open(unsigned port)
  {
    if (!m_impl)
      m_impl = std::make_unique<Impl>();
    if (!InitNet(m_impl->netInit))
      return false;
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, nullptr, uint16_t(port)) < 0 || !(m_impl->listener = SDLNet_TCP_Open(&ip)))
    {
      ErrorLog("Telemetry: cannot listen on TCP port %u: %s", port, SDLNet_GetError());
      return false;
    }
    m_impl->set = SDLNet_AllocSocketSet(kMaxClients + 1);
    SDLNet_TCP_AddSocket(m_impl->set, m_impl->listener);
    InfoLog("Telemetry: serving on TCP port %u", port);
    return true;
  }

  void CServer::Close()
  {
    if (!m_impl)
      return;
    for (auto &c : m_impl->clients)
      SDLNet_TCP_Close(c.socket);
    m_impl->clients.clear();
    if (m_impl->listener)
      SDLNet_TCP_Close(m_impl->listener);
    m_impl->listener = nullptr;
    if (m_impl->set)
      SDLNet_FreeSocketSet(m_impl->set);
    m_impl->set = nullptr;
    StopForza();
    if (m_impl->packet)
      SDLNet_FreePacket(m_impl->packet);
    m_impl->packet = nullptr;
    if (m_impl->udp)
      SDLNet_UDP_Close(m_impl->udp);
    m_impl->udp = nullptr;
  }

  bool CServer::SetForza(const std::string &hostPort)
  {
    if (!m_impl)
      m_impl = std::make_unique<Impl>();
    if (!InitNet(m_impl->netInit))
      return false;
    size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos)
      return false;
    std::string host = hostPort.substr(0, colon);
    int port = atoi(hostPort.c_str() + colon + 1);
    IPaddress ip;
    if (port <= 0 || port > 65535 || SDLNet_ResolveHost(&ip, host.c_str(), uint16_t(port)) < 0)
      return false;
    if (!m_impl->udp && !(m_impl->udp = SDLNet_UDP_Open(0)))
      return false;
    if (!m_impl->packet)
      m_impl->packet = SDLNet_AllocPacket(int(kForzaDashSize));
    m_impl->forza = ip;
    m_impl->forzaOn = true;
    InfoLog("Telemetry: Forza Data Out to %s", hostPort.c_str());
    return true;
  }

  void CServer::StopForza()
  {
    if (m_impl)
      m_impl->forzaOn = false;
  }

  bool CServer::Wanted() const
  {
    return m_impl && (!m_impl->clients.empty() || m_impl->forzaOn);
  }

  static std::string Trim(const std::string &s)
  {
    size_t a = s.find_first_not_of(" \t\r");
    size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  }

  std::string CServer::Command(Client &client, const std::string &line, const Frame *last)
  {
    std::istringstream in(line);
    std::string command, arg;
    in >> command;
    std::getline(in, arg);
    arg = Trim(arg);
    if (command == "fields")
    {
      client.selection.fields.clear();
      if (arg != "all")
      {
        std::stringstream list(arg);
        std::string f;
        while (std::getline(list, f, ','))
        {
          f = Trim(f);
          if (!f.empty())
            client.selection.fields.push_back(f);
        }
      }
      return "{\"ok\":\"fields\"}";
    }
    if (command == "others")
    {
      if (arg == "all")
        client.selection.others = -1;
      else
      {
        char *end = nullptr;
        long n = strtol(arg.c_str(), &end, 10);
        if (arg.empty() || *end != 0 || n < 0)
          return "{\"error\":\"usage: others N | others all\"}";
        client.selection.others = int(n);
      }
      return "{\"ok\":\"others\"}";
    }
    if (command == "every")
    {
      long n = atol(arg.c_str());
      if (n < 1)
        return "{\"error\":\"usage: every N, N at least 1\"}";
      client.every = unsigned(n);
      return "{\"ok\":\"every\"}";
    }
    if (command == "udp")
    {
      std::istringstream a(arg);
      std::string format, target;
      a >> format >> target;
      if (format == "off")
      {
        StopForza();
        return "{\"ok\":\"udp\"}";
      }
      if (format != "forza" || !SetForza(target))
        return "{\"error\":\"usage: udp forza HOST:PORT | udp off\"}";
      return "{\"ok\":\"udp\"}";
    }
    if (command == "list")
      return std::string("{\"ok\":\"list\",\"keys\":") + (last ? KnownKeys(*last) : "[]") + "}";
    return "{\"error\":\"unknown command: " + command + "\"}";
  }

  void CServer::Poll(const Frame *last)
  {
    if (!m_impl || !m_impl->listener)
      return;
    while (SDLNet_CheckSockets(m_impl->set, 0) > 0)
    {
      bool any = false;
      if (SDLNet_SocketReady(m_impl->listener))
      {
        any = true;
        TCPsocket s = SDLNet_TCP_Accept(m_impl->listener);
        if (s)
        {
          if (m_impl->clients.size() >= size_t(kMaxClients))
            SDLNet_TCP_Close(s);
          else
          {
            SDLNet_TCP_AddSocket(m_impl->set, s);
            Client c;
            c.socket = s;
            m_impl->clients.push_back(c);
          }
        }
      }
      for (size_t i = 0; i < m_impl->clients.size(); )
      {
        Client &c = m_impl->clients[i];
        if (!SDLNet_SocketReady(c.socket))
        {
          i++;
          continue;
        }
        any = true;
        char buf[512];
        int n = SDLNet_TCP_Recv(c.socket, buf, sizeof(buf));
        if (n <= 0)
        {
          SDLNet_TCP_DelSocket(m_impl->set, c.socket);
          SDLNet_TCP_Close(c.socket);
          m_impl->clients.erase(m_impl->clients.begin() + i);
          continue;
        }
        c.pending.append(buf, size_t(n));
        size_t lf;
        while ((lf = c.pending.find('\n')) != std::string::npos)
        {
          std::string line = Trim(c.pending.substr(0, lf));
          c.pending.erase(0, lf + 1);
          if (line.empty() || line == "go")
            continue;           // an acknowledgement
          std::string reply = Command(c, line, last) + "\n";
          SDLNet_TCP_Send(c.socket, reply.data(), int(reply.size()));
        }
        i++;
      }
      if (!any)
        break;
    }
  }

  void CServer::Publish(const Frame &frame)
  {
    if (!m_impl)
      return;
    // Clients with the same selection share a line
    std::map<std::string, std::string> lines;
    for (size_t i = 0; i < m_impl->clients.size(); )
    {
      Client &c = m_impl->clients[i];
      if (frame.frame % c.every != 0)
      {
        i++;
        continue;
      }
      std::string key = std::to_string(c.selection.others);
      for (auto &f : c.selection.fields)
        key += "," + f;
      auto it = lines.find(key);
      if (it == lines.end())
        it = lines.emplace(key, ToJson(frame, c.selection) + "\n").first;
      if (SDLNet_TCP_Send(c.socket, it->second.data(), int(it->second.size())) < int(it->second.size()))
      {
        SDLNet_TCP_DelSocket(m_impl->set, c.socket);
        SDLNet_TCP_Close(c.socket);
        m_impl->clients.erase(m_impl->clients.begin() + i);
        continue;
      }
      i++;
    }
    if (m_impl->forzaOn && m_impl->udp && m_impl->packet)
    {
      auto data = ForzaDash(frame);
      memcpy(m_impl->packet->data, data.data(), data.size());
      m_impl->packet->len = int(data.size());
      m_impl->packet->address = m_impl->forza;
      SDLNet_UDP_Send(m_impl->udp, -1, m_impl->packet);
    }
  }

  /*
   * The service
   */

  void CService::Init(const Util::Config::Node &config, IEmulator *emulator, bool always)
  {
    m_model3 = dynamic_cast<CModel3 *>(emulator);
    m_decoder = m_model3 ? MakeDecoder(emulator->GetGame()) : nullptr;
    m_always = always;
    m_haveLast = false;
    unsigned port = config["TelemetryPort"].ValueAsDefault<unsigned>(0);
    std::string forza = config["TelemetryForza"].ValueAsDefault<std::string>("");
    if (!m_decoder)
    {
      if (port != 0 || !forza.empty())
        InfoLog("Telemetry: no decoder for %s", emulator->GetGame().name.c_str());
      return;
    }
    if (port != 0)
      m_server.Open(port);
    if (!forza.empty() && !m_server.SetForza(forza))
      ErrorLog("Telemetry: cannot send Forza Data Out to %s", forza.c_str());
  }

  void CService::Shutdown()
  {
    m_server.Close();
    m_decoder.reset();
  }

  void CService::OnFrame(uint64_t frame)
  {
    if (!m_decoder)
      return;
    m_server.Poll(m_haveLast ? &m_last : nullptr);
    if (!m_always && !m_server.Wanted())
    {
      m_haveLast = false;   // the derived values restart when someone listens again
      return;
    }
    Frame f;
    if (!m_decoder->Decode(*m_model3, frame, f))
      return;
    m_deriver.Derive(f);
    m_last = std::move(f);
    m_haveLast = true;
    m_server.Publish(m_last);
  }

  void CService::OnPause()
  {
    if (!m_decoder)
      return;
    m_server.Poll(m_haveLast ? &m_last : nullptr);
    if (!m_haveLast)
      return;
    // At the frame rate, however fast the paused loop goes
    uint32_t now = SDL_GetTicks();
    if (now - m_lastPauseTicks < 17)
      return;
    m_lastPauseTicks = now;
    Frame f = m_last;
    f.state = State::Pause;
    f.racing = false;
    m_server.Publish(f);
  }

  std::string CService::Json(const Selection &selection) const
  {
    if (!m_decoder || !m_haveLast)
      return "tel none";
    return ToJson(m_last, selection);
  }

  CService &Service()
  {
    static CService service;
    return service;
  }
}
