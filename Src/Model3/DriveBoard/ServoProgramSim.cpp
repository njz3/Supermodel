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
 * ServoProgramSim.cpp
 *
 * The Model 3 drive boards' programs simulated (see ServoProgramSim.h), from
 * their disassemblies documented in BackForceFeeder's docs (EPR-19338A,
 * EPR-19338, EPR-20985, EPR-20512). The programs share a skeleton: the NMI (500 Hz) does
 * all the input and output (the command, debounced over 4 NMIs; the reply; one
 * servo board register and its value; the ADCs; the timers), the main code
 * runs between NMIs and only changes the RAM. Here each NMI runs the NMI's
 * part, then the main code's passes (some 20 run between two NMIs on the Z80).
 */

#include "ServoProgramSim.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace
{
  // The servo board's register queue: pairs (MODE, TQ) the NMI sends one at a time; a new sequence replaces it
  struct Queue
  {
    UINT8 pairs[16][2];
    int len;
  };

  // What the programs share, at the same RAM addresses in the three (ImportRam)
  struct CommonState
  {
    UINT8 phase, phaseEntered;                          // the main code's start-up phase (not in RAM)
    UINT8 cmdSample, cmdState, cmdCount, cmdNew, command; // E480-E488
    UINT8 replySel, initReply, status, reply;           // E312, E48A, E314
    UINT8 outMode, outTq, out2d, outEngine, outEngineS; // E118, E110, E128, E30C, E30E
    Queue queue;                                        // E800, E504
    UINT8 wheel, cockpit;                               // E401, E402
    UINT8 wheelPrev, motionSt, motionCnt, motionTick, motionDue; // E404-E408, E48E, E490
    UINT8 tickNmi, tick100ms, tick5nmi, tick10ms;       // E200-E203
    UINT8 centre, cockpitCtr, cabinet, mode;            // E302, E306, E3DA, E326
    uint16_t rndSeed;                                   // E318
    uint16_t slideTab;                                  // the slide wave (4x): E344-E354
    UINT8 slideCnt, slidePer, slideIdx, slideDir, slideHalf;
  };

  template <class State>
  class CServoProgram : public IServoProgram
  {
  public:
    void SetCommand(UINT8 data) override
    {
      m_cmdIn = data;
    }

    UINT8 Reply(void) const override
    {
      return s.reply;
    }

  protected:
    State s;
    const UINT8 *m_rom = nullptr;
    UINT8 m_dip1 = 0xCF, m_dip2 = 0xFF, m_cmdIn = 0;

    // The power-on values the programs share
    void ResetCommon(const UINT8 *rom, UINT8 dip1, UINT8 dip2)
    {
      m_rom = rom;
      m_dip1 = dip1;
      m_dip2 = dip2;
      m_cmdIn = 0;
      memset(&s, 0, sizeof(s));
      s.status = 0xFF;
      s.motionTick = 50;
      s.centre = 0x80;
      s.wheelPrev = 0x80;
      s.cockpitCtr = 0x80;
      s.outEngine = 1;
      s.mode = 1;
      s.replySel = 1;
      s.outTq = s.outMode = s.out2d = 0xFF;
      s.reply = 0xFF;
      s.wheel = 0x80;
      s.cockpit = 0x80;
    }

    void SaveBlock(CBlockFile *saveState, const char *name)
    {
      saveState->NewBlock(name, __FILE__);
      saveState->Write(s);
      saveState->Write(m_cmdIn);
    }

    void LoadBlock(CBlockFile *saveState, const char *name)
    {
      if (saveState->FindBlock(name) == Result::OKAY)
      {
        saveState->Read(s);
        saveState->Read(m_cmdIn);
      }
    }

    static UINT8 RamB(const UINT8 *ram, uint16_t a)
    {
      return ram[a - 0xE000];
    }

    static uint16_t RamW(const UINT8 *ram, uint16_t a)
    {
      return uint16_t(ram[a - 0xE000] | (ram[a - 0xE000 + 1] << 8));
    }

    // The shared state from the emulated board's RAM (E000-FFFF)
    void ImportCommon(const UINT8 *ram)
    {
      s.phaseEntered = 1;
      s.cmdSample = RamB(ram, 0xE480);
      s.cmdState = RamB(ram, 0xE482);
      s.cmdCount = RamB(ram, 0xE484);
      s.cmdNew = RamB(ram, 0xE486);
      s.command = RamB(ram, 0xE488);
      m_cmdIn = s.cmdSample;
      s.initReply = RamB(ram, 0xE48A);
      s.replySel = RamB(ram, 0xE312);
      s.status = RamB(ram, 0xE314);
      s.outTq = RamB(ram, 0xE110);
      s.outMode = RamB(ram, 0xE118);
      s.out2d = RamB(ram, 0xE128);
      s.outEngine = RamB(ram, 0xE30C);
      s.outEngineS = RamB(ram, 0xE30E);
      s.queue.len = std::min<int>(RamB(ram, 0xE504), 16);
      for (int i = 0; i < s.queue.len; i++)
      {
        s.queue.pairs[i][0] = RamB(ram, uint16_t(0xE800 + 2 * i));
        s.queue.pairs[i][1] = RamB(ram, uint16_t(0xE801 + 2 * i));
      }
      s.wheel = RamB(ram, 0xE401);
      s.cockpit = RamB(ram, 0xE402);
      s.wheelPrev = RamB(ram, 0xE404);
      s.motionSt = RamB(ram, 0xE406);
      s.motionCnt = RamB(ram, 0xE408);
      s.motionTick = RamB(ram, 0xE48E);
      s.motionDue = RamB(ram, 0xE490);
      s.tickNmi = RamB(ram, 0xE200);
      s.tick100ms = RamB(ram, 0xE201);
      s.tick5nmi = RamB(ram, 0xE202);
      s.tick10ms = RamB(ram, 0xE203);
      s.centre = RamB(ram, 0xE302);
      s.cockpitCtr = RamB(ram, 0xE306);
      s.cabinet = RamB(ram, 0xE3DA);
      s.mode = RamB(ram, 0xE326);
      s.rndSeed = RamW(ram, 0xE318);
    }

    UINT8 R(uint16_t a) const { return m_rom[a]; }
    uint16_t R16(uint16_t a) const { return uint16_t(m_rom[a] | (m_rom[uint16_t(a + 1)] << 8)); }

    // The programs rotate 80 left (c & F) + 1 times: n and n + 8 give the same bit
    static UINT8 Bit(UINT8 c) { return UINT8(1 << (c & 7)); }

    static int LowestBit(UINT8 v, int from, int to)
    {
      for (int b = from; b <= to; b++)
        if (v & (1 << b))
          return b;
      return -1;
    }

    void SetQueue(std::initializer_list<std::pair<UINT8, UINT8>> list)
    {
      s.queue.len = 0;
      for (auto &p : list)
      {
        s.queue.pairs[s.queue.len][0] = p.first;
        s.queue.pairs[s.queue.len][1] = p.second;
        s.queue.len++;
      }
    }

    UINT8 Random(void)
    {
      s.rndSeed = uint16_t(s.rndSeed * 5 + 0x3711);
      return UINT8(s.rndSeed >> 8);
    }

    void TicksZero(void)
    {
      s.tickNmi = 0;
      s.tick100ms = 0;
    }

    void Enter(UINT8 phase)
    {
      s.phase = phase;
      s.phaseEntered = 0;
    }

    // A command taken during a start-up window: CA (the test mode), a byte in lo..hi (the value asked for), or
    // anything else (dropped)
    enum StartupCommand { NoCommand, InRange, TestMode };

    StartupCommand TakeStartupCommand(UINT8 lo, UINT8 hi, UINT8 *value)
    {
      if (s.cmdNew != 0xFF)
        return NoCommand;
      s.cmdNew = 0;
      UINT8 c = s.command;
      if (c == 0xCA)
        return TestMode;
      if (value != nullptr && c >= lo && c <= hi)
      {
        *value = c;
        return InRange;
      }
      return NoCommand;
    }

    // -- The NMI's parts ---------------------------------------------------------------------------------------------

    // A byte is taken at the 4th NMI in a row that reads it, if it differs from the last taken
    void ReadCommand(void)
    {
      UINT8 b = m_cmdIn;
      if (b != s.cmdSample)
      {
        s.cmdSample = b;
        s.cmdState = 0;
        return;
      }
      if (s.cmdState & 2)
        return;
      if (!(s.cmdState & 1))
      {
        s.cmdCount = 3;
        s.cmdState |= 1;
      }
      if (--s.cmdCount != 0)
        return;
      s.cmdState |= 2;
      if (s.command == b)
        return;
      s.command = b;
      s.cmdNew = 0xFF;
    }

    void WriteReply(void)
    {
      UINT8 r;
      UINT8 sel = s.replySel;
      if (sel & 0x80) r = s.initReply;
      else if (sel & 0x01) r = s.status;
      else if (sel & 0x02) r = m_dip1;
      else if (sel & 0x04) r = m_dip2;
      else if (sel & 0x08) r = s.centre;
      else if (sel & 0x10) r = s.cockpitCtr;
      else if (sel & 0x20) r = s.wheel;
      else if (sel & 0x40) r = s.cockpit;
      else r = s.status;
      s.reply = r;
    }

    void OutQueue(void)
    {
      if (s.queue.len == 0)
        return;
      s.outMode = s.queue.pairs[0][0];
      s.outTq = s.queue.pairs[0][1];
      s.queue.len--;
      memmove(s.queue.pairs[0], s.queue.pairs[1], sizeof(s.queue.pairs[0]) * s.queue.len);
    }

    void OutSlide(void)
    {
      if (--s.slideCnt != 0)
        return;
      s.slideCnt = s.slidePer;
      UINT8 x = R(uint16_t(s.slideTab + s.slideIdx));
      s.outMode = 0xFE;
      s.outTq = (s.slideDir == 0xFF) ? UINT8(0x80 - x) : UINT8(0x80 + x);
      if (++s.slideIdx == 180)
      {
        s.slideIdx = 0;
        s.slideDir = UINT8(~s.slideDir);
        s.slideHalf++;
      }
    }

    void OutShake(uint16_t table, uint16_t &i, UINT8 &flag)
    {
      UINT8 v = R(uint16_t(table + i));
      i++;
      if (v == 0)
      {
        flag = 0;
        return;
      }
      s.outTq = v;
      s.outMode = 0xFE;
    }

    // EPR-19338's wheel_motion: the left confirmed, it goes on into the right's code (a JP NZ never taken)
    bool m_leftFallsIntoRight = false;

    // Every 50 NMIs: the wheel's motion in the status (bits 7:6), a direction seen twice in a row
    void WheelMotion(void)
    {
      if (s.motionDue != 0xFF)
        return;
      s.motionDue = 0;
      if (s.wheel > s.wheelPrev)
        MotionStep(0x10, 0x20, 0x40);   // going right
      else if (s.wheel < s.wheelPrev)
      {
        if (MotionStep(0x01, 0x02, 0x80) && m_leftFallsIntoRight)  // going left
          MotionStep(0x10, 0x20, 0x40);
      }
      else
        MotionStep(0x40, 0x80, 0xC0);   // still
      s.wheelPrev = s.wheel;
    }

    // True when the direction is confirmed by this sample
    bool MotionStep(UINT8 first, UINT8 seen, UINT8 bits)
    {
      if (s.motionSt & seen)
        return false;
      if (!(s.motionSt & first))
      {
        s.motionSt = first;
        s.motionCnt = 2;
      }
      if (--s.motionCnt != 0)
        return false;
      s.motionSt = seen;
      s.status = UINT8((s.status & 0x3F) | bits);
      return true;
    }
  };

  /*
   * EPR-20985, Daytona USA 2's drive board (CRC32 B139481D). It computes the spring itself, a torque by the wheel's
   * distance from a moving centre read in tables, sent as FE at every NMI, plus the road effects; the servo board's
   * own spring (FC 4) stays off. Start-up: CF 8 s, CE 1 s (the cockpit's centre), CD 1 s (the wheel's), the
   * calibration, the servo board's set-up, CC 1 s (the cabinet B0/B1), then the modes C1 (attract), C4 (strength,
   * 7x), C6 (shake), C7 (the game), C8, C9, CA (test), CB (start again).
   */
  struct Daytona2State : CommonState
  {
    // Timers
    UINT8 tickANmi, tickA250, tickBNmi, tickB250, tick2nmi, tick4ms;
    uint16_t torqueTmr;
    UINT8 road2Wait;
    // The spring
    uint16_t servoSet, paramRow, springSets, gainTab;
    UINT8 springCtr, springCol, followLag, returnPer, lock7x;
    // The game
    UINT8 gameReady, gameReq, springSt, vibSt, gainSt, gainN, torqueSt, torqueN, joltSt, joltDelay;
    uint16_t vibPtr, torqueTab;
    UINT8 slideSt, slideBit;
    UINT8 roadOn, roadSel, roadSt[8], road2St;
    uint16_t roadIdx;
    // The other modes
    UINT8 attractSt, patternSt, shakeReq, shakeC2St, shakeC6On, c4State, strengthBit, strengthNew, c6State;
    UINT8 c8State, c8Count, c9State, caState, testCmd, testBusy, cbState, calDir, kickOn, gameInitQueued;
    uint16_t shakeC2I, shakeC6I;
  };

  class CDaytona2Program : public CServoProgram<Daytona2State>
  {
  public:
    void Reset(const UINT8 *rom, UINT8 dip1, UINT8 dip2) override
    {
      ResetCommon(rom, dip1, dip2);
      s.servoSet = 0x1CC8;
      s.gainTab = 0x42A8;
      // DIP switches 1 (ON reads 0): bit 5 the cabinet (OFF B1, ON B0), bit 1 ON 7x ignored
      if (dip1 & 0x20)
      {
        s.cabinet = 0xFF;
        s.paramRow = 0x1C08;
        s.springSets = 0x1C88;
      }
      else
      {
        s.cabinet = 0x00;
        s.paramRow = 0x1C48;
        s.springSets = 0x1CA8;
      }
      s.lock7x = (dip1 & 0x02) ? 0x00 : 0xFF;
      // RESTART, before the first NMI (CWheelBoard times the NMIs as the Z80's CTC: the first after the ROM and RAM tests)
      Restart();
    }

    double FirstNmiCycles(void) const override
    {
      return 2102083;
    }

    bool Nmi(UINT8 wheel, UINT8 cockpit, UINT8 &mode, UINT8 &value) override
    {
      NmiBody(wheel, cockpit);
      mode = s.outMode;
      value = s.outTq;
      // The main code between this NMI and the next
      for (int pass = 0; pass < 20; pass++)
        MainPass();
      return true;
    }

    void SaveState(CBlockFile *saveState) override
    {
      SaveBlock(saveState, "ServoProgram20985");
    }

    void LoadState(CBlockFile *saveState) override
    {
      LoadBlock(saveState, "ServoProgram20985");
    }

    void ImportRam(const UINT8 *ram) override
    {
      // The emulated board's RAM (E000-FFFF): the state that matters for a game going on
      ImportCommon(ram);
      auto b = [ram](uint16_t a) { return RamB(ram, a); };
      auto w = [ram](uint16_t a) { return RamW(ram, a); };
      s.phase = Main;
      s.paramRow = w(0xE3AE);
      s.springSets = w(0xE53E);
      s.gainTab = w(0xE358);
      s.servoSet = w(0xE280);
      s.springCtr = b(0xE530);
      s.springCol = b(0xE538);
      s.followLag = b(0xE53A);
      s.returnPer = b(0xE53C);
      s.tick4ms = b(0xE540);
      s.roadOn = b(0xE360);
      s.roadSel = b(0xE35A);
      s.roadIdx = w(0xE310);
      s.attractSt = b(0xE388);
      // The game going on: its spring running, no request pending
      s.gameReady = (s.mode & 0x04) ? 0xFF : 0x00;
      s.gameReq = 0;
      if (s.gameReady && s.outEngine == 0x01)
        s.outEngine = 0x02;
    }

  private:
    enum Phase : UINT8 { WaitServo, WaitCentres, WaitWheelCtr, Calibrate, CabinetWindow, InitDone, Main };

    void Restart(void)
    {
      s.replySel |= 0x80;
      s.initReply = 0xCF;
      s.out2d &= 0xFE;
      s.status &= ~0x10;
      SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xF9, 0xA8 } });
      TicksZero();
      s.phase = WaitServo;
      s.phaseEntered = 1;
    }

    // -- The NMI ----------------------------------------------------------------------------------------------------

    void NmiBody(UINT8 wheel, UINT8 cockpit)
    {
      ReadCommand();
      WriteReply();
      OutputEngine();
      // (write_servo: out_mode, out_tq to the ports, by the caller)
      // The ADCs, after the outputs: the wheel the next NMI's output uses
      s.wheel = wheel;
      WheelMotion();
      s.cockpit = cockpit;
      Timers();
    }

    void OutputEngine(void)
    {
      UINT8 e = s.outEngine;
      if (e & 0x01) OutQueue();
      else if (e & 0x02) OutSpring();
      else if (e & 0x10) OutSlide();
      else if (e & 0x20) OutShake(0x4A5B, s.shakeC2I, s.shakeReq);
      else if (e & 0x40) OutShake(0x4B8C, s.shakeC6I, s.shakeC6On);
      // else hold: the same pair again
    }

    void OutSpring(void)
    {
      s.outMode = 0xFE;
      UINT8 springOff = UINT8(s.springCtr - 0x80);
      UINT8 idx = UINT8(s.wheel - springOff);
      s.outTq = R(uint16_t(s.servoSet + idx));
      RoadEffects();
    }

    void RoadEffects(void)
    {
      if (s.roadOn != 0xFF)
        return;
      int e = LowestBit(s.roadSel, 1, 7);
      if (e < 0)
        return;
      static const uint16_t tables[8] = { 0, 0x4328, 0x44D6, 0x44FF, 0x45E3, 0x47DB, 0x4863, 0x4863 };
      if (e == 2)
      {
        UINT8 &st = s.road2St;
        if (st == 0)
        {
          s.road2Wait = Random();
          st = 1;
        }
        if (st & 1)
        {
          if (s.road2Wait != 0)
            return;
          st = 2;
          s.roadIdx = 0x25;
        }
        UINT8 v = R(uint16_t(0x44D6 + s.roadIdx));
        s.roadIdx--;
        if (s.roadIdx == 0xFFFF)
          st = 0;
        s.outTq = UINT8(s.outTq + v - 0x80);
        return;
      }
      UINT8 &st = s.roadSt[e];
      if (!(st & 1))
      {
        // The step, drawn at each pass through the table: 1-4 for effects 1, 3, 4
        UINT8 step = (e == 1 || e == 3 || e == 4) ? UINT8((Random() & 3) + 1) : 1;
        st = UINT8((st & ~0xF0) | 1 | (step << 4));
      }
      UINT8 v = R(uint16_t(tables[e] + s.roadIdx));
      s.roadIdx = uint16_t(s.roadIdx + (st >> 4));
      if (e == 7)
        v = UINT8(-v);
      if (v != 0)
        s.outTq = UINT8(s.outTq + v - 0x80);
      else
      {
        s.roadIdx = 0;
        st &= ~1;
      }
    }

    void Timers(void)
    {
      s.torqueTmr--;
      if (++s.tick2nmi == 2)
      {
        s.tick2nmi = 0;
        s.tick4ms++;
      }
      if (++s.tickANmi == 125)
      {
        s.tickANmi = 0;
        s.tickA250++;
      }
      if (++s.tickBNmi == 125)
      {
        s.tickBNmi = 0;
        s.tickB250++;
      }
      if (--s.motionTick == 0)
      {
        s.motionDue = 0xFF;
        s.motionTick = 50;
      }
      s.road2Wait--;
      if (++s.tickNmi == 50)
      {
        s.tickNmi = 0;
        s.tick100ms++;
      }
      if (++s.tick5nmi == 5)
      {
        s.tick5nmi = 0;
        s.tick10ms++;
      }
    }

    // -- The main code -----------------------------------------------------------------------------------------------

    void MainPass(void)
    {
      switch (s.phase)
      {
      case WaitServo:
        // A fixed 8 s, the servo board ready (its alarms read all well)
        if (s.tick100ms >= 80)
          Enter(WaitCentres);
        return;
      case WaitCentres:
        if (!s.phaseEntered)
        {
          s.status |= 0x02;
          s.initReply = 0xCE;
          TicksZero();
          s.phaseEntered = 1;
        }
        if (s.tick100ms >= 10)
        {
          Enter(WaitWheelCtr);
          return;
        }
        switch (TakeStartupCommand(0x60, 0x9F, &s.cockpitCtr))
        {
        case TestMode: EnterTest(); return;
        case InRange: Enter(WaitWheelCtr); return;
        default: return;
        }
      case WaitWheelCtr:
        if (!s.phaseEntered)
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          s.initReply = 0xCD;
          TicksZero();
          s.phaseEntered = 1;
        }
        if (s.tick100ms >= 10)
        {
          Enter(Calibrate);
          return;
        }
        switch (TakeStartupCommand(0x70, 0x8F, &s.centre))
        {
        case TestMode: EnterTest(); return;
        case InRange: Enter(Calibrate); return;
        default: return;
        }
      case Calibrate:
        if (!s.phaseEntered)
        {
          TicksZero();
          s.phaseEntered = 1;
          s.calDir = 0;
          if (s.wheel < s.centre)
          {
            SetQueue({ { 0xFE, 0x60 } });
            s.calDir = 1;
          }
          else if (s.wheel > s.centre)
          {
            SetQueue({ { 0xFE, 0xA0 } });
            s.calDir = 2;
          }
        }
        if (s.calDir != 0 && TakeStartupCommand(0, 0, nullptr) == TestMode)
        {
          EnterTest();
          return;
        }
        // Pushed until the wheel reaches its centre (Supermodel reads it at 80 before the board's CC)
        if (s.calDir == 0 || (s.calDir == 1 && s.wheel >= s.centre) || (s.calDir == 2 && s.wheel <= s.centre))
        {
          s.outEngine = 1;
          SetQueue({ { 0xF8, 0xF9 }, { 0xFE, 0x80 }, { 0xFC, 0xFC }, { 0xFD, 0x66 }, { 0xF8, 0xFA }, { 0xFA, 0xF8 },
                     { 0xFA, 0xFE }, { 0xFA, 0xFF }, { 0xFB, 0x05 } });
          s.initReply = 0xCC;
          TicksZero();
          s.phase = CabinetWindow;
          s.phaseEntered = 1;
        }
        return;
      case CabinetWindow:
        if (s.cmdNew == 0xFF)
        {
          s.cmdNew = 0;
          UINT8 c = s.command;
          if (c == 0xCA)
          {
            EnterTest();
            return;
          }
          if (c == 0xB0)
          {
            s.cabinet = 0x00;
            s.paramRow = 0x1C48;
            s.springSets = 0x1CA8;
          }
          else if (c == 0xB1)
          {
            s.cabinet = 0xFF;
            s.paramRow = 0x1C08;
            s.springSets = 0x1C88;
          }
        }
        if (s.tick100ms >= 10)
        {
          SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFF, 0xFF } });
          s.tickANmi = s.tickA250 = 0;
          s.phase = InitDone;
        }
        return;
      case InitDone:
        if (s.queue.len == 0)
        {
          s.replySel &= 0x7F;
          s.phase = Main;
        }
        return;
      default:
        MainLoop();
        return;
      }
    }

    void EnterTest(void)
    {
      s.mode = 0x20;
      s.caState = 0;
      s.testCmd = 0;
      s.replySel &= 0x7F;
      s.phase = Main;
    }

    void MainLoop(void)
    {
      DecodeCommand();
      if (s.mode & 0x01) ModeC1();
      else if (s.mode & 0x02) ModeC4();
      else if (s.mode & 0x04) ModeC7();
      else if (s.mode & 0x08) ModeC8();
      else if (s.mode & 0x10) ModeC9();
      else if (s.mode & 0x20) ModeCA();
      else if (s.mode & 0x40) ModeC6();
      else if (s.mode & 0x80) ModeCB();
    }

    void LoadServoSet(void)
    {
      s.servoSet = R16(uint16_t(s.paramRow + 4 * s.springCol));
    }

    void SpringSet(UINT8 k)
    {
      uint16_t ent = uint16_t(s.springSets + 4 * k);
      s.paramRow = R16(ent);
      s.followLag = R(uint16_t(ent + 2));
      s.returnPer = R(uint16_t(ent + 3));
      s.tick4ms = 0;
    }

    void DecodeCommand(void)
    {
      if (s.cmdNew != 0xFF)
        return;
      s.cmdNew = 0;
      s.replySel &= 0x7F;
      UINT8 c = s.command;
      switch (c >> 4)
      {
      case 0x0:
        if (c < 0x08)
        {
          s.roadOn = 0;
          s.roadSel = Bit(c);
          s.gameReq = 0x80;
        }
        else
        {
          // A spring set: its row of tables, the centre's lag and return period
          SpringSet(c & 7);
          LoadServoSet();
          s.gameReq = 0x01;
          s.springSt = 0x01;
        }
        s.kickOn = 0;
        return;
      case 0x1:
        s.springCol = c & 7;
        LoadServoSet();
        s.gameReq = 0x01;
        s.springSt = 0x01;
        return;
      case 0x2:
        s.gainN = c & 0x0F;
        s.gameReq = 0x40;
        s.gainSt = 0;
        return;
      case 0x3:
        s.vibPtr = uint16_t(0x1BC8 + 4 * (c & 0x0F));
        s.gameReq = 0x02;
        s.vibSt = 0;
        return;
      case 0x4:
        s.slideDir = (c & 8) ? 0x00 : 0xFF;
        s.slideBit = Bit(c);
        s.slideSt = 0x01;
        s.gameReq = 0x04;
        return;
      case 0x5:
      case 0x6:
        s.torqueN = c & 0x0F;
        if ((c >> 4) == 0x5)
          s.torqueTab = s.cabinet == 0xFF ? 0x4278 : 0x4268;
        else
          s.torqueTab = s.cabinet == 0xFF ? 0x4298 : 0x4288;
        s.gameReq = 0x10;
        s.torqueSt = 0;
        return;
      case 0x7:
        s.strengthBit = Bit(c);
        s.strengthNew = 0xFF;
        return;
      case 0x8:
        s.testCmd = Bit(c);
        return;
      case 0x9:
      case 0xA:
        s.joltDelay = R(uint16_t(((c >> 4) == 0x9 ? 0x4D21 : 0x4D51) + 3 * (c & 0x0F)));
        s.gameReq = 0x08;
        s.joltSt = 0;
        return;
      case 0xC:
        switch (c)
        {
        case 0xC1:
          s.mode = 0x01;
          s.attractSt = s.patternSt = s.shakeC2St = s.shakeReq = 0;
          s.c8Count = 0;
          return;
        case 0xC2:
          s.shakeReq = 0xFF;
          s.shakeC2St = 0;
          return;
        case 0xC4: s.mode = 0x02; s.c4State = 0; s.strengthNew = 0; return;
        case 0xC6: s.mode = 0x40; s.c6State = 0; return;
        case 0xC7: s.mode = 0x04; s.gameReady = 0; s.gameReq = 0; s.gameInitQueued = 0; return;
        case 0xC8: s.mode = 0x08; s.c8State = 0; s.c8Count = 0; return;
        case 0xC9: s.mode = 0x10; s.c9State = 0; return;
        case 0xCA: s.mode = 0x20; s.caState = 0; s.testCmd = 0; return;
        case 0xCB: s.mode = 0x80; s.cbState = 0; return;
        default: return;
        }
      case 0xD:
        s.replySel = Bit(c);
        return;
      case 0xF:
        s.initReply = c;
        s.replySel |= 0x80;
        return;
      default:
        return;
      }
    }

    void InitQueueC1(void)
    {
      SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFB, 0xFF }, { 0xFF, 0xFF } });
      s.outEngine = 1;
    }

    void ModeC1(void)
    {
      if (!(s.attractSt & 0x02))
      {
        if (!(s.attractSt & 0x01))
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          InitQueueC1();
          s.tickANmi = s.tickA250 = s.tickBNmi = s.tickB250 = 0;
          s.attractSt = 0x01;
        }
        if (s.cabinet != 0xFF && s.tickA250 >= 120)
          s.attractSt = 0x02;
      }
      if (s.attractSt & 0x02)
      {
        // The wheel brought back to its centre every 30 s (cabinet B0)
        if (s.shakeReq != 0xFF)
        {
          if (s.wheel < UINT8(s.centre - 2))
          {
            if (!(s.attractSt & 0x04))
            {
              SetQueue({ { 0xFE, 0x60 } });
              s.outEngine = 1;
              s.attractSt = 0x06;
            }
          }
          else if (s.wheel >= UINT8(s.centre + 3))
          {
            if (!(s.attractSt & 0x08))
            {
              SetQueue({ { 0xFE, 0xA0 } });
              s.outEngine = 1;
              s.attractSt = 0x0A;
            }
          }
          else
          {
            SetQueue({ { 0xFF, 0x80 } });
            s.outEngine = 1;
            s.attractSt = 0x01;
            s.tickANmi = s.tickA250 = 0;
          }
        }
      }
      // The displays' pattern (random every 70 s, which moves the random sequence)
      if (s.patternSt == 0)
      {
        s.tickBNmi = s.tickB250 = 0;
        s.patternSt = 1;
      }
      if (s.patternSt == 1 && s.tickB250 >= 240)
      {
        Random();
        s.tickBNmi = s.tickB250 = 0;
        s.patternSt = 2;
      }
      if (s.patternSt == 2 && s.tickB250 >= 40)
      {
        s.tickBNmi = s.tickB250 = 0;
        s.patternSt = 1;
      }
      // The shake C2
      if (s.queue.len != 0)
        return;
      if (!(s.shakeC2St & 1))
      {
        if (s.shakeReq == 0xFF)
        {
          s.shakeC2I = 0;
          s.outEngine = 0x20;
          s.shakeC2St = 1;
        }
      }
      else if (s.shakeReq == 0)
      {
        s.attractSt = 0x01;
        s.tickANmi = s.tickA250 = 0;
        s.outEngine = 0;
        s.shakeC2St = 0;
      }
    }

    void ModeC4(void)
    {
      if (!(s.c4State & 1))
      {
        s.out2d |= 0x01;
        s.status |= 0x10;
        InitQueueC1();
        s.c4State = 1;
      }
      if (s.queue.len != 0 || s.strengthNew != 0xFF)
        return;
      s.strengthNew = 0;
      if (s.lock7x)
        return;
      static const uint16_t tabs[6] = { 0x4318, 0x4308, 0x42E8, 0x42C8, 0x42B8, 0x42A8 };
      int b = LowestBit(s.strengthBit, 0, 7);
      if (b >= 6 || b < 0)
      {
        s.strengthBit = 1;
        s.gainTab = 0x4318;
      }
      else
        s.gainTab = tabs[b];
    }

    void ModeC6(void)
    {
      if (s.c6State == 0)
      {
        s.out2d |= 0x01;
        s.status |= 0x10;
        InitQueueC1();
        s.c6State = 1;
      }
      if (s.c6State == 1 && s.queue.len == 0)
      {
        s.outEngine = 0x40;
        s.shakeC6I = 0;
        s.shakeC6On = 0xFF;
        s.c6State = 2;
      }
      if (s.c6State == 2 && s.shakeC6On == 0)
      {
        s.c6State = 4;
        s.outEngine = 0;
      }
    }

    void ModeC7(void)
    {
      if (s.gameReady != 0xFF)
      {
        // The game's set-up, blocking until sent
        if (!s.gameInitQueued)
        {
          s.out2d &= 0xFE;
          s.status &= ~0x10;
          s.springCtr = s.centre;
          s.roadOn = 0;
          s.kickOn = 0;
          s.springCol = 0;
          SpringSet(0);
          LoadServoSet();
          SetQueue({ { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFB, R(uint16_t(s.gainTab + 15)) }, { 0xFE, 0x80 },
                     { 0xFD, 0x00 }, { 0xFC, 0x00 } });
          s.outEngine = 1;
          s.gameInitQueued = 1;
        }
        if (s.queue.len != 0)
          return;
        s.gameInitQueued = 0;
        s.outEngine = 2;
        s.gameReady = 0xFF;
        return;
      }
      if (s.gameReq & 0x01) GameSpring();
      if (s.gameReq & 0x02) GameVibration();
      if (s.gameReq & 0x04) GameSlide();
      if (s.gameReq & 0x08) GameJolt();
      if (s.gameReq & 0x10) GameTorque();
      if (s.gameReq & 0x40) GameGain();
      if (s.gameReq & 0x80) GameRoad();
      MovingCentre();
    }

    void GameSpring(void)
    {
      if (s.springSt == 0)
        s.springSt = 1;
      if (s.springSt == 1)
      {
        SetQueue({ { 0xFD, 0x00 }, { 0xFC, 0x00 } });
        s.outEngine = 1;
        s.springSt = 2;
      }
      else if (s.springSt == 2 && s.queue.len == 0)
      {
        s.outEngine = 2;
        s.gameReq = 0;
      }
    }

    void GameVibration(void)
    {
      if (s.vibSt == 0)
      {
        SetQueue({ { 0xFC, R(s.vibPtr) }, { 0xFC, R(uint16_t(s.vibPtr + 1)) }, { 0xFC, R(uint16_t(s.vibPtr + 2)) },
                   { 0xFC, R(uint16_t(s.vibPtr + 3)) } });
        s.outEngineS = s.outEngine;
        s.outEngine = 1;
        s.vibSt = 1;
      }
      else if (s.queue.len == 0)
      {
        s.outEngine = s.outEngineS;
        s.gameReq = 0;
      }
    }

    void GameGain(void)
    {
      if (s.gainSt == 0)
      {
        SetQueue({ { 0xFB, R(uint16_t(s.gainTab + s.gainN)) } });
        s.outEngineS = s.outEngine;
        s.outEngine = 1;
        s.gainSt = 1;
      }
      else if (s.queue.len == 0)
      {
        s.gameReq = 0;
        s.outEngine = s.outEngineS;
      }
    }

    void GameTorque(void)
    {
      uint16_t t = s.torqueTab;
      if (s.torqueSt == 0)
      {
        SetQueue({ { 0xFD, 0x00 }, { 0xFE, R(uint16_t(t + s.torqueN)) } });
        s.outEngine = 1;
        s.torqueSt = 1;
      }
      else if (s.torqueSt == 1)
      {
        if (s.queue.len != 0)
          return;
        if (s.torqueN < 8)
        {
          s.gameReq = 0;
          s.outEngine = 0;
        }
        else
        {
          s.torqueTmr = 100;
          s.torqueSt = 2;
        }
      }
      else if (s.torqueSt == 2 && s.torqueTmr == 0)
      {
        SetQueue({ { 0xFD, 0x00 }, { 0xFE, R(uint16_t(t + 7)) } });
        s.outEngine = 1;
        s.torqueSt = 1;
      }
    }

    void GameJolt(void)
    {
      if (s.joltSt == 0)
      {
        SetQueue({ { 0xFF, 0xFF } });
        s.outEngine = 1;
        s.tick5nmi = 0;
        s.tick10ms = 0;
        s.joltSt = 1;
      }
      else if (s.tick10ms >= s.joltDelay)
      {
        // The kick that should follow never plays (the program reads its address one byte too far)
        s.kickOn = 0;
        s.gameReq = 0x01;
        s.springSt = 0x01;
      }
    }

    void GameRoad(void)
    {
      int e = LowestBit(s.roadSel, 1, 7);
      if (e < 0)
        return;
      if (e == 2)
        s.road2St = 0;
      else
        s.roadSt[e] = 0;
      s.roadIdx = 0;
      s.roadOn = 0xFF;
      s.gameReq = 0;
    }

    void GameSlide(void)
    {
      static const uint16_t tabs[8] = { 0x41B4, 0x4100, 0x404C, 0x3F98, 0x3EE4, 0x3E30, 0x3D7C, 0x3CC8 };
      switch (s.slideSt)
      {
      case 0x01:
        SetQueue({ { 0xFC, 0xFC }, { 0xFD, 0x10 } });
        s.outEngine = 1;
        s.slideSt = 0x02;
        break;
      case 0x02:
        if (s.queue.len != 0)
          break;
        s.slideTab = tabs[LowestBit(s.slideBit, 0, 7)];
        s.slidePer = 2;
        s.slideCnt = 2;
        s.slideHalf = 0;
        s.slideIdx = 0;
        s.outEngine = 0x10;
        s.slideSt = 0x04;
        break;
      case 0x04:
        if (s.slideHalf == 1)
        {
          SetQueue({ { 0xFD, 0x21 } });
          s.outEngine = 1;
          s.slideSt = 0x08;
        }
        break;
      case 0x08:
        if (s.queue.len != 0)
          break;
        s.slidePer = 1;
        s.slideCnt = 1;
        s.outEngine = 0x10;
        s.slideSt = 0x10;
        break;
      case 0x10:
        if (s.slideHalf == 2)
        {
          SetQueue({ { 0xFE, 0x80 }, { 0xFB, R(uint16_t(s.gainTab + 7)) } });
          s.outEngine = 1;
          s.slideSt = 0x20;
        }
        break;
      case 0x20:
        if (s.queue.len != 0)
          break;
        SpringSet(0);
        LoadServoSet();
        s.gameReq = 0x01;
        s.springSt = 0x01;
        break;
      default:
        break;
      }
    }

    void MovingCentre(void)
    {
      UINT8 st = s.status & 0xC0;
      UINT8 h = s.springCtr;
      if (st == 0x40)
      {
        // Going right: the centre follow_lag behind the wheel
        if (s.wheel >= h && UINT8(s.wheel - h) > s.followLag)
          s.springCtr = UINT8(s.wheel - s.followLag);
      }
      else if (st == 0x80)
      {
        if (s.wheel < h && UINT8(h - s.wheel) > s.followLag)
          s.springCtr = UINT8(s.wheel + s.followLag);
      }
      if (s.springCtr != s.centre && s.tick4ms >= s.returnPer)
      {
        s.tick4ms = 0;
        s.springCtr = UINT8(s.springCtr + (s.centre > s.springCtr ? 1 : -1));
      }
    }

    void ModeC8(void)
    {
      if (!(s.c8State & 1))
      {
        s.out2d &= 0xFE;
        s.status &= ~0x10;
        SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFF, 0xFF } });
        s.outEngine = 1;
        s.c8State = 1;
      }
      if (s.queue.len != 0 || (s.c8State & 4))
        return;
      if (s.cockpit >= UINT8(s.cockpitCtr - 2) && s.cockpit <= UINT8(s.cockpitCtr + 2))
      {
        if (++s.c8Count >= 50)
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          s.c8State |= 4;
        }
      }
      else
        s.c8Count = 0;
    }

    void ModeC9(void)
    {
      if (!(s.c9State & 1))
      {
        SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFF, 0xFF } });
        s.outEngine = 1;
        s.c9State |= 1;
      }
      if (s.queue.len == 0 && !(s.c9State & 2))
      {
        s.out2d |= 0x01;
        s.status |= 0x10;
        s.c9State |= 2;
      }
    }

    void ModeCA(void)
    {
      if (!(s.caState & 1))
      {
        InitQueueC1();
        s.caState = 1;
      }
      if (!(s.caState & 2))
      {
        if (s.queue.len != 0)
          return;
        s.out2d &= 0xFE;
        s.status &= ~0x10;
        s.caState |= 2;
      }
      int b = LowestBit(s.testCmd, 0, 7);
      switch (b)
      {
      case 0: SetQueue({ { 0xFE, 0x80 } }); break;
      case 1: SetQueue({ { 0xFE, 0x69 } }); break;
      case 2: SetQueue({ { 0xFE, 0x97 } }); break;
      case 3: s.out2d |= 0x01; s.status |= 0x10; break;
      case 4: s.out2d &= 0xFE; s.status &= ~0x10; break;
      case 5:
        if (s.testBusy)
        {
          if (s.queue.len == 0)
          {
            s.testBusy = 0;
            s.testCmd = 0;
          }
          return;
        }
        if (s.wheel >= 0x70 && s.wheel <= 0x8F)
        {
          s.centre = s.wheel;
          SetQueue({ { 0xF8, 0xF9 }, { 0xFE, 0x80 }, { 0xF8, 0xFA } });
          s.testBusy = 0xFF;
          return;
        }
        break;
      case 6:
        if (s.cockpit >= 0x60 && s.cockpit <= 0x9F)
          s.cockpitCtr = s.cockpit;
        break;
      default:
        return;
      }
      s.testCmd = 0;
    }

    void ModeCB(void)
    {
      if (!(s.cbState & 1))
      {
        InitQueueC1();
        s.cbState = 1;
      }
      if (s.queue.len == 0)
      {
        // Start again from the centres' windows (the attract state is not cleared: a quirk kept)
        s.mode = 0x01;
        s.replySel |= 0x80;
        Enter(WaitCentres);
      }
    }
  };

  /*
   * EPR-19338A, the drive board of Scud Race, Dirt Devils, Emergency Call Ambulance and Le Mans 24's TWIN-LEMANS
   * cabinet (CRC32 C9FAC464). Its spring is a torque by the wheel's distance from the centre read in a table of 256
   * (a row of 8 tables, the row by the DIP switches and the cabinet B0/B1, the table by the game's 1x), sent as FE at
   * every NMI, plus the road effects. Start-up: CF 8 s, CE 1 s (the cockpit's centre), CD 1 s (the wheel's; the
   * wheel then pushed back to it), CC 1 s (the cabinet), then the same modes as EPR-20985's.
   *
   * EPR-20512, Sega Rally 2's (CRC32 CF64350D), is the same program with the game's commands cut down to the torque
   * (00-3F one way, 40-7F the other, held until the next) and its tables elsewhere: the layout below. EPR-19338, Scud
   * Race Plus's (CRC32 DBF88DE6), is EPR-19338A's earlier version: its tables A9 bytes lower, the cabinet by the DIP
   * switches only (no B0/B1, CC 0.1 s), Ax a power slide, the re-centring of C1 whatever the cabinet, stronger pushes
   * and a bug in wheel_motion (BFF's docs/EPR-19338-ScudRacePlus-DriveBoard).
   */
  struct ScudRaceLayout
  {
    const char *block;          // the save state's block
    double firstNmi;            // Z80 cycles to the first NMI
    uint16_t rowB0, rowB1;      // the parameter rows of the cabinets B0 and B1 (+ 20 x the DIP switches' row)
    bool rowAtReset;            // the DIP switches' row from power-on (else row 0 until a B0/B1)
    uint16_t servoTab, fbCurve; // at power-on
    uint16_t shakeC2, shakeC6;  // the shakes' tables
    UINT8 torqueFd;             // the FD sent before a torque command's FE
    enum Recentre : UINT8 { Never, CabinetB0, Always } recentre;  // C1 brings the wheel back to its centre every 30 s
    UINT8 calPushR, calPushL;   // the start-up's pushes to the centre (FE)
    UINT8 recPushR, recPushL;   // C1's
    UINT8 ccTicks;              // how long CC lasts (100 ms)
    bool ccCommands;            // B0/B1 (and CA) taken during CC
    bool axSlide;               // Ax plays a power slide
    bool c1ZeroesBlink;         // C1's set-up zeroes the blink's timer too
    bool leftFallsIntoRight;    // wheel_motion's bug
    int tableShift;             // EPR-19338A's tables moved by this
  };

  const ScudRaceLayout kEpr19338A = { "ServoProgram19338A", 1823939, 0x1AF9, 0x19F9, true, 0x1BF9, 0x31A9, 0x395C, 0x3A8D,
                                      0x00, ScudRaceLayout::CabinetB0, 0x6C, 0x94, 0x6D, 0x93, 10, true, false, true,
                                      false, 0 };
  const ScudRaceLayout kEpr19338 = { "ServoProgram19338", 1963011, 0x1A50, 0x1950, true, 0x1B50, 0x3100, 0x38B3, 0x39E4,
                                     0x00, ScudRaceLayout::Always, 0x69, 0x97, 0x69, 0x97, 1, false, true, false, true,
                                     -0xA9 };
  const ScudRaceLayout kEpr20512 = { "ServoProgram20512", 1684867, 0x12C2, 0x12C2, false, 0x14C2, 0x2AE2, 0x3295, 0x33C6,
                                     0x11, ScudRaceLayout::Never, 0x6C, 0x94, 0x6D, 0x93, 10, true, false, true, false,
                                     0 };

  struct ScudRaceState : CommonState
  {
    // Timers: E216/E217 (C1's 30 s), E21A/E21B (C1's blink), E215 (effect 2's wait)
    UINT8 tickANmi, tickA250, tickBNmi, tickB250, shockWait;
    // The start-up's push to the centre
    UINT8 pushDir, pushTries;
    // The spring: its table (E280), FD and FC (E282, E283), the row (E3AE), the FB curve (E358)
    uint16_t servoTab, paramRow, fbCurve;
    UINT8 setFd, setFc, centreOff, noStrength;
    // The game
    UINT8 gameInit, gameReq, springStep, vibFlag, gainN, gainFlag, torqueN, torqueFlag, slideStep, slideN;
    uint16_t vibPtr, torqueTab;
    UINT8 roadEff, roadOn, effFlag[8], effStep[8];
    uint16_t effIndex;
    UINT8 kickDelay, kickTab, kickOn, kickStep;
    uint16_t kickIdx;
    UINT8 axFlag, axBits, axStep;
    // The other modes
    UINT8 attractSt, blinkSt, shakeReq, shakeC2St, shakeC6On, c4State, c6State, c8State, c9State, caState, cbState;
    UINT8 strengthReq, strengthNew, testCmd, testBusy;
    uint16_t shakeC2I, shakeC6I;
  };

  class CScudRaceProgram : public CServoProgram<ScudRaceState>
  {
  public:
    explicit CScudRaceProgram(const ScudRaceLayout &layout = kEpr19338A)
      : L(layout)
    {
      m_leftFallsIntoRight = L.leftFallsIntoRight;
    }

    void Reset(const UINT8 *rom, UINT8 dip1, UINT8 dip2) override
    {
      ResetCommon(rom, dip1, dip2);
      s.servoTab = L.servoTab;
      s.fbCurve = L.fbCurve;
      // DIP switches 1 (ON reads 0): bit 5 the cabinet (OFF B1, ON B0), bits 2-4 the row, bit 1 ON 7x ignored
      s.cabinet = (dip1 & 0x20) ? 0xFF : 0x00;
      s.paramRow = L.rowAtReset ? RowOf(s.cabinet) : L.rowB0;
      s.noStrength = (dip1 & 0x02) ? 0x00 : 0xFF;
      Restart();
    }

    double FirstNmiCycles(void) const override
    {
      return L.firstNmi;
    }

    bool Nmi(UINT8 wheel, UINT8 cockpit, UINT8 &mode, UINT8 &value) override
    {
      NmiBody(wheel, cockpit);
      mode = s.outMode;
      value = s.outTq;
      // The main code until the next NMI. A state machine this NMI let go on (its queue sent, its time up) goes on
      // before a command this NMI took is decoded, as most often on the board (the main loop's reads of the queue
      // and of the command)
      if (s.phase == Main)
        ModeHandler();
      for (int pass = 0; pass < 20; pass++)
        MainPass();
      return true;
    }

    void SaveState(CBlockFile *saveState) override
    {
      SaveBlock(saveState, L.block);
    }

    void LoadState(CBlockFile *saveState) override
    {
      LoadBlock(saveState, L.block);
    }

    void ImportRam(const UINT8 *ram) override
    {
      ImportCommon(ram);
      auto b = [ram](uint16_t a) { return RamB(ram, a); };
      auto w = [ram](uint16_t a) { return RamW(ram, a); };
      s.shockWait = b(0xE215);
      s.tickANmi = b(0xE216);
      s.tickA250 = b(0xE217);
      s.tickBNmi = b(0xE21A);
      s.tickB250 = b(0xE21B);
      s.pushTries = b(0xE30A);
      s.servoTab = w(0xE280);
      s.setFd = b(0xE282);
      s.setFc = b(0xE283);
      s.torqueTab = w(0xE284);
      s.paramRow = w(0xE3AE);
      s.fbCurve = w(0xE358);
      s.centreOff = b(0xE304);
      s.noStrength = b(0xE3B0);
      s.gameInit = b(0xE32E);
      s.gameReq = b(0xE338);
      s.springStep = b(0xE33A);
      s.vibPtr = w(0xE33C);
      s.torqueN = b(0xE33E);
      s.slideStep = b(0xE340);
      s.slideTab = w(0xE344);
      s.slideN = b(0xE34A);
      s.slideCnt = b(0xE34C);
      s.slidePer = b(0xE34E);
      s.slideIdx = b(0xE350);
      s.slideDir = b(0xE352);
      s.slideHalf = b(0xE354);
      s.gainN = b(0xE356);
      s.roadEff = b(0xE35A);
      s.roadOn = b(0xE360);
      s.effIndex = w(0xE310);
      static const uint16_t flags[8] = { 0, 0xE362, 0xE366, 0xE368, 0xE36C, 0xE370, 0xE38A, 0xE38E };
      static const uint16_t steps[8] = { 0, 0xE364, 0, 0xE36A, 0xE36E, 0xE372, 0xE38C, 0xE390 };
      for (int n = 1; n < 8; n++)
      {
        s.effFlag[n] = b(flags[n]);
        s.effStep[n] = steps[n] ? b(steps[n]) : 0;
      }
      s.kickDelay = b(0xE374);
      s.kickTab = b(0xE376);
      s.kickOn = b(0xE37A);
      s.kickStep = b(0xE37C);
      s.kickIdx = w(0xE37E);
      s.axFlag = b(0xE3B8);
      s.axStep = b(0xE3BA);
      s.axBits = b(0xE3BC);
      s.torqueFlag = b(0xE3B2);
      s.gainFlag = b(0xE3B4);
      s.vibFlag = b(0xE3B6);
      s.attractSt = b(0xE388);
      s.blinkSt = b(0xE389);
      s.shakeReq = b(0xE328);
      s.shakeC2I = w(0xE3A2);
      s.shakeC2St = b(0xE3A4);
      s.shakeC6I = w(0xE3A6);
      s.shakeC6On = b(0xE3A8);
      s.c6State = b(0xE3AA);
      s.c4State = b(0xE3AC);
      s.c8State = b(0xE330);
      s.c9State = b(0xE386);
      s.caState = b(0xE332);
      s.testCmd = b(0xE334);
      s.testBusy = b(0xE336);
      s.cbState = b(0xE3BE);
      s.strengthReq = b(0xE32A);
      s.strengthNew = b(0xE32C);
      // Where the main code was (not in RAM): the game's set-up being sent, a start-up phase, or the main loop
      s.phase = Main;
      bool held6c = s.queue.len == 0 ? (s.outMode == 0xFE && s.outTq == L.calPushR)
                                     : (s.queue.pairs[0][0] == 0xFE && s.queue.pairs[0][1] == L.calPushR);
      bool held94 = s.queue.len == 0 ? (s.outMode == 0xFE && s.outTq == L.calPushL)
                                     : (s.queue.pairs[0][0] == 0xFE && s.queue.pairs[0][1] == L.calPushL);
      if ((s.mode & 0x04) && s.gameInit != 0xFF && s.outEngine == 1 && s.queue.len > 0)
        s.phase = GameInitFlush;
      else if ((s.replySel & 0x80) && (s.replySel & 0x7F))
      {
        switch (s.initReply)
        {
        case 0xCF: s.phase = WaitServo; break;
        case 0xCE: s.phase = WaitCentres; break;
        case 0xCD:
          if (held6c || held94)
          {
            s.phase = Push;
            s.pushDir = held6c ? 1 : 2;
          }
          else
            s.phase = WaitWheelCtr;
          break;
        case 0xCC:
          if (s.queue.len > 0 && s.queue.pairs[s.queue.len - 1][0] == 0xFF && s.queue.pairs[s.queue.len - 1][1] == 0xFF)
            s.phase = StartFlush;
          else
            s.phase = CabinetWindow;
          break;
        default:
          break;
        }
      }
    }

  protected:
    enum Phase : UINT8 { WaitServo, WaitCentres, WaitWheelCtr, Push, CabinetWindow, StartFlush, GameInitFlush, Main };

    const ScudRaceLayout &L;

    uint16_t RowOf(UINT8 cabinet) const
    {
      return uint16_t((cabinet ? L.rowB1 : L.rowB0) + 0x20 * ((~m_dip1 >> 2) & 7));
    }

    // A table of EPR-19338A where this version has it
    uint16_t T(int a) const
    {
      return uint16_t(a + L.tableShift);
    }

    void Restart(void)
    {
      s.replySel |= 0x80;
      s.initReply = 0xCF;
      s.out2d &= 0xFE;
      s.status &= ~0x10;
      SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xF9, 0x9F } });
      TicksZero();
      s.phase = WaitServo;
      s.phaseEntered = 1;
    }

    // -- The NMI ----------------------------------------------------------------------------------------------------

    void NmiBody(UINT8 wheel, UINT8 cockpit)
    {
      ReadCommand();
      WriteReply();
      OutputEngine();
      // (port 2D's blink and the A-RST pulse of MODE bit 3: not simulated, nothing asks for the A-RST with the servo
      // board all well; out_mode, out_tq to the ports by the caller)
      s.wheel = wheel;
      WheelMotion();
      s.cockpit = cockpit;
      Timers();
    }

    void OutputEngine(void)
    {
      UINT8 e = s.outEngine;
      if (e & 0x01) OutQueue();
      else if (e & 0x02) TorqueByPosition();
      else if (e & 0x08) {}  // a one-sided table: nothing selects it
      else if (e & 0x10) OutSlide();
      else if (e & 0x20) OutShake(L.shakeC2, s.shakeC2I, s.shakeReq);
      else if (e & 0x40) OutShake(L.shakeC6, s.shakeC6I, s.shakeC6On);
      // else hold: the same pair again
    }

    void TorqueByPosition(void)
    {
      s.outMode = 0xFE;
      UINT8 base = R(uint16_t(s.servoTab + UINT8(s.wheel - s.centreOff)));
      s.outTq = base;
      RoadEffects(base);
    }

    void RoadEffects(UINT8 base)
    {
      if (s.roadOn != 0xFF)
      {
        KickAdd();
        return;
      }
      int n = LowestBit(s.roadEff, 1, 7);
      if (n < 0)
        return;
      if (n == 2)
      {
        // A shock after a random wait: a sweep of 38 samples (no zero test: its first, 00, a full scale spike)
        UINT8 &f = s.effFlag[2];
        if (!(f & 3))
        {
          s.shockWait = Random();
          f = 1;
        }
        if (f & 1)
        {
          s.outTq = base;
          if (s.shockWait != 0)
          {
            KickAdd();
            return;
          }
          f = 2;
          s.effIndex = 0x25;
        }
        uint16_t p = uint16_t(T(0x33D7) + s.effIndex);
        s.effIndex--;
        if (s.effIndex == 0xFFFF)
          f = 0;
        s.outTq = UINT8(base + R(p) - 0x80);
        KickAdd();
        return;
      }
      static const uint16_t tables[8] = { 0, 0x3229, 0, 0x3400, 0x34E4, 0x36DC, 0x3764, 0x3764 };
      uint16_t table = T(tables[n]);
      if (!(s.effFlag[n] & 1))
      {
        // The step, drawn at each pass through the table: 1-4 for effects 1, 3, 4
        s.effStep[n] = (n <= 4) ? UINT8((Random() & 3) + 1) : 1;
        s.effFlag[n] |= 1;
      }
      UINT8 v = R(uint16_t(table + s.effIndex));
      s.effIndex = uint16_t(s.effIndex + s.effStep[n]);
      if (n == 7)
        v = UINT8(-v);
      if (v != 0)
        s.outTq = UINT8(base + v - 0x80);
      else
      {
        s.effIndex = 0;
        s.effFlag[n] &= ~1;
      }
      KickAdd();
    }

    void KickAdd(void)
    {
      if (s.kickOn == 0)
        return;
      // The table's address taken as the word at E376: its high byte (E377) is never written, so 003C/003D, where
      // the ROM reads 00: the kick stops at once (the program's bug, kept)
      UINT8 v = R(uint16_t(s.kickTab + s.kickIdx));
      s.kickIdx++;
      if (v == 0)
      {
        s.kickOn = 0;
        return;
      }
      if (!(s.kickOn & 0x80))
        v = UINT8(-v);
      s.outTq = UINT8(s.outTq + v - 0x80);
    }

    void Timers(void)
    {
      if (++s.tickANmi == 0x7D)
      {
        s.tickANmi = 0;
        s.tickA250++;
      }
      if (++s.tickBNmi == 0x7D)
      {
        s.tickBNmi = 0;
        s.tickB250++;
      }
      if (--s.motionTick == 0)
      {
        s.motionTick = 50;
        s.motionDue = 0xFF;
      }
      s.shockWait--;
      if (++s.tickNmi == 50)
      {
        s.tickNmi = 0;
        s.tick100ms++;
      }
      if (++s.tick5nmi == 5)
      {
        s.tick5nmi = 0;
        s.tick10ms++;
      }
    }

    // -- The main code -----------------------------------------------------------------------------------------------

    void MainPass(void)
    {
      switch (s.phase)
      {
      case WaitServo:
        // A fixed 8 s, the servo board ready (its alarms read all well); a command taken stays pending
        if (s.tick100ms >= 0x50)
          Enter(WaitCentres);
        return;
      case WaitCentres:
        if (!s.phaseEntered)
        {
          s.status |= 0x02;
          s.initReply = 0xCE;
          TicksZero();
          s.phaseEntered = 1;
        }
        if (s.tick100ms >= 10)
        {
          Enter(WaitWheelCtr);
          return;
        }
        switch (TakeStartupCommand(0x60, 0x9F, &s.cockpitCtr))
        {
        case TestMode: EnterTest(); return;
        case InRange: Enter(WaitWheelCtr); return;
        default: return;
        }
      case WaitWheelCtr:
        if (!s.phaseEntered)
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          s.initReply = 0xCD;
          TicksZero();
          s.phaseEntered = 1;
        }
        if (s.tick100ms < 10)
        {
          switch (TakeStartupCommand(0x70, 0x8F, &s.centre))
          {
          case TestMode: EnterTest(); return;
          case InRange: break;
          default: return;
          }
        }
        Calibrate();
        return;
      case Push:
        // Again every 5 s (an error after 3 times on the board, the wheel never coming: not simulated)
        if (s.tick100ms >= 0x32)
        {
          if (--s.pushTries == 0)
            s.pushTries = 3;
          TicksZero();
          SetQueue({ { 0xFE, s.pushDir == 1 ? L.calPushR : L.calPushL } });
        }
        if (TakeStartupCommand(0, 0, nullptr) == TestMode)
        {
          EnterTest();
          return;
        }
        if ((s.pushDir == 1 && s.wheel >= s.centre) || (s.pushDir == 2 && s.wheel <= s.centre))
          Ready();
        return;
      case CabinetWindow:
        // The full 1 s, B0 or B1 the cabinet (its row with the DIP switches'), any other byte dropped (EPR-19338:
        // 0.1 s, no command taken)
        if (s.tick100ms >= L.ccTicks)
        {
          SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFF, 0xFF } });
          s.phase = StartFlush;
          return;
        }
        if (L.ccCommands && s.cmdNew == 0xFF)
        {
          s.cmdNew = 0;
          UINT8 c = s.command;
          if (c == 0xCA)
            EnterTest();
          else if (c == 0xB0 || c == 0xB1)
          {
            s.cabinet = (c == 0xB1) ? 0xFF : 0x00;
            s.paramRow = RowOf(s.cabinet);
          }
        }
        return;
      case StartFlush:
        // Until sent, commands taken staying pending
        if (s.queue.len == 0)
        {
          s.replySel &= 0x7F;
          s.phase = Main;
        }
        return;
      case GameInitFlush:
        if (s.queue.len == 0)
        {
          s.outEngine = 2;
          s.gameInit = 0xFF;
          s.phase = Main;
        }
        return;
      default:
        DecodeCommand();
        ModeHandler();
        return;
      }
    }

    // The wheel's centre known: pushed to it if it is not there, then the servo board's set-up
    void Calibrate(void)
    {
      s.centreOff = UINT8(s.centre - 0x80);
      s.pushTries = 3;
      TicksZero();
      if (s.wheel < s.centre)
      {
        s.pushDir = 1;  // left of it: torque to the right
        SetQueue({ { 0xFE, L.calPushR } });
        s.phase = Push;
      }
      else if (s.wheel > s.centre)
      {
        s.pushDir = 2;
        SetQueue({ { 0xFE, L.calPushL } });
        s.phase = Push;
      }
      else
      {
        s.outEngine = 1;
        Ready();
      }
    }

    void Ready(void)
    {
      SetQueue({ { 0xF8, 0xF9 }, { 0xFE, 0x80 }, { 0xFD, 0x66 }, { 0xF8, 0xFA }, { 0xFA, 0xF8 }, { 0xFA, 0x0E },
                 { 0xFA, 0x4F }, { 0xFB, 0x05 } });
      s.initReply = 0xCC;
      TicksZero();
      s.phase = CabinetWindow;
    }

    void EnterTest(void)
    {
      s.mode = 0x20;
      s.caState = 0;
      s.testCmd = 0;
      s.replySel &= 0x7F;
      s.phase = Main;
    }

    void ModeHandler(void)
    {
      if (s.mode & 0x01) ModeC1();
      else if (s.mode & 0x02) ModeC4();
      else if (s.mode & 0x04) ModeC7();
      else if (s.mode & 0x08) ModeC8();
      else if (s.mode & 0x10) ModeC9();
      else if (s.mode & 0x20) ModeCA();
      else if (s.mode & 0x40) ModeC6();
      else if (s.mode & 0x80) ModeCB();
    }

    // A game command abandons the state machine in progress, whatever its step
    void GameCommand(UINT8 req)
    {
      s.gameReq = req;
      s.axFlag = 0;
      s.kickOn = 0;
    }

    virtual void DecodeCommand(void)
    {
      if (s.cmdNew != 0xFF)
        return;
      s.cmdNew = 0;
      s.replySel &= 0x7F;
      UINT8 c = s.command;
      switch (c >> 4)
      {
      case 0x0:
        s.roadOn = 0;
        s.roadEff = Bit(c);
        GameCommand(0x80);
        return;
      case 0x1:
      {
        // The spring's table, FD and FC: entry c&7 of the row
        uint16_t e = uint16_t(s.paramRow + 4 * (c & 7));
        s.servoTab = R16(e);
        s.setFd = R(uint16_t(e + 2));
        s.setFc = R(uint16_t(e + 3));
        GameCommand(0x01);
        s.springStep = 1;
        return;
      }
      case 0x2:
        s.gainN = c & 0x0F;
        GameCommand(0x40);
        s.gainFlag = 0;
        return;
      case 0x3:
        s.vibPtr = uint16_t(T(0x19B9) + 4 * (c & 0x0F));
        GameCommand(0x02);
        s.vibFlag = 0;
        return;
      case 0x4:
        s.slideDir = (c & 8) ? 0x00 : 0xFF;
        s.slideN = Bit(c);
        s.slideStep = 1;
        GameCommand(0x04);
        return;
      case 0x5:
      case 0x6:
        s.torqueN = c & 7;
        s.torqueTab = T(((c >> 4) == 0x5) ? 0x3199 : 0x31A1);
        GameCommand(0x10);
        s.torqueFlag = 0;
        return;
      case 0x7:
        s.strengthReq = Bit(c);
        s.strengthNew = 0xFF;
        return;
      case 0x8:
        s.testCmd = Bit(c);
        return;
      case 0x9:
      {
        uint16_t p = uint16_t(T(0x3C22) + 3 * (c & 0x0F));
        s.kickDelay = R(p);
        s.kickTab = R(uint16_t(p + 2));
        GameCommand(0x08);
        s.kickStep = 0;
        return;
      }
      case 0xA:
        // EPR-19338A: its bits are never read, it only stops the state machine in progress; EPR-19338: a power slide
        GameCommand(0x00);
        s.axBits = Bit(c);
        s.axFlag = 1;
        s.axStep = 0;
        return;
      case 0xC:
        switch (c)
        {
        case 0xC1: s.mode = 0x01; s.attractSt = 0; s.blinkSt = 0; s.shakeC2St = 0; s.shakeReq = 0; return;
        case 0xC2: s.shakeReq = 0xFF; s.shakeC2St = 0; return;
        case 0xC4: s.mode = 0x02; s.c4State = 0; s.strengthNew = 0; return;
        case 0xC6: s.mode = 0x40; s.c6State = 0; return;
        case 0xC7: s.mode = 0x04; s.gameInit = 0; s.gameReq = 0; s.axFlag = 0; return;
        case 0xC8: s.mode = 0x08; s.c8State = 0; return;
        case 0xC9: s.mode = 0x10; s.c9State = 0; return;
        case 0xCA: EnterTest(); return;
        case 0xCB: s.mode = 0x80; s.cbState = 0; return;
        default: return;
        }
      case 0xD:
        s.replySel = Bit(c);
        return;
      case 0xF:
        s.initReply = c;
        s.replySel |= 0x80;
        return;
      default:
        return;
      }
    }

    void Q6(void)
    {
      SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFB, 0xFF }, { 0xFF, 0xFF } });
      s.outEngine = 1;
    }

    void Q5(void)
    {
      SetQueue({ { 0xFE, 0x80 }, { 0xFD, 0x00 }, { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFF, 0xFF } });
      s.outEngine = 1;
    }

    void ModeC1(void)
    {
      if (!(s.attractSt & 0x02))
      {
        if (!(s.attractSt & 0x01))
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          Q6();
          s.tickANmi = s.tickA250 = 0;
          if (L.c1ZeroesBlink)
            s.tickBNmi = s.tickB250 = 0;
          s.attractSt = 0x01;
        }
        bool recentres = L.recentre == ScudRaceLayout::Always || (L.recentre == ScudRaceLayout::CabinetB0 && s.cabinet != 0xFF);
        if (!recentres || s.tickA250 < 0x78)
        {
          C1Blink();
          C1Shake();
          return;
        }
        s.attractSt = 0x02;
      }
      // The wheel brought back to its centre every 30 s (cabinet B0)
      if (s.shakeReq != 0xFF)
      {
        if (s.wheel < UINT8(s.centre - 2))
        {
          if (!(s.attractSt & 0x04))
          {
            SetQueue({ { 0xFE, L.recPushR } });
            s.outEngine = 1;
            s.attractSt = 0x06;
          }
        }
        else if (s.wheel >= UINT8(s.centre + 3))
        {
          if (!(s.attractSt & 0x08))
          {
            SetQueue({ { 0xFE, L.recPushL } });
            s.outEngine = 1;
            s.attractSt = 0x0A;
          }
        }
        else
        {
          SetQueue({ { 0xFF, 0x80 } });
          s.outEngine = 1;
          s.attractSt = 0x01;
          s.tickANmi = s.tickA250 = 0;
        }
      }
      C1Blink();
      C1Shake();
    }

    // The displays' blink: a random pattern every 70 s, which moves the random sequence
    void C1Blink(void)
    {
      if (!(s.blinkSt & 0x02))
      {
        if (!(s.blinkSt & 0x01))
        {
          s.tickBNmi = s.tickB250 = 0;
          s.blinkSt = 0x01;
        }
        if (s.tickB250 >= 0xF0)
        {
          Random();
          s.tickBNmi = s.tickB250 = 0;
          s.blinkSt = 0x02;
        }
      }
      if ((s.blinkSt & 0x02) && s.tickB250 >= 0x28)
      {
        s.tickBNmi = s.tickB250 = 0;
        s.blinkSt = 0x01;
      }
    }

    // The shake C2 (301 samples)
    void C1Shake(void)
    {
      if (s.queue.len != 0)
        return;
      if (!(s.shakeC2St & 1))
      {
        if (s.shakeReq != 0xFF)
          return;
        s.shakeC2I = 0;
        s.outEngine = 0x20;
        s.shakeC2St = 1;
        return;
      }
      if (s.shakeReq != 0)
        return;
      s.attractSt = 0x01;
      s.tickANmi = s.tickA250 = 0;
      s.outEngine = 0;
      s.shakeC2St = 0;
    }

    void ModeC4(void)
    {
      if (!(s.c4State & 1))
      {
        s.out2d |= 0x01;
        s.status |= 0x10;
        Q6();
        s.c4State = 1;
      }
      if (s.queue.len != 0 || s.strengthNew != 0xFF)
        return;
      s.strengthNew = 0;
      if (s.noStrength == 0xFF)
        return;
      // The FB curve of 7x (kept until another, CB included)
      static const uint16_t curves[6] = { 0x3219, 0x3209, 0x31E9, 0x31C9, 0x31B9, 0x31A9 };
      int b = LowestBit(s.strengthReq, 0, 7);
      if (b < 0 || b >= 6)
      {
        s.strengthReq = 1;
        s.fbCurve = T(0x3219);
      }
      else
        s.fbCurve = T(curves[b]);
    }

    void ModeC6(void)
    {
      if (s.c6State & 4)
        return;
      if (!(s.c6State & 2))
      {
        if (!(s.c6State & 1))
        {
          s.out2d |= 0x01;
          s.status |= 0x10;
          Q6();
          s.c6State = 1;
        }
        if (s.queue.len != 0)
          return;
        // The shake C6 (401 samples)
        s.outEngine = 0x40;
        s.shakeC6I = 0;
        s.shakeC6On = 0xFF;
        s.c6State = 2;
      }
      if (s.shakeC6On == 0xFF)
        return;
      s.c6State = 4;
      s.outEngine = 0;
    }

    void ModeC7(void)
    {
      if (s.gameInit != 0xFF)
      {
        // The game's set-up: entry 0 of the row, then sent before anything else (no command decoded meanwhile)
        s.out2d &= 0xFE;
        s.status &= ~0x10;
        s.roadOn = 0;
        s.kickOn = 0;
        s.servoTab = R16(s.paramRow);
        s.setFd = R(uint16_t(s.paramRow + 2));
        s.setFc = R(uint16_t(s.paramRow + 3));
        SetQueue({ { 0xFC, 0x0A }, { 0xFC, 0x0B }, { 0xFB, R(uint16_t(s.fbCurve + 15)) }, { 0xFE, 0x80 },
                   { 0xFD, s.setFd }, { 0xFC, s.setFc } });
        s.outEngine = 1;
        s.phase = GameInitFlush;
        return;
      }
      if (s.gameReq & 0x01) GameSpring();
      if (s.gameReq & 0x02) GameVibration();
      if (s.gameReq & 0x04) GameSlide();
      if (s.gameReq & 0x08) GameKick();
      if (s.gameReq & 0x10) GameTorque();
      if (s.gameReq & 0x40) GameGain();
      if (s.gameReq & 0x80) GameRoad();
      if (L.axSlide && (s.axFlag & 1)) GameAxSlide();
    }

    // EPR-19338's Ax: the half sine n of 4x from the wheel's side (away from the centre first), a sample every 2 NMIs,
    // swinging with no end until a command sets the output
    void GameAxSlide(void)
    {
      if (!(s.axStep & 1))
      {
        SetQueue({ { 0xFC, 0xFC }, { 0xFD, 0x10 } });
        s.outEngine = 1;
        s.axStep = 1;
      }
      if (s.queue.len != 0)
        return;
      int n = LowestBit(s.axBits, 0, 7);
      if (n < 0)
      {
        s.axBits = 1;
        n = 0;
      }
      s.slideTab = uint16_t(T(0x30E5) - 0xB4 * n);
      s.slideDir = (s.wheel >= s.centre) ? 0xFF : 0x00;
      s.slidePer = 2;
      s.slideCnt = 2;
      s.slideHalf = 0;
      s.slideIdx = 0;
      s.outEngine = 0x10;
      s.gameReq = 0;
      s.axFlag = 0;
    }

    void GameSpring(void)
    {
      if (!(s.springStep & 3))
        s.springStep = 1;
      if (s.springStep & 1)
      {
        SetQueue({ { 0xFE, 0x80 }, { 0xFD, s.setFd }, { 0xFC, s.setFc } });
        s.outEngine = 1;
        s.springStep = 2;
      }
      if (s.queue.len != 0)
        return;
      s.outEngine = 2;
      s.gameReq = 0;
      s.axFlag = 0;
    }

    void GameVibration(void)
    {
      if (!(s.vibFlag & 1))
      {
        SetQueue({ { 0xFC, R(s.vibPtr) }, { 0xFC, R(uint16_t(s.vibPtr + 1)) }, { 0xFC, R(uint16_t(s.vibPtr + 2)) },
                   { 0xFC, R(uint16_t(s.vibPtr + 3)) } });
        s.outEngineS = s.outEngine;
        s.outEngine = 1;
        s.vibFlag = 1;
      }
      if (s.queue.len != 0)
        return;
      s.outEngine = s.outEngineS;
      s.gameReq = 0;
      s.axFlag = 0;
    }

    void GameGain(void)
    {
      if (!(s.gainFlag & 1))
      {
        SetQueue({ { 0xFB, R(uint16_t(s.fbCurve + s.gainN)) } });
        s.outEngineS = s.outEngine;
        s.outEngine = 1;
        s.gainFlag = 1;
      }
      if (s.queue.len != 0)
        return;
      s.gameReq = 0;
      s.axFlag = 0;
      s.outEngine = s.outEngineS;
    }

    void GameTorque(void)
    {
      if (!(s.torqueFlag & 1))
      {
        SetQueue({ { 0xFD, L.torqueFd }, { 0xFE, R(uint16_t(s.torqueTab + s.torqueN)) } });
        s.outEngine = 1;
        s.torqueFlag = 1;
      }
      if (s.queue.len != 0)
        return;
      // The FE torque held, no spring until a 1x
      s.gameReq = 0;
      s.axFlag = 0;
      s.outEngine = 0;
    }

    void GameKick(void)
    {
      if (!(s.kickStep & 1))
      {
        SetQueue({ { 0xFF, 0xFF } });
        s.outEngine = 1;
        s.tick5nmi = 0;
        s.tick10ms = 0;
        s.kickStep |= 1;
      }
      if (s.tick10ms < s.kickDelay)
        return;
      s.kickOn = (s.wheel >= s.centre) ? 0x80 : 0x01;
      s.kickIdx = 0;
      s.gameReq = 0x01;
      s.springStep = 1;
      s.axFlag = 0;
    }

    void GameRoad(void)
    {
      int n = LowestBit(s.roadEff, 1, 7);
      if (n < 0)
        return;
      s.effFlag[n] = 0;
      s.gameReq = 0;
      s.axFlag = 0;
      s.effIndex = 0;
      s.roadOn = 0xFF;
    }

    void GameSlide(void)
    {
      // Steps going on from one to the next within a pass while their condition holds
      if (s.slideStep == 0)
        s.slideStep = 0x01;
      if (s.slideStep & 0x01)
      {
        SetQueue({ { 0xFC, 0xFC }, { 0xFD, 0x10 } });
        s.outEngine = 1;
        s.slideStep = 0x02;
      }
      if (s.slideStep & 0x02)
      {
        if (s.queue.len != 0)
          return;
        s.slideTab = uint16_t(T(0x30E5) - 0xB4 * LowestBit(s.slideN, 0, 7));
        s.slidePer = 2;
        s.slideCnt = 2;
        s.slideHalf = 0;
        s.slideIdx = 0;
        s.outEngine = 0x10;
        s.slideStep = 0x04;
      }
      if (s.slideStep & 0x04)
      {
        if (s.slideHalf != 1)
          return;
        SetQueue({ { 0xFD, 0x21 } });
        s.outEngine = 1;
        s.slideStep = 0x08;
      }
      if (s.slideStep & 0x08)
      {
        if (s.queue.len != 0)
          return;
        s.slidePer = 1;
        s.slideCnt = 1;
        s.outEngine = 0x10;
        s.slideStep = 0x10;
      }
      if (s.slideStep & 0x10)
      {
        if (s.slideHalf != 2)
          return;
        SetQueue({ { 0xFE, 0x80 }, { 0xFB, R(uint16_t(s.fbCurve + 7)) } });
        s.outEngine = 1;
        s.slideStep = 0x20;
      }
      if (s.slideStep & 0x20)
      {
        if (s.queue.len != 0)
          return;
        s.gameReq = 0x01;
        s.springStep = 1;
        s.axFlag = 0;
      }
    }

    void ModeC8(void)
    {
      if (s.c8State & 4)
        return;
      if (!(s.c8State & 2))
      {
        if (!(s.c8State & 1))
        {
          s.out2d &= 0xFE;
          s.status &= ~0x10;
          Q5();
          s.c8State |= 1;
        }
        if (s.queue.len != 0)
          return;
        s.c8State |= 2;
      }
      // The cockpit at its centre
      if (s.cockpit >= UINT8(s.cockpitCtr - 2) && s.cockpit < UINT8(s.cockpitCtr + 3))
      {
        s.out2d |= 0x01;
        s.status |= 0x10;
        s.c8State |= 4;
      }
    }

    void ModeC9(void)
    {
      if (s.c9State & 2)
        return;
      if (!(s.c9State & 1))
      {
        Q5();
        s.c9State |= 1;
      }
      if (s.queue.len != 0)
        return;
      s.c9State |= 2;
      s.out2d |= 0x01;
      s.status |= 0x10;
    }

    void ModeCA(void)
    {
      if (!(s.caState & 2))
      {
        if (!(s.caState & 1))
        {
          Q6();
          s.caState |= 1;
        }
        if (s.queue.len != 0)
          return;
        s.out2d &= 0xFE;
        s.status &= ~0x10;
        s.caState |= 2;
        return;
      }
      // The test's commands 80-87, each through the queue (then held)
      switch (LowestBit(s.testCmd, 0, 7))
      {
      case 0: SetQueue({ { 0xFE, 0x80 } }); break;
      case 1: SetQueue({ { 0xFE, 0x69 } }); break;
      case 2: SetQueue({ { 0xFE, 0x97 } }); break;
      case 3: s.out2d |= 0x01; s.status |= 0x10; break;
      case 4: s.out2d &= 0xFE; s.status &= ~0x10; break;
      case 5:
        // The wheel as its centre (centre_off not computed again)
        if (s.testBusy != 0xFF)
        {
          if (s.wheel < 0x70 || s.wheel >= 0x90)
          {
            s.testBusy = 0;
            break;
          }
          s.centre = s.wheel;
          SetQueue({ { 0xF8, 0xF9 }, { 0xFE, 0x80 }, { 0xF8, 0xFA } });
          s.testBusy = 0xFF;
        }
        if (s.queue.len != 0)
          return;
        s.testBusy = 0;
        break;
      case 6:
        if (s.cockpit < 0x60 || s.cockpit >= 0xA0)
          return;
        s.cockpitCtr = s.cockpit;
        break;
      case 7:
        break;  // port 2D bit 1
      default:
        return;
      }
      s.testCmd = 0;
    }

    void ModeCB(void)
    {
      if (!(s.cbState & 1))
      {
        Q6();
        s.cbState |= 1;
      }
      if (s.queue.len != 0)
        return;
      // The start-up again from CE (no CF; C1's states, the FB curve and the centre kept)
      s.mode = 0x01;
      s.replySel |= 0x80;
      Enter(WaitCentres);
    }
  };

  // EPR-20512 (Sega Rally 2): the main board's torque relayed, FD 11 then FE v held until the next (the torque by
  // position, a small push within 8 of the centre, only until the first)
  class CSegaRally2Program : public CScudRaceProgram
  {
  public:
    CSegaRally2Program()
      : CScudRaceProgram(kEpr20512)
    {
    }

  protected:
    void DecodeCommand(void) override
    {
      if (s.cmdNew != 0xFF)
        return;
      s.cmdNew = 0;
      s.replySel &= 0x7F;
      UINT8 c = s.command;
      if (c < 0x80)
      {
        s.torqueN = c & 0x3F;
        s.torqueTab = (c < 0x40) ? 0x2A62 : 0x2AA2;
        GameCommand(0x10);
        s.torqueFlag = 0;
        return;
      }
      switch (c >> 4)
      {
      case 0x8:
        s.testCmd = Bit(c);
        return;
      case 0xC:
        switch (c)
        {
        case 0xC1: s.mode = 0x01; s.attractSt = 0; s.blinkSt = 0; s.shakeC2St = 0; s.shakeReq = 0; return;
        case 0xC2: s.shakeReq = 0xFF; s.shakeC2St = 0; return;
        case 0xC6: s.mode = 0x40; s.c6State = 0; return;
        case 0xC7: s.mode = 0x04; s.gameInit = 0; s.gameReq = 0; return;
        case 0xC8: s.mode = 0x08; s.c8State = 0; return;
        case 0xC9: s.mode = 0x10; s.c9State = 0; return;
        case 0xCA: EnterTest(); return;
        case 0xCB: s.mode = 0x80; s.cbState = 0; return;
        default: return;  // no C4: no strength command
        }
      case 0xD:
        s.replySel = Bit(c);
        return;
      case 0xF:
        s.initReply = c;
        s.replySel |= 0x80;
        return;
      default:
        return;
      }
    }
  };
}

std::unique_ptr<IServoProgram> MakeServoProgram(uint32_t crc32)
{
  switch (crc32)
  {
  case 0xB139481D:
    return std::make_unique<CDaytona2Program>();
  case 0xC9FAC464:
    return std::make_unique<CScudRaceProgram>();
  case 0xCF64350D:
    return std::make_unique<CSegaRally2Program>();
  case 0xDBF88DE6:
    return std::make_unique<CScudRaceProgram>(kEpr19338);
  default:
    return nullptr;
  }
}
