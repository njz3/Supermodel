/**
 ** Supermodel
 ** A Sega Model Series Arcade Emulator.
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
 * ClutchProgramSim.cpp
 *
 * The clutch drive boards' programs simulated (see ClutchProgramSim.h), from
 * their disassemblies documented in BackForceFeeder's docs (EPR-16488A,
 * EPR-17891, EPR-15111). They need no ROM: the few constants they use are
 * written here.
 *
 * The three run on a Z80 at 4 MHz interrupted every 4096 cycles (IM 1), as
 * CModel2DriveBoard schedules it. EPR-16488A does everything in its interrupt
 * once calibrated; EPR-17891's interrupt does the I/O and its main loop
 * computes the motor byte between two interrupts. Both are simulated exactly,
 * interrupt by interrupt (but for EPR-16488A's interrupts longer than the
 * timer's period, see CDaytonaProgram::Interrupt). EPR-15111 computes its
 * effects in cooperative tasks that take 3 or 4 interrupt periods a round:
 * when the motor byte changes depends on the cycles each path of the program
 * takes. Here the program's cycles are counted (the interrupt's by the
 * command it decodes, the tasks' by the paths through their routines, as
 * Supermodel's Z80 core counts them) to put each motor write in the period
 * the emulated board puts it; only the few cycles an interrupt waits for the
 * instruction in progress are not known, so a read or a write within a few
 * cycles of an interrupt can fall on the other side of it (measured: the
 * motor byte differs at 0.02-0.5 % of the interrupts).
 */

#include "ClutchProgramSim.h"

#include <cstring>

namespace
{
  // The interrupt timer's period (Z80 cycles)
  constexpr int kPeriod = 4096;

  // |a - b|
  inline UINT8 Distance(UINT8 a, UINT8 b)
  {
    return a >= b ? UINT8(a - b) : UINT8(b - a);
  }

  /****************************************************************************
   EPR-16488A: Daytona USA

   At power-up the main program calibrates (DIP switch 4 OFF): the motor driven
   down with a power rising by one every 976 interrupts until the wheel reads
   below 70, then up until it reads 90 or more; the mean of the two powers,
   less one, is the "breakaway" added to every force. No wheel motion by power
   62: the calibration fails and the board never drives the motor again. Then
   the interrupt does everything: it takes a command once read the same at two
   interrupts in a row, replies, and ramps the motor's power by one an
   interrupt towards the effect's target.
  ****************************************************************************/

  class CDaytonaProgram : public IClutchProgram
  {
  public:
    void Reset(UINT8 dsw1, UINT8 dsw2) override
    {
      (void) dsw2;
      memset(&s, 0, sizeof(s));
      s.dip = dsw1;
      s.reply = 0xFF;
      s.port46 = s.shown46 = 0xFF;
      s.port23 = s.shown23 = 0xFF;
      s.step = StepBoot;
    }

    double FirstInterruptCycles(void) const override
    {
      // The program enables interrupts 1 075 961 cycles after reset and takes the request pending since the timer's
      // 262nd period at once; the next interrupts come at the timer's periods from the 263rd. The first only reads
      // the wheel, which nothing uses before the calibration's first step ends: it is taken at the 262nd period, the
      // others then fall on the timer.
      return 262.0 * kPeriod;
    }

    void Interrupt(UINT8 command, UINT8 wheel) override
    {
      // An interrupt longer than the timer's period (a spring's loop run 254-256 times: centre 00, see Spring) holds
      // the next request until its end, where the program takes it at once; requests made meanwhile are lost. So
      // a period has one interrupt, or none, and its writes may land in the next period.
      int64_t next = s.time + kPeriod;
      if (s.time >= s.busyUntil)
        Isr(command, wheel, s.time);
      else
      {
        s.pending = 1;
        if (s.busyUntil < next)
        {
          s.pending = 0;
          Isr(command, wheel, s.busyUntil);
        }
      }
      // The ports as they stand at the next request
      int n = 0;
      while (n < s.writes && s.write[n].at < next)
      {
        if (s.write[n].port == 0x46)
          s.shown46 = s.write[n].value;
        else
          s.shown23 = s.write[n].value;
        n++;
      }
      for (int i = n; i < s.writes; i++)
        s.write[i - n] = s.write[i];
      s.writes = UINT8(s.writes - n);
      s.time = next;
    }

    UINT8 Motor(void) const override
    {
      return s.shown46;
    }

    UINT8 Reply(void) const override
    {
      return s.shown23;
    }

    void SaveState(CBlockFile *saveState) override
    {
      saveState->NewBlock("ClutchProgram16488A", __FILE__);
      saveState->Write(s);
    }

    void LoadState(CBlockFile *saveState) override
    {
      if (saveState->FindBlock("ClutchProgram16488A") == Result::OKAY)
        saveState->Read(s);
    }

  private:
    enum Step : UINT8
    {
      StepBoot,     // before interrupt 1
      StepPre1,     // power 1, no way
      StepPre2,     // power 0
      StepDown,     // way 1 (down), power D
      StepMid1,
      StepMid2,
      StepUp,       // way 2 (up), power D
      StepEnd1,
      StepEnd2,
      StepRunning
    };

    struct State
    {
      int64_t time;         // Z80 cycles: the timer's request this call stands for
      int64_t busyUntil;    // the end of the last interrupt (its EI)
      struct
      {
        int64_t at;
        UINT8 port, value;
      } write[4];           // port writes made after the next request
      UINT8 writes;
      UINT8 shown46, shown23;   // the ports at the last request
      uint16_t loops;       // the spring's loop count in this interrupt
      UINT8 pending;        // a request made during the last interrupt
      uint16_t ticks;       // E210: every interrupt; zeroed by the waits and the oscillation detector
      uint16_t wait;        // interrupts left in the calibration's step
      UINT8 dip;            // port 26
      UINT8 step;           // the calibration (main program)
      UINT8 power;          // D: the calibration's power
      UINT8 breakawayDown;  // E104
      UINT8 calibFailed;    // E000
      UINT8 ready;          // E001
      UINT8 testMode;       // E002
      UINT8 oscillating;    // E003
      UINT8 breakaway;      // E105
      UINT8 wheel;          // E120
      UINT8 centre;         // E122
      UINT8 offCentre;      // E123
      UINT8 powerRamp;      // E132
      UINT8 way;            // E134: 0 none, 1 down (bit 7), 2 up (bit 6)
      UINT8 side;           // E135: 0 centre, 1 below, 2 above
      UINT8 enabled;        // E136
      UINT8 basePower;      // E139
      UINT8 dipPower;       // E13A
      UINT8 boost;          // E13B
      UINT8 reply;          // E140
      UINT8 cmdRx;          // E141
      UINT8 effect;         // E142
      UINT8 swings;         // E161
      UINT8 centredFor;     // E162
      UINT8 port46, port23;
    } s;

