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
 * Agent.cpp
 *
 * Agent mode (-agent: the window hidden; -agent-show: shown, to watch a race
 * played by a program, with -throttle for real time and -sound to hear it).
 * The protocol: one command a line on stdin, one answer on stdout
 * (everything else Supermodel prints goes to stderr). Once the game is loaded:
 *
 *   ready GAME WIDTH HEIGHT
 *
 * then, for each command:
 *
 *   step [N]            Runs N frames (1 by default) with the inputs as set.
 *                       Answer: "ok FRAME" then, on the same line, every byte
 *                       the main board wrote to the drive board meanwhile, in
 *                       hex (a byte equal to the previous one is not repeated).
 *   set INPUT VALUE     Holds an input at a value until released: an axis
 *                       (Steering, AnalogJoyX...) from -1 to 1, a pedal
 *                       (Accelerator, Brake...) from 0 to 1, a button (Start1,
 *                       Coin1, ViewChange...) 0 or 1. INPUT is the name of the
 *                       Input... setting in Supermodel.ini, without "Input".
 *                       An axis goes the trigonometric way, as a real wheel's
 *                       angle (and BackForceFeeder): +1 at its left end (its
 *                       minimum: Steering fully left, a stick left or up), -1
 *                       at its right end.
 *                       Answer: "ok", or "err ..." for an unknown input.
 *   setraw INPUT VALUE  The same with the raw value the game reads.
 *   get INPUT           The raw value the game reads now (held, or read from
 *                       the device at the last frame): "value RAW".
 *   release INPUT|all   Gives the input back to its device.
 *   inputs              The game's inputs: "inputs N", then N lines
 *                       "NAME TYPE", TYPE axis, analog, switch or other.
 *   obs [W H]           The last frame, scaled to W x H (its own size if
 *                       omitted or 0): "obs W H", a new line, then W * H * 3
 *                       bytes, RGB, top row first.
 *   shot PATH           Saves the last frame as a BMP file. "ok" or "err".
 *   save PATH           Saves a state. "ok" or "err".
 *   load PATH           Loads a state. "ok" or "err".
 *   ffb                 The force feedback commands sent since the last call,
 *                       whether a device takes them or not: "ffb N", then N
 *                       lines "FRAME INPUT EFFECT FORCE", EFFECT stop,
 *                       constant, selfcenter, friction or vibrate. FORCE is
 *                       -1..1, the trigonometric way as the axes: a constant
 *                       force > 0 pushes to the left (towards +1), < 0 to the
 *                       right, so a DirectInput device receives a magnitude of
 *                       FORCE * 10000 (as on sm2-emu); the others are a
 *                       strength >= 0.
 *   servo on|off        Starts or stops the drive board's journal.
 *   servo               The journal since the last call: "servo N", then N
 *                       lines "FRAME NMI PORT VALUE" (hex port and value):
 *                       what the Z80 wrote to its outputs at each NMI (29 the
 *                       reply, a new value only; 2A, 2D, 2E the servo or motor
 *                       boards) and each new command shown to it (28).
 *   outputs             The outputs set so far: "outputs NAME=VALUE ...".
 *   ram ADDRESS LENGTH  Memory read by the main CPU (hex address and length,
 *                       at most 1 MB): "ram LENGTH", a new line, the bytes.
 *   mem ADDRESS LENGTH  The same (sm2-emu's name): "mem LENGTH", the bytes.
 *   poke ADDRESS VALUE [SIZE]  Writes VALUE (hex, SIZE 1, 2 or 4 bytes, 4 by
 *                       default) at ADDRESS through the main CPU's bus: "ok".
 *   frame               "frame N", the number of frames run.
 *   tel                 The racing game's telemetry this frame, one JSON line
 *                       (the keys of TelemetryServer.h, as sm2-emu's tel), or
 *                       "tel none" for a game without a decoder.
 *   tel fields LIST|all The keys the next tel lines hold, as the telemetry
 *   tel others N|all    server's commands (Telemetry.md): "ok".
 *   settle [MS]         Waits MS milliseconds before each frame (0 by default),
 *                       so that a device set just before a step (vJoy) is read
 *                       in that frame even with -no-throttle; real-time pacing
 *                       leaves that time anyway. Answer: "settle MS".
 *   quit                "bye", and Supermodel exits.
 *
 * A command that fails answers "err" and a reason.
 */

#include "Agent.h"
#include "TelemetryServer.h"

#include "Supermodel.h"
#include "SDLIncludes.h"
#include "Graphics/SuperAA.h"
#include "Inputs/Inputs.h"
#include "Inputs/InputTypes.h"
#include "Model3/IEmulator.h"
#include "Model3/Model3.h"
#include "Model3/DriveBoard/DriveBoard.h"
#include "OSD/Outputs.h"
#include "Util/BMPFile.h"
#include "Util/NewConfig.h"

#include <GL/glew.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Agent
{
  static FILE *s_out = nullptr;           // the protocol's stdout
  static std::atomic<uint64_t> s_frame{ 0 };    // frames run
  static std::atomic<uint64_t> s_running{ 0 };  // the frame being run (or the last one), labels the journals
  static int s_settleMs = 0;                     // wait before each frame (settle)

  static bool s_enabled = false;
  static bool s_show = false;           // -agent-show: the window shown

  static void TakeStdout()
  {
    fflush(stdout);
#ifdef _WIN32
    int fd = _dup(_fileno(stdout));
    _setmode(fd, _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    s_out = _fdopen(fd, "wb");
    _dup2(_fileno(stderr), _fileno(stdout));
#else
    int fd = dup(fileno(stdout));
    s_out = fdopen(fd, "wb");
    dup2(fileno(stderr), fileno(stdout));
#endif
  }

  void Init(int &argc, char **argv)
  {
    int kept = 1;
    for (int i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-agent") == 0)
        s_enabled = true;
      else if (strcmp(argv[i], "-agent-show") == 0)
        s_enabled = s_show = true;
      else
        argv[kept++] = argv[i];
    }
    argc = kept;
    argv[argc] = nullptr;
    if (s_enabled)
      TakeStdout();
  }

  bool Enabled()
  {
    return s_enabled;
  }

  void AdjustConfig(Util::Config::Node &config)
  {
    if (!s_enabled)
      return;
    // The window is hidden and never has the focus (-agent-show: shown, to watch a race played by a program,
    // in real time with -throttle and heard with -sound)
    if (!s_show)
      config.Get("FullScreen").SetValue(false);
    config.Get("VSync").SetValue(false);
#ifdef SUPERMODEL_WIN32
    config.Get("BackgroundInput").SetValue(true);
#endif
  }

  uint32_t WindowFlags()
  {
    return s_enabled && !s_show ? SDL_WINDOW_HIDDEN : 0;
  }

  static void Reply(const char *format, ...)
  {
    va_list args;
    va_start(args, format);
    vfprintf(s_out, format, args);
    va_end(args);
    fputc('\n', s_out);
    fflush(s_out);
  }

  static void ReplyBytes(const std::string &header, const uint8_t *data, size_t length)
  {
    fputs(header.c_str(), s_out);
    fputc('\n', s_out);
    fwrite(data, 1, length, s_out);
    fflush(s_out);
  }

  /*
   * Outputs: the drive board bytes recorded, everything passed on to the configured outputs
   */

  class COutputsRecorder : public COutputs
  {
  public:
    COutputsRecorder(COutputs *next);
    bool Initialize() override;
    void Attached() override;
    std::vector<uint8_t> TakeDriveBytes();

  protected:
    void SendOutput(EOutputs output, UINT8 prevValue, UINT8 value) override;

  private:
    COutputs *m_next;
    std::mutex m_mutex;
    std::vector<uint8_t> m_driveBytes;
  };

  static std::unique_ptr<COutputsRecorder> s_recorder;

  COutputs *WrapOutputs(COutputs *outputs)
  {
    if (!s_enabled)
      return outputs;
    s_recorder = std::make_unique<COutputsRecorder>(outputs);
    return s_recorder.get();
  }

  COutputsRecorder::COutputsRecorder(COutputs *next)
    : m_next(next)
  {
  }

  bool COutputsRecorder::Initialize()
  {
    return true;
  }

  void COutputsRecorder::Attached()
  {
    if (m_next)
    {
      m_next->SetGame(GetGame());
      m_next->Attached();
    }
  }

  std::vector<uint8_t> COutputsRecorder::TakeDriveBytes()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint8_t> bytes;
    bytes.swap(m_driveBytes);
    return bytes;
  }

  void COutputsRecorder::SendOutput(EOutputs output, UINT8 prevValue, UINT8 value)
  {
    if (output == OutputRawDrive)
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_driveBytes.size() < 65536)
        m_driveBytes.push_back(value);
    }
    if (m_next)
      m_next->SetValue(output, value);
  }

  /*
   * Force feedback: every command sent to an input, from the drive board's thread
   */

  struct FFBRecord
  {
    uint64_t frame;
    const char *input;
    int effect;
    float force;
  };

  static std::mutex s_ffbMutex;
  static std::vector<FFBRecord> s_ffb;

  static void OnForceFeedback(const CInput &input, const ForceFeedbackCmd &ffCmd)
  {
    std::lock_guard<std::mutex> lock(s_ffbMutex);
    if (s_ffb.size() < 1000000)
      // Supermodel's constant force is > 0 to the right (towards the axis' maximum): the agent's is trigonometric
      s_ffb.push_back({ s_running.load(), input.id, int(ffCmd.id), ffCmd.id == FFConstantForce ? -ffCmd.force : ffCmd.force });
  }

  static const char *EffectName(int effect)
  {
    switch (effect)
    {
    case FFStop:          return "stop";
    case FFConstantForce: return "constant";
    case FFSelfCenter:    return "selfcenter";
    case FFFriction:      return "friction";
    case FFVibrate:       return "vibrate";
    default:              return "unknown";
    }
  }

  /*
   * The drive board's journal: its outputs at each NMI, from the drive board's thread
   */

  struct IORecord
  {
    uint64_t frame;
    uint64_t nmi;
    uint8_t port;
    uint8_t value;
  };

  static std::mutex s_ioMutex;
  static std::vector<IORecord> s_io;
  static int s_lastReply = -1;

  static void OnDriveBoardIO(uint64_t nmi, uint8_t port, uint8_t value)
  {
    std::lock_guard<std::mutex> lock(s_ioMutex);
    if (port == 0x29)
    {
      // The reply is written at every NMI: its changes only
      if (value == s_lastReply)
        return;
      s_lastReply = value;
    }
    if (s_io.size() < 4000000)
      s_io.push_back({ s_running.load(), nmi, port, value });
  }

  /*
   * Inputs held by the agent
   */

  struct Override
  {
    std::shared_ptr<CInput> input;
    UINT16 value;
  };

  static std::vector<Override> s_overrides;

  static const char *InputType(const CInput *input)
  {
    if (dynamic_cast<const CAxisInput *>(input))
      return "axis";
    if (dynamic_cast<const CAnalogInput *>(input))
      return "analog";
    if (dynamic_cast<const CSwitchInput *>(input))
      return "switch";
    return "other";
  }

  // From the agent's value (-1..1 for an axis, +1 at its left or minimum end; 0..1 for a pedal; 0 or 1 for a button)
  // to the value the game reads
  static bool ToRaw(const CInput *input, double v, UINT16 &raw)
  {
    double r;
    if (auto *axis = dynamic_cast<const CAxisInput *>(input))
    {
      v = std::clamp(v, -1.0, 1.0);
      double off = axis->GetOffValue();
      r = v >= 0 ? off + v * (axis->GetMinValue() - off) : off - v * (axis->GetMaxValue() - off);
    }
    else if (auto *analog = dynamic_cast<const CAnalogInput *>(input))
    {
      v = std::clamp(v, 0.0, 1.0);
      r = analog->GetMinValue() + v * (analog->GetMaxValue() - analog->GetMinValue());
    }
    else if (auto *button = dynamic_cast<const CSwitchInput *>(input))
      r = v >= 0.5 ? button->GetOnValue() : button->GetOffValue();
    else
      return false;
    raw = UINT16(std::lround(r));
    return true;
  }

  static void Hold(const std::shared_ptr<CInput> &input, UINT16 value)
  {
    for (auto &o : s_overrides)
    {
      if (o.input == input)
      {
        o.value = value;
        return;
      }
    }
    s_overrides.push_back({ input, value });
  }

  static void ApplyOverrides()
  {
    for (auto &o : s_overrides)
      o.input->value = o.value;
  }

  /*
   * Frames
   */

  // RGBA bottom row first, w x h, to RGB top row first, W x H, each pixel the average of its box
  static void Scale(const std::vector<uint8_t> &rgba, int w, int h, int W, int H, std::vector<uint8_t> &rgb)
  {
    rgb.assign(size_t(W) * size_t(H) * 3, 0);
    for (int y = 0; y < H; y++)
    {
      int y0 = int(int64_t(y) * h / H);
      int y1 = std::max(y0 + 1, int(int64_t(y + 1) * h / H));
      for (int x = 0; x < W; x++)
      {
        int x0 = int(int64_t(x) * w / W);
        int x1 = std::max(x0 + 1, int(int64_t(x + 1) * w / W));
        unsigned sum[3] = { 0, 0, 0 };
        unsigned n = 0;
        for (int yy = y0; yy < y1; yy++)
        {
          const uint8_t *row = &rgba[size_t(h - 1 - yy) * size_t(w) * 4];
          for (int xx = x0; xx < x1; xx++)
          {
            sum[0] += row[xx * 4 + 0];
            sum[1] += row[xx * 4 + 1];
            sum[2] += row[xx * 4 + 2];
            n++;
          }
        }
        uint8_t *out = &rgb[(size_t(y) * size_t(W) + size_t(x)) * 3];
        for (int c = 0; c < 3; c++)
          out[c] = uint8_t(sum[c] / n);
      }
    }
  }

  // The last frame, from the frame buffer the renderers draw into (SuperAA's, forced in agent mode): RGBA, bottom row
  // first
  static bool ReadFrame(SuperAA *superAA, std::vector<uint8_t> &rgba, int &width, int &height)
  {
    GLuint fbo = superAA ? superAA->GetTargetID() : 0;
    if (fbo == 0)
      return false;
    GLint previousRead = 0, previousTexture = 0, texture = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &texture);
    glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glBindTexture(GL_TEXTURE_2D, GLuint(previousTexture));
    rgba.resize(size_t(width) * size_t(height) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousRead));
    return width > 0 && height > 0;
  }

  // Real-time pacing when Throttle is on: waits for the time of the next frame (as Main.cpp's loop does)
  class CPacer
  {
  public:
    CPacer(const Util::Config::Node &config)
      : m_config(config),
        m_frequency(SDL_GetPerformanceFrequency()),
        m_next(0)
    {
    }

    void Wait()
    {
      if (!m_config["Throttle"].ValueAs<bool>())
        return;
      double hz = std::abs(m_config["RefreshRate"].ValueAs<float>());
      uint64_t perFrame = uint64_t(double(m_frequency) / (hz > 1.0 ? hz : 57.524));
      uint64_t now = SDL_GetPerformanceCounter();
      while (now < m_next)
      {
        if ((m_next - now) * 1000 > 2 * m_frequency)
          SDL_Delay(1);
        now = SDL_GetPerformanceCounter();
      }
      m_next = now + perFrame;
    }

  private:
    const Util::Config::Node &m_config;
    uint64_t m_frequency;
    uint64_t m_next;
  };

  static std::string RestOfLine(std::istringstream &in)
  {
    std::string rest;
    std::getline(in, rest);
    size_t start = rest.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : rest.substr(start);
  }

  void Run(IEmulator *emulator, CInputs *inputs, SuperAA *superAA, const Util::Config::Node &config,
           std::function<bool()> pollInputs,
           std::function<bool(const std::string &path)> saveState,
           std::function<bool(const std::string &path)> loadState)
  {
    if (!s_enabled || s_out == nullptr || !s_recorder)
    {
      ErrorLog("Agent mode was not set up.");
      return;
    }
    COutputsRecorder *outputs = s_recorder.get();
    CPacer pacer(config);

    CInput::SetForceFeedbackObserver(OnForceFeedback);
    CModel3 *model3 = dynamic_cast<CModel3 *>(emulator);
    const Game &game = emulator->GetGame();
    uint32_t gameInputs = game.inputs;

    std::vector<uint8_t> frame;
    int width = 0, height = 0;
    ReadFrame(superAA, frame, width, height);
    outputs->TakeDriveBytes();
    Reply("ready %s %d %d", game.name.c_str(), width, height);

    bool running = true;
    char line[4096];
    while (running && fgets(line, sizeof(line), stdin))
    {
      std::istringstream in(line);
      std::string command;
      in >> command;
      if (command.empty())
        continue;

      if (command == "step")
      {
        long n = 1;
        in >> n;
        if (n < 1)
          n = 1;
        for (long i = 0; i < n; i++)
        {
          if (s_settleMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(s_settleMs));
          if (!pollInputs())
          {
            running = false;
            break;
          }
          ApplyOverrides();
          s_running = s_frame + 1;  // what the drive board sends during this frame is labelled with its number
          emulator->RunFrame();
          s_frame = s_running.load();
          Telemetry::Service().OnFrame(s_frame);
          pacer.Wait();
        }
        std::string reply = "ok " + std::to_string(s_frame.load());
        char hex[4];
        for (uint8_t b : outputs->TakeDriveBytes())
        {
          snprintf(hex, sizeof(hex), " %02X", b);
          reply += hex;
        }
        Reply("%s", reply.c_str());
      }
      else if (command == "set" || command == "setraw")
      {
        std::string name;
        double value = 0;
        in >> name >> value;
        if (!in)
        {
          Reply("err usage: %s INPUT VALUE", command.c_str());
          continue;
        }
        auto input = (*inputs)[name.c_str()];
        if (!input || input->IsUIInput())
        {
          Reply("err unknown input %s", name.c_str());
          continue;
        }
        UINT16 raw;
        if (command == "setraw")
          raw = UINT16(std::lround(value));
        else if (!ToRaw(input.get(), value, raw))
        {
          Reply("err %s is not an axis, a pedal or a button: use setraw", name.c_str());
          continue;
        }
        Hold(input, raw);
        Reply("ok");
      }
      else if (command == "get")
      {
        std::string name;
        in >> name;
        auto input = (*inputs)[name.c_str()];
        if (!input || input->IsUIInput())
          Reply("err unknown input %s", name.c_str());
        else
          Reply("value %u", unsigned(input->value));
      }
      else if (command == "servo")
      {
        std::string mode;
        in >> mode;
        if (mode == "on" || mode == "off")
        {
          {
            std::lock_guard<std::mutex> lock(s_ioMutex);
            s_io.clear();
            s_lastReply = -1;
          }
          CDriveBoard::SetIOObserver(mode == "on" ? OnDriveBoardIO : nullptr);
          Reply("ok");
        }
        else
        {
          std::vector<IORecord> records;
          {
            std::lock_guard<std::mutex> lock(s_ioMutex);
            records.swap(s_io);
          }
          std::string text = "servo " + std::to_string(records.size()) + "\n";
          char item[64];
          for (auto &r : records)
          {
            snprintf(item, sizeof(item), "%llu %llu %02X %02X\n", (unsigned long long)r.frame, (unsigned long long)r.nmi,
              r.port, r.value);
            text += item;
          }
          fputs(text.c_str(), s_out);
          fflush(s_out);
        }
      }
      else if (command == "release")
      {
        std::string name;
        in >> name;
        if (name == "all")
          s_overrides.clear();
        else
        {
          auto input = (*inputs)[name.c_str()];
          s_overrides.erase(std::remove_if(s_overrides.begin(), s_overrides.end(),
            [&](const Override &o) { return o.input == input; }), s_overrides.end());
        }
        Reply("ok");
      }
      else if (command == "inputs")
      {
        std::vector<std::shared_ptr<CInput>> list;
        for (unsigned i = 0; i < inputs->Count(); i++)
        {
          auto input = (*inputs)[i];
          if (input && !input->IsUIInput() && (input->gameFlags & gameInputs))
            list.push_back(input);
        }
        Reply("inputs %zu", list.size());
        for (auto &input : list)
          Reply("%s %s", input->id, InputType(input.get()));
      }
      else if (command == "obs")
      {
        int W = 0, H = 0;
        in >> W >> H;
        if (!ReadFrame(superAA, frame, width, height))
        {
          Reply("err no frame buffer");
          continue;
        }
        if (W <= 0 || H <= 0)
        {
          W = width;
          H = height;
        }
        std::vector<uint8_t> rgb;
        Scale(frame, width, height, W, H, rgb);
        ReplyBytes("obs " + std::to_string(W) + " " + std::to_string(H), rgb.data(), rgb.size());
      }
      else if (command == "shot")
      {
        std::string path = RestOfLine(in);
        if (path.empty() || !ReadFrame(superAA, frame, width, height))
          Reply("err no path or no frame buffer");
        else if (Util::WriteSurfaceToBMP<Util::RGBA8>(path, frame.data(), width, height, true))
          Reply("err cannot write %s", path.c_str());
        else
          Reply("ok");
      }
      else if (command == "save" || command == "load")
      {
        std::string path = RestOfLine(in);
        bool done = !path.empty() && (command == "save" ? saveState(path) : loadState(path));
        if (done)
        {
          outputs->TakeDriveBytes();
          // The telemetry of the state loaded, not of the last frame run before it (tel answers at once)
          if (command == "load")
            Telemetry::Service().OnFrame(s_frame);
          Reply("ok");
        }
        else
          Reply("err cannot %s %s", command.c_str(), path.c_str());
      }
      else if (command == "ffb")
      {
        std::vector<FFBRecord> records;
        {
          std::lock_guard<std::mutex> lock(s_ffbMutex);
          records.swap(s_ffb);
        }
        Reply("ffb %zu", records.size());
        for (auto &r : records)
          Reply("%llu %s %s %.4f", (unsigned long long)r.frame, r.input, EffectName(r.effect), r.force);
      }
      else if (command == "outputs")
      {
        std::string reply = "outputs";
        char item[64];
        for (int i = 0; i < NUM_OUTPUTS; i++)
        {
          EOutputs output = EOutputs(i);
          if (outputs->HasValue(output))
          {
            snprintf(item, sizeof(item), " %s=%02X", COutputs::GetOutputName(output), outputs->GetValue(output));
            reply += item;
          }
        }
        Reply("%s", reply.c_str());
      }
      else if (command == "ram" || command == "mem")  // mem: sm2-emu's name, by address too
      {
        unsigned address = 0, length = 0;
        in >> std::hex >> address >> length;
        if (!in || model3 == nullptr || length == 0 || length > 0x100000)
        {
          Reply("err usage: %s ADDRESS LENGTH (hex, at most 100000)", command.c_str());
          continue;
        }
        std::vector<uint8_t> bytes(length);
        for (unsigned i = 0; i < length; i++)
          bytes[i] = model3->Read8(address + i);
        ReplyBytes(command + " " + std::to_string(length), bytes.data(), bytes.size());
      }
      else if (command == "poke")
      {
        // A value written by the main CPU's bus (the autopilot's tools: a race's clock held while lines are made)
        unsigned address = 0, value = 0, size = 4;
        bool parsed = bool(in >> std::hex >> address >> value);
        if (parsed && !(in >> std::dec >> size))
          size = 4;
        if (!parsed || model3 == nullptr || (size != 1 && size != 2 && size != 4))
        {
          Reply("err usage: poke ADDRESS VALUE [SIZE] (hex address and value, size 1, 2 or 4)");
          continue;
        }
        if (size == 1)
          model3->Write8(address, UINT8(value));
        else if (size == 2)
          model3->Write16(address, UINT16(value));
        else
          model3->Write32(address, value);
        Reply("ok");
      }
      else if (command == "settle")
      {
        int ms = s_settleMs;
        if (in >> ms)
          s_settleMs = std::clamp(ms, 0, 1000);
        Reply("settle %d", s_settleMs);
      }
      else if (command == "frame")
        Reply("frame %llu", (unsigned long long)s_frame.load());
      else if (command == "tel")
      {
        static Telemetry::Selection selection;
        std::string what, arg;
        in >> what;
        arg = RestOfLine(in);
        if (what.empty())
          Reply("%s", Telemetry::Service().Json(selection).c_str());
        else if (what == "fields")
        {
          selection.fields.clear();
          std::stringstream list(arg == "all" ? std::string() : arg);
          std::string f;
          while (std::getline(list, f, ','))
          {
            f.erase(std::remove_if(f.begin(), f.end(), [](char c) { return c == ' ' || c == '\t'; }), f.end());
            if (!f.empty())
              selection.fields.push_back(f);
          }
          Reply("ok");
        }
        else if (what == "others")
        {
          selection.others = arg == "all" ? -1 : atoi(arg.c_str());
          Reply("ok");
        }
        else
          Reply("err usage: tel | tel fields LIST|all | tel others N|all");
      }
      else if (command == "quit")
      {
        Reply("bye");
        running = false;
      }
      else
        Reply("err unknown command %s", command.c_str());
    }

    CInput::SetForceFeedbackObserver(nullptr);
    CDriveBoard::SetIOObserver(nullptr);
  }
}