    // The interrupt (0038 -> 01CD), taken at `start`
    void Isr(UINT8 command, UINT8 wheel, int64_t start)
    {
      UINT8 old46 = s.port46, old23 = s.port23;
      bool running = s.ready != 0;
      s.loops = 0;
      s.ticks++;
      if (!s.testMode)
      {
        s.wheel = wheel;
        if (!s.ready)
        {
          // The ISR only counts: the main program calibrates
          Calibrate();
        }
        else
        {
          ReceiveCommand(command);
          s.basePower = UINT8(((Distance(s.centre, s.wheel) & 0xF0) >> 4) + s.breakaway + s.boost);
          FindSide();
          DetectSwings();
          ComputeEffect();
        }
      }
      // Its length to its EI: about 2 400 cycles, 17 more per turn of a spring's loop (measured: 6 713 for 254); its
      // OUT 23 about 1 000 cycles in, its OUT 46 254 before the EI (the calibration's, by the main program after it)
      s.busyUntil = start + 2395 + 17 * s.loops;
      if (s.port23 != old23)
        QueueWrite(start + 1000, 0x23, s.port23);
      if (s.port46 != old46)
        QueueWrite(running ? s.busyUntil - 254 : start + 1300, 0x46, s.port46);
    }

    void QueueWrite(int64_t at, UINT8 port, UINT8 value)
    {
      if (s.writes == 4)
        return;
      s.write[s.writes].at = at;
      s.write[s.writes].port = port;
      s.write[s.writes].value = value;
      s.writes++;
    }

    // The calibration's motor writes (065E): power D, a way or none; the ramp and the way are left alone
    void Out(UINT8 power, UINT8 way)
    {
      UINT8 v = UINT8(~power);
      if (way == 1)
        v &= 0x7F;
      else if (way == 2)
        v &= 0xBF;
      s.port46 = v;
    }

    UINT8 DipPower(void) const
    {
      return UINT8(((~s.dip) & 7) * 4);
    }

    void Wait(Step next)
    {
      s.step = next;
      s.wait = 976;
    }

    void Done(void)
    {
      s.ready = 1;
      s.step = StepRunning;
      s.ticks = 0;
    }

    // DIP switch 4 ON (or the calibration failed): breakaway 5, the centre never set (00)
    void Skip(void)
    {
      s.port46 = 0xFF;
      s.breakaway = 5;
      s.dipPower = DipPower();
      Done();
    }

    void Fail(void)
    {
      s.calibFailed = 1;
      Skip();
    }

    // The main program after interrupt 1 and after each step's 976 interrupts (calibrate, 05AA)
    void Calibrate(void)
    {
      if (s.step == StepBoot)
      {
        UINT8 t = UINT8((~s.dip) & 0xC0);
        if (t == 0x40 || t == 0x80)
        {
          // Self-test: without a loop-back from port 23 to port 27 it stops on error 5, the reply 55
          s.testMode = 1;
          s.port23 = 0x55;
          return;
        }
        if ((s.dip & 0x08) == 0)
        {
          Skip();
          return;
        }
        Out(1, 0);
        Wait(StepPre1);
        return;
      }
      if (--s.wait != 0)
        return;
      switch (s.step)
      {
      case StepPre1:
        Out(0, 0);
        Wait(StepPre2);
        break;
      case StepPre2:
        s.centre = 0x80;    // assumed, never measured
        s.power = 1;
        Out(s.power, 1);
        Wait(StepDown);
        break;
      case StepDown:
        if (s.wheel < UINT8(s.centre - 0x10))
        {
          s.breakawayDown = s.power;
          Out(1, 0);
          Wait(StepMid1);
        }
        else if (++s.power >= 0x3F)
          Fail();
        else
        {
          Out(s.power, 1);
          Wait(StepDown);
        }
        break;
      case StepMid1:
        Out(0, 0);
        Wait(StepMid2);
        break;
      case StepMid2:
        s.power = 1;
        Out(s.power, 2);
        Wait(StepUp);
        break;
      case StepUp:
        if (s.wheel >= UINT8(s.centre + 0x10))
        {
          // The power that moved the wheel up; with the one that moved it down, the breakaway
          s.breakaway = UINT8(((UINT8(s.breakawayDown + s.power)) >> 1) - 1);
          Out(1, 0);
          Wait(StepEnd1);
        }
        else if (++s.power >= 0x3F)
          Fail();
        else
        {
          Out(s.power, 2);
          Wait(StepUp);
        }
        break;
      case StepEnd1:
        Out(0, 0);
        Wait(StepEnd2);
        break;
      case StepEnd2:
        s.dipPower = DipPower();
        Done();
        break;
      default:
        break;
      }
    }

    // A byte counts once read the same at two interrupts in a row, then at every interrupt while it stays (0520)
    void ReceiveCommand(UINT8 v)
    {
      if (v != s.cmdRx)
      {
        s.cmdRx = v;
        return;
      }
      UINT8 r = v;
      if ((v & 0xF0) == 0x00)
      {
        // Forces on: 05-07, 0A, 0D-0F; off (and no boost): the others
        if (v == 0x0A || (v & 7) >= 5)
          s.enabled = 1;
        else
        {
          s.enabled = 0;
          s.boost = 0;
        }
      }
      else if ((v & 0xF0) == 0xF0)
      {
      }
      else if (v >= 0x80)
      {
        if (v == 0x80 || v == 0x83)
          r = s.wheel;
        else if (v == 0x81)
          r = 0xFF;
        else if (v == 0x82)
          r = s.dip;
      }
      else if ((v & 0xF0) == 0x70)
        s.boost = UINT8(((v & 0x0E) >> 1) + 2);
      else
        s.effect = v;
      s.reply = r;
      s.port23 = r;
    }

    // Centre +-2: side 0; below: 1; above: 2 (027D)
    void FindSide(void)
    {
      s.offCentre = Distance(s.wheel, s.centre);
      UINT8 a = UINT8(s.centre + 2);
      if (a < s.wheel)
        s.side = 2;
      else if (UINT8(a - 5) < s.wheel)
        s.side = 0;
      else
        s.side = 1;
    }

    void ResetSwings(void)
    {
      s.oscillating = 0;
      s.swings = 0;
      s.ticks = 0;
    }

    // The oscillation detector (02A6): ten returns to the centre after 48 to 291 interrupts away, and the
    // centring springs stop pushing outside their dead zone
    void DetectSwings(void)
    {
      if (s.side != 0)
      {
        uint16_t limit = s.oscillating ? 0x05B9 : 0x0125;
        if (s.ticks >= limit)
          ResetSwings();
        return;
      }
      if (s.ticks == 1)
      {
        // At the centre, as at the previous interrupt
        s.centredFor++;
        if (s.centredFor < 0x62)
          s.ticks = 0;
        else
          ResetSwings();
        return;
      }
      // Back at the centre
      s.centredFor = 0;
      if (s.oscillating)
      {
        s.ticks = 0;
        return;
      }
      if (s.ticks < 0x31 || s.ticks >= 0x125)
      {
        ResetSwings();
        return;
      }
      if (++s.swings >= 0x0A)
        s.oscillating = 1;
      s.ticks = 0;
    }

    UINT8 Strength(void) const
    {
      return UINT8((s.effect & 7) * 2 + s.dipPower);
    }

    // The centre zone of the springs: the way kept until the wheel is exactly at the centre, the breakaway's power
    UINT8 Zone(void)
    {
      if (s.way != 0 && s.offCentre < 1)
        s.way = 0;
      return s.breakaway;
    }

    // A centring spring: from the dead zone dz, a slope of 2 (3 from strength 1E), full strength from `full`
    UINT8 Spring(UINT8 dz, UINT8 full)
    {
      UINT8 strength = Strength();
      UINT8 slope = strength >= 0x1E ? 3 : 2;
      UINT8 a;
      if (s.offCentre >= full)
        a = strength;
      else
      {
        UINT8 b = UINT8(s.offCentre - dz);
        int count = b == 0 ? 256 : b;
        s.loops = uint16_t(count);
        a = UINT8(slope * count - s.boost);
        if (a < 0x80 && a >= strength)
          a = strength;
      }
      return UINT8(a + s.basePower);
    }

    // A push one way; changing way starts the ramp again from 0
    UINT8 Push(UINT8 way)
    {
      if (s.way == (way == 1 ? 2 : 1))
        s.powerRamp = 0;
      s.way = way;
      return UINT8(Strength() + s.basePower + 0x0A);
    }

    // The effect's target (031A), then the ramp one step towards it and the motor byte (04E7)
    void ComputeEffect(void)
    {
      bool off = true;
      UINT8 target = 0;
      if (!s.calibFailed && s.enabled)
      {
        off = false;
        switch (s.effect & 0xF8)
        {
        case 0x20:
          s.way = 0;
          target = UINT8(Strength() + s.basePower);
          break;
        case 0x30:
        case 0x38:
        {
          bool narrow = (s.effect & 0xF8) == 0x30;
          bool inZone = narrow ? s.side == 0 : s.offCentre < 8;
          if (inZone || s.side == 0)
            target = Zone();
          else
          {
            s.way = s.side == 1 ? 2 : 1;
            if (s.oscillating)
              off = true;
            else
              target = narrow ? Spring(0x02, 0x15) : Spring(0x07, 0x18);
          }
          break;
        }
        case 0x40:
          target = Push(s.side == 2 ? 2 : 1);
          break;
        case 0x50:
          target = Push(1);
          break;
        case 0x60:
          target = Push(2);
          break;
        default:
          off = true;
          break;
        }
      }
      if (off)
      {
        s.way = 0;
        target = 0;
      }
      UINT8 a = s.powerRamp;
      if (a < target)
        s.powerRamp = ++a;
      else if (a > target)
        s.powerRamp = --a;
      if (a >= 0x3F)
      {
        a = 0x3F;
        s.powerRamp = 0x3F;
      }
      UINT8 v = UINT8(~a);
      if (s.way == 1)
        v &= 0x7F;
      else if (s.way == 2)
        v &= 0xBF;
      s.port46 = v;
    }
  };

  /****************************************************************************
   EPR-17891: Sega Rally Championship

   At power-up the main program calibrates (DIP switch 4 OFF): the motor driven
   the 80-9F way with a power rising by one every 2559 interrupts until the
   wheel reads 9E or more, then the C0-DF way until it reads below 60; the two
   powers are added to the pushes away from the centre. No wheel motion by
   power 1F: error 7, the board dead until reset. Then it waits for a command
   00 and runs: the interrupt reads the inputs and writes the reply and the
   motor byte the main loop computed after the previous interrupt.
  ****************************************************************************/

  class CSegaRallyProgram : public IClutchProgram
  {
  public:
    void Reset(UINT8 dsw1, UINT8 dsw2) override
    {
      (void) dsw2;
      memset(&s, 0, sizeof(s));
      s.dip = dsw1;
      s.port46 = 0xFF;
      s.port23 = 0xFF;
      // Self-test (DIP switch 7 or 8 alone ON): interrupts never enabled, it stops on error 5 without a loop-back
      UINT8 t = UINT8(dsw1 & 0xC0);
      s.phase = t == 0x40 || t == 0x80 ? PhaseError : PhaseBoot;
    }

    double FirstInterruptCycles(void) const override
    {
      // Interrupts enabled 3 074 222 cycles after reset: the request pending since the timer's 750th period is
      // taken at once, the next ones at the timer's periods from the 751st
      return 750.0 * kPeriod;
    }

    void Interrupt(UINT8 command, UINT8 wheel) override
    {
      if (s.phase == PhaseError)
        return;
      // The interrupt (0225): the inputs, then once the link is up the reply and the motor byte computed before
      s.adc = wheel;
      s.cmdRx = command;
      s.dipSw = s.dip;
      s.ticks++;
      if (s.linkUp)
      {
        s.reply = command == 0x01 ? s.adc : command == 0x02 ? 0xFF : command == 0x03 ? s.dipSw : command;
        s.port23 = s.reply;
        s.port46 = s.motorOut;
      }
      // The main program until the next interrupt
      switch (s.phase)
      {
      case PhaseBoot:
        if ((s.dipSw & 0x08) == 0)
        {
          // DIP switch 4 ON: no calibration, both compensations 0
          s.port46 = 0xFF;
          WaitForLink();
        }
        else
        {
          s.brk80 = s.brkC0 = 0xFF;
          Step80();
        }
        break;
      case PhaseCal80:
        if (s.ticks == 0xFFFF)
        {
          if (s.adc < 0x9E)
            Step80();
          else
            StepC0();
        }
        break;
      case PhaseCalC0:
        if (s.ticks == 0xFFFF)
        {
          if (s.adc >= 0x60)
            StepC0();
          else
          {
            s.port46 = 0xFF;
            WaitForLink();
          }
        }
        break;
      case PhaseWait:
        Link();
        break;
      case PhaseRun:
        Compute();
        break;
      default:
        break;
      }
    }

    UINT8 Motor(void) const override
    {
      return s.port46;
    }

    UINT8 Reply(void) const override
    {
      return s.port23;
    }

    void SaveState(CBlockFile *saveState) override
    {
      saveState->NewBlock("ClutchProgram17891", __FILE__);
      saveState->Write(s);
    }

    void LoadState(CBlockFile *saveState) override
    {
      if (saveState->FindBlock("ClutchProgram17891") == Result::OKAY)
        saveState->Read(s);
    }

  private:
    enum Phase : UINT8
    {
      PhaseBoot,    // before interrupt 1
      PhaseCal80,   // calibration, the 80-9F way
      PhaseCalC0,   // the C0-DF way
      PhaseWait,    // waiting for a 00
      PhaseRun,
      PhaseError    // error 7 (or the self-test): nothing more
    };

    struct State
    {
      uint16_t ticks;     // E510: every interrupt; F600 at each calibration step
      UINT8 dip;          // port 26
      UINT8 phase;
      UINT8 cmdRx;        // E201
      UINT8 dipSw;        // E203
      UINT8 adc;          // E210
      UINT8 reply;        // E301
      UINT8 motorOut;     // E310
      UINT8 motorCmd;     // E520
      UINT8 lastTorque;   // E521
      UINT8 powerOffset;  // E522
      UINT8 brkC0;        // E524
      UINT8 brk80;        // E525
      UINT8 linkUp;       // E526
      UINT8 chopShift;    // E527
      UINT8 port46, port23;
    } s;

    // A calibration step: one more power, held for 2559 interrupts (ticks F600 to FFFF); power 20 is error 7
    void Step80(void)
    {
      UINT8 p = UINT8(s.brk80 + 1);
      if (p == 0x20)
      {
        Error();
        return;
      }
      s.brk80 = p;
      s.port46 = UINT8(~(p | 0x40));
      s.ticks = 0xF600;
      s.phase = PhaseCal80;
    }

    void StepC0(void)
    {
      UINT8 p = UINT8(s.brkC0 + 1);
      if (p == 0x20)
      {
        Error();
        return;
      }
      s.brkC0 = p;
      s.port46 = UINT8(~(p | 0x80));
      s.ticks = 0xF600;
      s.phase = PhaseCalC0;
    }

    void Error(void)
    {
      s.port46 = 0xFF;
      s.port23 = 0xFF;
      s.phase = PhaseError;
    }

    // Motor off, reply FF, "--" on the display: the board waits for a 00 (051E)
    void WaitForLink(void)
    {
      s.port46 = 0xFF;
      s.port23 = 0xFF;
      s.phase = PhaseWait;
      Link();
    }

    void Link(void)
    {
      if (s.cmdRx != 0)
        return;
      s.linkUp = 0xFF;
      s.phase = PhaseRun;
      Compute();
    }

    // The motor byte from the command, the wheel and the DIP switches (compute_motor 036B)
    void Compute(void)
    {
      s.powerOffset = UINT8((~s.dipSw) & 7);
      UINT8 b = s.cmdRx;
      for (int replay = 0; replay < 3; replay++)
      {
        UINT8 family = b & 0xF0;
        if (b == 0x00 || (replay > 0 && family == 0x00))
        {
          s.lastTorque = b;
          s.motorCmd = 0;
          break;
        }
        if (family == 0x10)
        {
          // 10-17: the chopping; then the last torque again
          if ((b & 0x08) == 0)
            s.chopShift = b & 7;
          b = s.lastTorque;
          continue;
        }
        if (family == 0x40 || family == 0x50)
        {
          // A power with no way
          s.lastTorque = b;
          s.motorCmd = UINT8((b & 0x1F) + 1 + s.powerOffset);
          break;
        }
        if (family == 0x80 || family == 0x90)
        {
          // The 80-9F way, the calibration's power added on that side of 7D
          UINT8 comp = (UINT8(s.adc + 3) >> 2) >= 0x20 ? s.brk80 : 0;
          s.lastTorque = b;
          s.motorCmd = UINT8(UINT8((b & 0x1F) + 1 + s.powerOffset + comp) | 0x40);
          break;
        }
        if (family == 0xC0 || family == 0xD0)
        {
          UINT8 comp = (UINT8(s.adc + 3) >> 2) < 0x20 ? s.brkC0 : 0;
          s.lastTorque = b;
          s.motorCmd = UINT8(UINT8((b & 0x1F) + 1 + s.powerOffset + comp) | 0x80);
          break;
        }
        // Any other byte replays the last torque
        b = s.lastTorque;
      }
      if (s.chopShift != 0)
      {
        // On and off every 2^(N+1) interrupts, a quarter stronger when on (apply_chop 0426)
        if (((s.ticks >> 1) & 0xFF) & (1 << s.chopShift))
          s.motorCmd &= 0xC0;
        else
          s.motorCmd = UINT8(s.motorCmd + ((s.motorCmd & 0x3F) >> 2));
      }
      s.motorOut = UINT8(~s.motorCmd);
    }
  };

  /****************************************************************************
   EPR-15111: Virtua Racing, player 1's side

   The board serves two seats; here player 2's link reads a constant 00, its
   wheel 80, and its DIP switch 4 is ON (no calibration). The interrupt reads
   the links (a command counts once read the same at two interrupts in a row,
   the reply at every interrupt), the DIP switches and the wheels. Eight
   cooperative tasks (task 1 player 1, task 2 player 2, tasks 3-8 idle) run
   between the interrupts: task 1 calibrates (DIP switch 4 OFF: the motor
   driven down, then up, with a power rising every 1000 interrupts until the
   wheel passes each of five places a side; the powers found are the base
   forces at those places), then for ever computes the effect and writes the
   motor byte, once a round of the eight tasks: every 3 or 4 interrupts.

   Time here is the tasks' time: the Z80's cycles outside the interrupt, from
   the end of interrupt 10 (when the tasks are started). Each interrupt takes
   its own number of cycles (by the command it decodes), each task round its
   own (by the effect's path), as Supermodel's Z80 core counts them; task 1's
   events (its reads, its motor write) are placed at their cycle in the round:
   the start of the instruction, which runs before an interrupt taken after it
   started.
  ****************************************************************************/

  class CVirtuaRacingProgram : public IClutchProgram
  {
  public:
    void Reset(UINT8 dsw1, UINT8 dsw2) override
    {
      memset(&s, 0, sizeof(s));
      s.dsw1 = dsw1;
      s.dsw2 = dsw2;
      s.port46 = 0xFF;
      s.port23 = 0xFF;
      s.p1.state = 1;
      s.t1At = -1;
      s.p2At = -1;
    }

    double FirstInterruptCycles(void) const override
    {
      // Interrupts enabled 1 929 227 cycles after reset, right after the timer's 471st period
      return 471.0 * kPeriod;
    }

    void Interrupt(UINT8 command, UINT8 wheel) override
    {
      s.isrCount++;
      int cycles;
      if (s.e700 != 0)
      {
        // A side calibrating (or a self-test): the ADCs and the timers only, no link read or answered (1217)
        s.p1.wheel = wheel;
        s.timer++;
        if (s.p2Left != 0 && --s.p2Left == 0)
          s.e700 &= ~0x02;
        s.ticks++;
        cycles = 1555;
      }
      else
      {
        // read_links (0D09): player 1's command and reply, DSW1, player 2's (a constant 00), DSW2
        bool latched = command == s.p1.cmdRaw;
        UINT8 c = latched ? command : s.p1.cmd;
        cycles = 1938 + CommandCycles(c, s.p1.forcesOn != 0) + (latched ? 0 : 10) + (s.p1.idle != 0xFF ? 19 : 0) +
                 (s.p2Idle != 0xFF ? 19 : 0);
        UINT8 r = Command(command);
        s.port23 = r;
        s.e200 = s.dsw1;
        s.e201 = s.dsw2;
        // read_wheels (0EA0), count_ticks (0E7C)
        s.p1.wheel = wheel;
        s.ticks++;
        if (s.p1.idle != 0xFF)
          s.p1.idle++;
        if (s.p2Idle != 0xFF)
          s.p2Idle++;
      }
      if (s.isrCount < 10)
        return;
      if (s.isrCount == 10)
        StartTasks();
      // The tasks until the next interrupt
      int64_t end = s.clock + kPeriod - cycles;
      RunTasks(end);
      s.clock = end;
    }

    UINT8 Motor(void) const override
    {
      return s.port46;
    }

    UINT8 Reply(void) const override
    {
      return s.port23;
    }

    void SaveState(CBlockFile *saveState) override
    {
      saveState->NewBlock("ClutchProgram15111", __FILE__);
      saveState->Write(s);
    }

    void LoadState(CBlockFile *saveState) override
    {
      if (saveState->FindBlock("ClutchProgram15111") == Result::OKAY)
        saveState->Read(s);
    }

  private:
    // Task 1's events
    enum Event : UINT8
    {
      EvNone,
      EvStart,      // task_p1 (02E8)
      EvClear,      // E700 bit 0 cleared (p1_run 0562)
      EvCalOut,     // a calibration step's motor byte (p1_motor_out 0539)
      EvCalReset,   // its timer zeroed (p1_wait_step 048A)
      EvCalCheck,   // its timer tested, after a task round (04A7)
      EvMotorOff,   // the calibration's end or failure: motor FF, the powers
      EvZone,       // a pass (0567): update_zone, its read of the wheel
      EvBase,       // base_for_position, its read of the wheel
      EvEffect,     // run_effect, motor_byte
      EvOut         // OUT 46
    };

    // The calibration's phases (02E8-0487)
    enum CalPhase : UINT8
    {
      CalPreDown,   // power 1, no way
      CalDown,      // way 1 (down), the wheel to go below each threshold
      CalBrake,     // the breakaway, no way
      CalPreUp,     // power 1, no way
      CalUp         // way 2 (up), the wheel to reach each threshold
    };

    // Player 1's block (E500)
    struct Side
    {
      UINT8 cmdRaw;     // +00
      UINT8 cmd;        // +01
      UINT8 state;      // +02
      UINT8 forcesOn;   // +03
      UINT8 effect;     // +04
      UINT8 wheel;      // +06 (and E400)
      UINT8 zoneRef;    // +07
      UINT8 zone;       // +08
      UINT8 motor;      // +09
      UINT8 way;        // +0A
      UINT8 power;      // +0B
      UINT8 idle;       // +0C
      UINT8 failed;     // +11
      UINT8 base;       // +41
      UINT8 place[11];  // the base by place, from the wheel's low end: +24 +23 +22 +21 +20 +40 +30 +31 +32 +33 +34
    };

    struct State
    {
      int64_t clock;      // the tasks' time at the start of the current period
      int64_t t1At;       // task 1's next event (-1: none)
      int64_t p2At;       // task 2 clears E700 bit 1 (-1: done)
      int64_t passAt;     // the current pass's start (056A), or the next one's
      int64_t outAt;      // the calibration's motor write being made
      int64_t clearAt;    // the calibration over: E700 bit 0 cleared
      uint32_t isrCount;
      int32_t passCycles; // the current pass's routines so far
      uint16_t timer;     // E704: the calibrating interrupts since the step's start
      UINT8 dsw1, dsw2;   // ports 25, 26
      UINT8 e200, e201;   // the banks as read by the last interrupt: the answers to 81, 82
      UINT8 e700;         // bit 0 player 1 calibrating, bit 1 player 2; any: no link served
      UINT8 ticks;        // E300
      UINT8 p2Idle;       // E60C: player 2's interrupts since a way, the interrupt's length depends on it
      UINT8 t1Event;
      UINT8 calPhase, calIndex, calPower, calWay, calByte;
      UINT8 firstStep;    // the calibration's first step: its round also starts task 2
      UINT8 p2Zone;       // task 2 has computed its zone (its first pass sets it: 46 cycles more)
      uint32_t p2Left;    // DSW2 switch 4 OFF: the calibrating interrupts before player 2's calibration fails
      UINT8 port46, port23;
      Side p1;
    } s;

    // The task-time constants, measured on Supermodel's Z80 core with DSW2 showing the wheel (C0); see DisplayCycles
    static constexpr int kStart = 1712;             // end of interrupt 10 -> task_p1 (read_mode, the task contexts)
    static constexpr int kRound = 6332;             // a pass's round without its routines (056A -> 056A)
    static constexpr int kOut = 130;                // 056A -> OUT 46, without the routines
    static constexpr int kCalRound = 5436;          // a round of the calibration's wait loop
    static constexpr int kCalFirstCheck = 5506;     // a step's motor write -> its first timer test
    static constexpr int kCalReset = 89;            // the motor write -> the timer zeroed

    // The interrupt's cycles by the latched command player 1's side decodes (command 0D36), from a 1x with the
    // forces off (1938 in all)
    static int CommandCycles(UINT8 c, bool forcesOn)
    {
      switch (c)
      {
      case 0xF1: return -130;
      case 0xF2: return -113;
      case 0xF4: return -96;
      case 0xF8: return -79;
      case 0x81: return 102;
      case 0x82: return 119;
      default: break;
      }
      UINT8 family = c & 0xF0;
      if (family == 0x00)
        return c >= 0x01 && c <= 0x0A ? 34 + 17 * (c - 1) : 149;
      if (family <= 0x60)
        return 17 * ((family >> 4) - 1) + (forcesOn ? 48 : 0);
      return 142;
    }

    // The link (command 0D36): a byte counts once read the same at two interrupts in a row; the latched byte is
    // decoded again at every interrupt. Returns the reply.
    UINT8 Command(UINT8 v)
    {
      Side &p = s.p1;
      if (v == p.cmdRaw)
        p.cmd = v;
      else
        p.cmdRaw = v;
      UINT8 c = p.cmd;
      if (c == 0xF1 || c == 0xF2 || c == 0xF4 || c == 0xF8)
        return c;
      UINT8 family = c & 0xF0;
      if (family == 0x00)
      {
        if (c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x08 || c == 0x09)
        {
          p.state = c;
          p.forcesOn = 0;
        }
        else if (c == 0x05 || c == 0x06 || c == 0x07 || c == 0x0A)
        {
          p.state = c;
          p.forcesOn = 1;
        }
        return c;
      }
      if (family <= 0x60)
      {
        // An effect, kept only while the forces are on
        if (p.forcesOn)
          p.effect = c;
        return c;
      }
      // Questions: the banks as the previous interrupt read them, the wheel's previous sample
      if (c == 0x81)
        return s.e200;
      if (c == 0x82)
        return s.e201;
      if (c == 0x83)
        return p.wheel;
      return c;
    }

    // read_mode (0216) after interrupt 10: the game (DSW1 switch 8 OFF) starts the tasks; otherwise a self-test,
    // which shows a count on the display, then fails its link loop-back test (or waits on its bench switches):
    // the interrupts stay in calibrating mode, no reply, motor off
    void StartTasks(void)
    {
      s.clock = 0;
      UINT8 mode = s.dsw1 & 0xC0;
      if (mode == 0x40)
      {
        s.e700 = 0x01;
        return;
      }
      if (mode == 0x00)
      {
        s.e700 = 0x02;
        return;
      }
      s.e700 = 0x03;
      Schedule(EvStart, kStart);
      // Player 2's calibration (DSW2 switch 4 OFF), its wheel reading 80 for good: it fails after its 62 steps
      // (measured: interrupts 11-62 082 calibrating); meanwhile no link is served and no effect computed
      if (s.dsw2 & 0x08)
        s.p2Left = 62072;
    }

    void Schedule(Event e, int64_t at)
    {
      s.t1Event = e;
      s.t1At = at;
    }

    void RunTasks(int64_t end)
    {
      for (;;)
      {
        bool task2 = s.p2At >= 0 && (s.t1At < 0 || s.p2At <= s.t1At);
        int64_t at = task2 ? s.p2At : s.t1At;
        if (at < 0 || at >= end)
          break;
        if (task2)
        {
          // Player 2 has no calibration: p2_run (0809) clears its bit
          s.e700 &= ~0x02;
          s.p2At = -1;
        }
        else
          Task1(at);
      }
    }

    // The CP chain of p1_no_calibration (04C3): the weight 1-6 found at its compare, 7 at the last, 0 after it
    static int WeightChainCycles(UINT8 dsw)
    {
      int w = (~dsw) & 7;
      if (w == 0)
        return 136;
      if (w == 7)
        return 126;
      return 17 * w + 17;
    }

    // The DIP switches 1-3 ON, counted in binary
    static UINT8 Weight(UINT8 dsw)
    {
      return UINT8((~dsw) & 7);
    }

    // The display routine's cycles (0C7A), by the mode DSW2 switches 7-8 select, against the wheel's (C0)
    int DisplayCycles(void) const
    {
      switch (s.dsw2 & 0xC0)
      {
      case 0x00: return 322;
      case 0x40: return 331;
      case 0x80: return 359;
      default: return 340;
      }
    }

    int DisplayAdjust(void) const
    {
      return DisplayCycles() - 340;
    }

    // The powers without a calibration (p1_no_calibration 04B7): from 4 + 4w at the centre, 2 more a place out
    void NoCalibrationPowers(void)
    {
      UINT8 a = UINT8(4 + 4 * Weight(s.dsw1));
      Side &p = s.p1;
      p.place[5] = a;
      for (int i = 1; i <= 5; i++)
      {
        p.place[5 - i] = UINT8(a + 2 * i);
        p.place[5 + i] = UINT8(a + 2 * i);
      }
    }

    // The motor byte the calibration writes (p1_motor_out 0539): power D, bit 7 low for way 1, bit 6 for way 2
    static UINT8 RawByte(UINT8 power, UINT8 way)
    {
      UINT8 v = UINT8(~power);
      if (way == 1)
        return UINT8((v | 0x40) & 0x7F);
      if (way == 2)
        return UINT8((v | 0x80) & 0xBF);
      return UINT8(v | 0xC0);
    }

    // p1_motor_out's cycles to its OUT 46 by the way
    static int MotorOutCycles(UINT8 way)
    {
      return way == 1 ? 76 : way == 2 ? 83 : 93;
    }

    void Task1(int64_t at)
    {
      switch (s.t1Event)
      {
      case EvStart:
        if ((s.dsw1 & 0x08) == 0)
        {
          // No calibration (DSW1 switch 4 ON); task 2 starts in the round
          s.port46 = 0xFF;
          s.p1.motor = 0xFF;
          NoCalibrationPowers();
          int64_t run1 = at + 361 + WeightChainCycles(s.dsw1);
          int64_t run2 = run1 + 783 + WeightChainCycles(s.dsw2);
          s.p2At = (s.dsw2 & 0x08) ? -1 : run2 + 10;
          s.passAt = run2 + 4021;
          Schedule(EvClear, run1 + 10);
        }
        else
        {
          // The first step: power 1, no way; task 2 starts in its round
          s.calPhase = CalPreDown;
          s.calPower = 1;
          s.calWay = 0;
          s.calByte = RawByte(1, 0);
          s.firstStep = 1;
          s.outAt = at + 167;
          s.p2At = (s.dsw2 & 0x08) ? -1 : at + 1040 + WeightChainCycles(s.dsw2) + 10;
          Schedule(EvCalOut, s.outAt);
        }
        break;
      case EvCalOut:
        s.port46 = s.calByte;
        s.p1.motor = s.calByte;
        Schedule(EvCalReset, at + kCalReset);
        break;
      case EvCalReset:
        s.timer = 0;
        Schedule(EvCalCheck, s.outAt + (s.firstStep ? kCalFirstCheck - 25 - DisplayAdjust() : kCalFirstCheck) +
                 2 * DisplayAdjust());
        s.firstStep = 0;
        break;
      case EvCalCheck:
        if (s.timer < 1000)
          Schedule(EvCalCheck, at + kCalRound + 2 * DisplayAdjust());
        else
          CalibrationStep(at);
        break;
      case EvMotorOff:
        s.port46 = 0xFF;
        s.p1.motor = 0xFF;
        Schedule(EvClear, s.clearAt);
        break;
      case EvClear:
        s.e700 &= ~0x01;
        Schedule(EvZone, s.passAt + 44);
        break;
      case EvZone:
        if (s.e700 != 0)
        {
          // Player 2 still calibrating (DSW2 switch 4 OFF, its round only approximated here): no effect computed
          s.passCycles = 0;
          Schedule(EvOut, s.passAt + kOut);
          break;
        }
        s.passCycles = UpdateZone();
        Schedule(EvBase, at + s.passCycles + 17);
        break;
      case EvBase:
      {
        // run_effect reads the effect (an interrupt may have latched a new one) 66 cycles in, 85 with a way
        int cycles = BaseForPosition();
        s.passCycles += cycles;
        Schedule(EvEffect, at + cycles + 17 + (s.p1.way != 0 ? 85 : 66));
        break;
      }
      case EvEffect:
        s.passCycles += RunEffect();
        Schedule(EvOut, s.passAt + kOut + s.passCycles);
        break;
      case EvOut:
      {
        s.port46 = s.p1.motor;
        // The round: the display (blinking on a failed calibration), task 2's pass, the idle tasks
        int display = DisplayAdjust();
        if (s.p1.failed)
          display += 30 + (s.ticks >= 0x80 ? 55 - DisplayCycles() : 0);
        if (!s.p2Zone && s.e700 == 0)
        {
          display += 46;
          s.p2Zone = 1;
        }
        s.passAt += kRound + s.passCycles + display + DisplayAdjust();
        Schedule(EvZone, s.passAt + 44);
        break;
      }
      default:
        s.t1At = -1;
        break;
      }
    }

    // The next step's motor write, `cycles` after the timer test: one more power, on `way`; power 3F is a failure
    void NextStep(int64_t check, int cycles, UINT8 way)
    {
      s.calPower++;
      cycles += 25;
      if (s.calPower == 0x3F)
      {
        Fail(check + cycles);
        return;
      }
      s.calWay = way;
      s.calByte = RawByte(s.calPower, way);
      s.outAt = check + cycles + 17 + MotorOutCycles(way);
      Schedule(EvCalOut, s.outAt);
    }

    void StepAt(int64_t check, int cycles, UINT8 power, UINT8 way)
    {
      s.calPower = power;
      s.calWay = way;
      s.calByte = RawByte(power, way);
      s.outAt = check + cycles + 17 + MotorOutCycles(way);
      Schedule(EvCalOut, s.outAt);
    }

    // The calibration failed (p1_calib_failed 04B3, `at`): no torque for good, the powers of the weight alone
    void Fail(int64_t at)
    {
      s.p1.failed = 1;
      NoCalibrationPowers();
      int64_t run = at + 19 + 318 + WeightChainCycles(s.dsw1);
      s.clearAt = run + 10;
      s.passAt = run + 5822 + DisplayAdjust();
      s.p2Zone = 1;
      Schedule(EvMotorOff, at + 26);
    }

    // A step's 1000 interrupts are over (the timer test at `check`): the wheel tested, the next step
    void CalibrationStep(int64_t check)
    {
      static const UINT8 down[5] = { 0x71, 0x61, 0x51, 0x41, 0x31 };
      static const UINT8 up[5] = { 0x90, 0xA0, 0xB0, 0xC0, 0xD0 };
      Side &p = s.p1;
      UINT8 wheel = p.wheel;    // E400, the last interrupt's
      const int ret = 75;       // the test, the wait routine's return
      switch (s.calPhase)
      {
      case CalPreDown:
        s.calPhase = CalDown;
        s.calIndex = 0;
        NextStep(check, ret + 7, 1);
        break;
      case CalDown:
        if (wheel >= down[s.calIndex])
          NextStep(check, ret + 30, 1);
        else
        {
          // The power that moved the wheel below this threshold; the first gives the breakaway for now
          int cycles = ret + 30 + 23;
          p.place[4 - s.calIndex] = s.calPower;
          if (s.calIndex == 0)
          {
            p.place[5] = UINT8(s.calPower - 1);
            cycles += 23;
          }
          if (++s.calIndex < 5)
            NextStep(check, cycles, 1);
          else
          {
            // The breakaway held with no way: a brake
            s.calPhase = CalBrake;
            StepAt(check, cycles + 19 + 7, p.place[5], 0);
          }
        }
        break;
      case CalBrake:
        s.calPhase = CalPreUp;
        StepAt(check, ret + 7, 1, 0);
        break;
      case CalPreUp:
        s.calPhase = CalUp;
        s.calIndex = 0;
        NextStep(check, ret + 7, 2);
        break;
      case CalUp:
        if (wheel < up[s.calIndex])
          NextStep(check, ret + 30, 2);
        else
        {
          p.place[6 + s.calIndex] = s.calPower;
          if (++s.calIndex < 5)
            NextStep(check, ret + 30 + 23, 2);
          else
          {
            // Done: the breakaway is the lower of the first powers, less one; then the weight added to all
            bool downLower = p.place[4] < p.place[6];
            p.place[5] = UINT8((downLower ? p.place[4] : p.place[6]) - 1);
            int branch = downLower ? 33 : 23;
            UINT8 w4 = UINT8(4 * Weight(s.dsw1));
            for (int i = 0; i < 11; i++)
              p.place[i] = UINT8(p.place[i] + w4);
            int64_t run = check + 746 + branch;
            s.clearAt = run + 10;
            s.passAt = run + 5822 + DisplayAdjust();
            s.p2Zone = 1;
            Schedule(EvMotorOff, check + 202 + branch);
          }
        }
        break;
      default:
        break;
      }
    }

    // update_zone (0AA4): the centre zone, 79-88 going up, 77-86 coming down
    int UpdateZone(void)
    {
      static const UINT8 zonesUp[5] = { 0xFF, 0x89, 0x79, 0x01, 0x00 };
      static const UINT8 zonesDown[5] = { 0xFF, 0x87, 0x77, 0x01, 0x00 };
      Side &p = s.p1;
      UINT8 w = p.wheel;
      bool goingUp = w >= p.zoneRef;
      const UINT8 *table = goingUp ? zonesUp : zonesDown;
      int b = 4;
      for (int i = 0; i < 5 && w < table[i]; i++)
        b--;
      int cycles = 48 + (goingUp ? 20 : 10) + 7 + 37 * (4 - b) + 17 + 37 + 10;
      if (b != p.zone)
      {
        p.zone = UINT8(b);
        p.zoneRef = w;
        cycles += 46;
      }
      return cycles;
    }

    // base_for_position (0A1F): the base power of the wheel's place
    int BaseForPosition(void)
    {
      static const UINT8 bounds[10] = { 0x31, 0x41, 0x51, 0x61, 0x71, 0x90, 0xA0, 0xB0, 0xC0, 0xD0 };
      Side &p = s.p1;
      int k = 0;
      while (k < 10 && p.wheel >= bounds[k])
        k++;
      p.base = p.place[k];
      return 19 + (k < 10 ? 17 * (k + 1) : 180) + 48;
    }

    // A push one way (ROLL, UNCENTERING away from the centre): never a reversal at once, the way dropped first
    // (the clutch holds against the standing motor), the new way once 100 interrupts went by without one
    int Push(UINT8 want, UINT8 n, int same, int other, int wait, int start)
    {
      Side &p = s.p1;
      if (p.way == want)
      {
        p.power = UINT8(n + p.base + 1);
        return same;
      }
      if (p.way != 0)
      {
        p.way = 0;
        return other;
      }
      if (p.idle < 0x64)
        return wait;
      p.way = want;
      p.power = UINT8(n + p.base + 1);
      return start;
    }

    // run_effect (0ADC) and motor_byte (0C42)
    int RunEffect(void)
    {
      Side &p = s.p1;
      int cycles = 33;
      if (p.way != 0)
      {
        p.idle = 0;
        cycles += 19;
      }
      if (!p.forcesOn)
      {
        // The stored effect lost while the forces are off
        p.effect = 0x10;
        cycles += 36;
      }
      else
        cycles += 19;
      cycles += 26;
      UINT8 effect = p.effect;
      UINT8 n = effect & 0x0F;
      int family = effect >> 4;
      cycles += family >= 1 && family <= 6 ? 17 * family : 102;
      switch (family)
      {
      case 2:   // CLUTCH: no way, a brake
        p.way = 0;
        p.power = UINT8(n + p.base);
        cycles += 93;
        break;
      case 3:   // CENTERING
        if (p.zone < 2)
        {
          p.way = 2;
          p.power = UINT8(n + p.base);
          cycles += 139;
        }
        else if (p.zone >= 3)
        {
          p.way = 1;
          p.power = UINT8(n + p.base);
          cycles += 146;
        }
        else
        {
          p.way = 0;
          p.power = 0;
          cycles += 101;
        }
        break;
      case 4:   // UNCENTERING: away from the centre; in the centre zone the breakaway less one, no way
        if (p.zone == 2)
        {
          p.way = 0;
          p.power = UINT8(p.place[5] - 1);
          cycles += 124;
        }
        else if (p.zone < 2)
          cycles += Push(1, n, 150, 118, 125, 232);
        else
          cycles += Push(2, n, 167, 135, 142, 249);
        break;
      case 5:   // ROLL down
        cycles += Push(1, n, 114, 82, 89, 196);
        break;
      case 6:   // ROLL up
        cycles += Push(2, n, 114, 82, 89, 196);
        break;
      default:  // SPRING (1x), or the forces turned on before any effect: the panel's spring alone
        p.way = 0;
        p.power = 0;
        cycles += 48;
        break;
      }
      // motor_byte: the power (at most 3F) complemented, the way's bit low; all off on a failed calibration
      UINT8 power = p.power;
      cycles += 36 + 44 + 33 + 29;
      if (power >= 0x3F)
      {
        power = 0x3F;
        cycles += 7;
      }
      UINT8 m = UINT8(~power);
      if (p.way == 1)
      {
        m = UINT8((m | 0x40) & 0x7F);
        cycles += 26;
      }
      else if (p.way == 2)
      {
        m = UINT8((m | 0x80) & 0xBF);
        cycles += 33;
      }
      else
      {
        m |= 0xC0;
        cycles += 43;
      }
      if (p.failed)
      {
        m = 0xFF;
        cycles += 7;
      }
      p.motor = m;
      return cycles;
    }
  };
}

std::unique_ptr<IClutchProgram> MakeClutchProgram(uint32_t crc32)
{
  switch (crc32)
  {
  case EPR16488A_CRC32:
    return std::make_unique<CDaytonaProgram>();
  case EPR17891_CRC32:
    return std::make_unique<CSegaRallyProgram>();
  case EPR15111_CRC32:
    return std::make_unique<CVirtuaRacingProgram>();
  default:
    return nullptr;
  }
}
