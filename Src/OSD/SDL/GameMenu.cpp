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
 * GameMenu.cpp
 *
 * The in-game menu, drawn with Dear ImGui over the game's last frame in the
 * game's own window and OpenGL context (created the first time it opens).
 * Keyboard navigation for a cabinet without a mouse: arrows, Space or Enter,
 * Escape to close; Page Up and Page Down scroll the game's notes. Its Display
 * and Controls pages follow linuxloader's (and windowsloader's), whose
 * Controls page is organised as Flycast's.
 */

#include "GameMenu.h"
#include "Game.h"
#include "Main.h"
#include "OSD/Outputs.h"
#include "Graphics/AspectRatio.h"
#include "Inputs/Inputs.h"
#include "Inputs/Input.h"
#include "Inputs/InputSystem.h"
#include "Inputs/InputTypes.h"
#include "Util/NewConfig.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace GameMenu
{
  namespace
  {
    struct Note
    {
      enum class Kind { Heading, Paragraph, Bullet };
      Kind kind;
      std::string text;
    };

    bool s_initialized = false;
    bool s_open = false;
    bool s_closeRequested = false;
    unsigned s_changes = 0;
    Action s_action = Action::None;
    std::string s_status;
    int s_cursor = SDL_ENABLE;    // the mouse cursor's visibility before it opened
    bool s_selectPage = false;
    Page s_page = Page::Display;

    // The Display page
    struct Size
    {
      unsigned width, height;
    };
    std::vector<Size> s_resolutions;            // the screen's modes and Model 3 multiples that fit it, smallest first
    unsigned s_renderWidth = 0, s_renderHeight = 0;

    // The Controls page: the device whose page is shown, and the input being bound to one of its inputs
    struct Device
    {
      enum class Kind { Keyboard, Mouse, Joystick };
      Kind kind = Kind::Keyboard;
      int index = 0;                            // the joystick's, from 0
      std::string name;
    };
    CInputs *s_inputs = nullptr;
    Device s_device;
    bool s_mappingRequest = false;              // its page opens at the next frame
    bool s_mappingOpen = false;
    bool s_mappingClose = false;                // Escape closes it
    bool s_capturing = false;
    std::shared_ptr<CInput> s_captureInput;
    std::vector<std::string> s_changedBindings;

    // The Game page
    std::vector<std::pair<std::string, std::string>> s_info;  // label, value
    std::vector<std::string> s_noteNames;                     // the game's ROM set, then its parent's
    std::string s_notesFile;
    std::vector<Note> s_notes;

    // The Outputs page
    Game s_game;
    COutputs *s_outputs = nullptr;
  }

  static std::string Trim(const std::string &s)
  {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      return std::string();
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
  }

  static std::string Lower(std::string s)
  {
    for (char &c : s)
      c = char(std::tolower((unsigned char)c));
    return s;
  }

  // The notes of the first of the names that a section of the file is for (all its sections, in order). The format:
  // a section [name] or [name1, name2, ...] for those ROM sets; then paragraphs, separated by blank lines; "## Title"
  // a heading, "- text" a bullet point, a line that follows without a blank line continues the paragraph or point;
  // ";" at the start of a line a comment
  static std::vector<Note> ReadNotes(const std::string &file, const std::vector<std::string> &names)
  {
    struct Section
    {
      std::vector<std::string> names;
      std::vector<std::string> lines;
    };
    std::vector<Section> sections;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line))
    {
      std::string text = Trim(line);
      if (text.size() >= 2 && text.front() == '[' && text.back() == ']')
      {
        sections.emplace_back();
        std::istringstream list(text.substr(1, text.size() - 2));
        for (std::string name; std::getline(list, name, ',');)
          sections.back().names.push_back(Lower(Trim(name)));
      }
      else if (!sections.empty() && (text.empty() || text[0] != ';'))
        sections.back().lines.push_back(text);
    }

    for (const std::string &name : names)
    {
      std::vector<Note> notes;
      for (const Section &section : sections)
      {
        if (name.empty() || std::find(section.names.begin(), section.names.end(), Lower(name)) == section.names.end())
          continue;
        bool open = false;  // the last note goes on with the next line
        for (const std::string &text : section.lines)
        {
          if (text.empty())
            open = false;
          else if (text.compare(0, 3, "## ") == 0)
          {
            notes.push_back({ Note::Kind::Heading, Trim(text.substr(3)) });
            open = false;
          }
          else if (text.compare(0, 2, "- ") == 0)
          {
            notes.push_back({ Note::Kind::Bullet, Trim(text.substr(2)) });
            open = true;
          }
          else if (open)
            notes.back().text += " " + text;
          else
          {
            notes.push_back({ Note::Kind::Paragraph, text });
            open = true;
          }
        }
      }
      if (!notes.empty())
        return notes;
    }
    return std::vector<Note>();
  }

  void SetGame(const Game &game, const std::string &notesFile)
  {
    static const char *kDriveBoards[] = { "None", "Wheel", "Joystick", "Ski", "Billboard" };

    s_info.clear();
    s_info.push_back({ "Title", game.version.empty() ? game.title : game.title + " (" + game.version + ")" });
    s_info.push_back({ "ROM set", game.parent.empty() ? game.name : game.name + " (clone of " + game.parent + ")" });
    if (!game.manufacturer.empty() || game.year)
    {
      std::string made = game.manufacturer;
      if (game.year)
        made += (made.empty() ? "" : ", ") + std::to_string(game.year);
      s_info.push_back({ "Made by", made });
    }
    s_info.push_back({ "Hardware", Game::SteppingName(game.stepping) });
    if (unsigned(game.driveboard_type) < sizeof(kDriveBoards) / sizeof(kDriveBoards[0]))
      s_info.push_back({ "Drive board", kDriveBoards[game.driveboard_type] });
    s_info.push_back({ "Network board", game.netboard_present ? "Yes (to link cabinets)" : "No" });

    s_noteNames = { game.name, game.parent };
    s_notesFile = notesFile;
    s_notes.clear();
    s_game = game;
  }

  void SetOutputs(COutputs *outputs)
  {
    s_outputs = outputs;
  }

  void SetInputs(CInputs *inputs)
  {
    s_inputs = inputs;
  }

  void SetRenderSize(unsigned width, unsigned height)
  {
    s_renderWidth = width;
    s_renderHeight = height;
  }

  // The resolutions offered: the modes of the window's screen, and Model 3's 496 x 384 times 1 to 4 that fit it
  static void ListResolutions(SDL_Window *window)
  {
    s_resolutions.clear();
    int display = std::max(0, SDL_GetWindowDisplayIndex(window));
    SDL_DisplayMode desktop{};
    bool known = SDL_GetDesktopDisplayMode(display, &desktop) == 0;
    for (int i = 0; i < SDL_GetNumDisplayModes(display); i++)
    {
      SDL_DisplayMode mode;
      if (SDL_GetDisplayMode(display, i, &mode) == 0 && mode.w > 0 && mode.h > 0)
        s_resolutions.push_back({ unsigned(mode.w), unsigned(mode.h) });
    }
    for (unsigned n = 1; n <= 4; n++)
    {
      if (!known || (496 * n <= unsigned(desktop.w) && 384 * n <= unsigned(desktop.h)))
        s_resolutions.push_back({ 496 * n, 384 * n });
    }
    std::sort(s_resolutions.begin(), s_resolutions.end(), [](const Size &a, const Size &b)
    {
      return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    s_resolutions.erase(std::unique(s_resolutions.begin(), s_resolutions.end(), [](const Size &a, const Size &b)
    {
      return a.width == b.width && a.height == b.height;
    }), s_resolutions.end());
  }

  static void EndCapture()
  {
    if (s_capturing && s_inputs)
      s_inputs->GetInputSystem()->EndMappingRead();
    s_capturing = false;
    s_captureInput = nullptr;
  }

  bool IsCapturing()
  {
    return s_capturing;
  }

  // Sized for the window: legible on a cabinet's 1080p or 4K screen; again when the window changes (display mode)
  static void Scale(float windowHeight)
  {
    static float scaledFor = 0.0f;
    if (windowHeight == scaledFor)
      return;
    scaledFor = windowHeight;
    float scale = std::max(1.0f, windowHeight / 720.0f);
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
  }

  bool Open(SDL_Window *window)
  {
    if (!s_initialized)
    {
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImGuiIO &io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.IniFilename = nullptr;
      io.Fonts->AddFontDefaultVector();
      if (!ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext()))
      {
        ImGui::DestroyContext();
        return false;
      }
      if (!ImGui_ImplOpenGL3_Init("#version 410"))
      {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
      }
      s_initialized = true;
    }

    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    Scale(float(height));

    s_cursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    s_open = true;
    s_closeRequested = false;
    s_status.clear();
    ListResolutions(window);
    if (!s_notesFile.empty())
      s_notes = ReadNotes(s_notesFile, s_noteNames);  // each time: changes to the file show without a restart
    return true;
  }

  void SelectPage(Page page)
  {
    s_page = page;
    s_selectPage = true;
  }

  void Close()
  {
    if (!s_open)
      return;
    EndCapture();
    s_mappingRequest = false;
    s_open = false;
    SDL_ShowCursor(s_cursor);
  }

  bool IsOpen()
  {
    return s_open;
  }

  bool CloseRequested()
  {
    return s_closeRequested;
  }

  bool ProcessEvents()
  {
    bool running = true;
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
      if (e.type == SDL_QUIT)
        running = false;
      const bool escape = e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE;
      if (s_capturing)
      {
        // An input being bound: Escape cancels; the keys (the mouse's buttons and moves) pressed for it are not the
        // menu's. Their releases still go to the menu, which saw them pressed before
        if (escape)
        {
          if (!e.key.repeat)
            EndCapture();
          continue;
        }
        bool key = e.type == SDL_KEYDOWN || e.type == SDL_TEXTINPUT;
        bool mouse = e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL;
        if ((key && s_device.kind == Device::Kind::Keyboard) || (mouse && s_device.kind == Device::Kind::Mouse))
          continue;
      }
      else if (escape && !e.key.repeat)
      {
        // Escape closes a device's page, else the menu, unless it closes a list first
        if (s_mappingOpen && !ImGui::IsPopupOpen("##reset"))
        {
          s_mappingClose = true;
          continue;
        }
        if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
          s_closeRequested = true;
          continue;
        }
      }
      ImGui_ImplSDL2_ProcessEvent(&e);
    }
    return running;
  }

  static int Slider(Util::Config::Node &config, const char *label, const char *key, int fallback, int min, int max, const char *format)
  {
    int value = std::clamp(config[key].ValueAsDefault<int>(fallback), min, max);
    if (ImGui::SliderInt(label, &value, min, max, format))
    {
      config.Set(key, value);
      s_changes |= ChangeLayout;
    }
    return value;
  }

  static void Wrapped(const char *text)
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
  }

  static void DrawDisplayPage(Util::Config::Node &config);

  // --- What the game sends on the outputs (CModel3::WriteInputs, CBillBoard, CNetOutputs), as linuxloader shows it

  namespace
  {
    struct OutputLine
    {
      std::string name, type, meaning, value;
    };

    // The lamps each game lights on its lamp port (CModel3::WriteInputs, register 0x14), bit 0 first, as seen in its
    // attract mode and in a race with each view button pressed in turn; nullptr: that bit never lit. A lamp's use is
    // given only where it is known (the start lamp blinks with "press start", Daytona USA 2's VR buttons light their
    // own); the others say where they lit. The games not listed were not checked: no lamp is shown for them
    struct GameLamps
    {
      const char *game;   // the ROM set, or its clones' parent
      const char *bits[8];
    };

    const char kStart[] = "Start button lamp";
    const char kInAttract[] = "Lamp, lit in the attract mode";
    const char kInRace[] = "Lamp, lit in the race";
    const GameLamps kGameLamps[] =
    {
      { "daytona2", { nullptr, nullptr, kStart, "VR1 view button lamp", "VR2 view button lamp", "VR3 view button lamp", "VR4 view button lamp", kInAttract } },
      { "dayto2pe", { nullptr, nullptr, kStart, "VR1 view button lamp", "VR2 view button lamp", "VR3 view button lamp", "VR4 view button lamp", kInAttract } },
      { "scud",     { nullptr, nullptr, kStart, nullptr, nullptr, kInRace, nullptr, kInAttract } },
      { "srally2",  { nullptr, nullptr, kStart, kInRace, nullptr, nullptr, nullptr, kInAttract } },
      { "lemans24", { nullptr, nullptr, kStart, nullptr, nullptr, nullptr, nullptr, kInAttract } },
      { "dirtdvls", { nullptr, nullptr, kStart, kInRace, nullptr, nullptr, nullptr, nullptr } },
      { "eca",      { nullptr, nullptr, kStart, kInRace, kInRace, kInRace, kInRace, "Lamp" } },
      { "harley",   { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "Lamp, lit in the attract mode and the race" } },
    };

    // The lamp port's bits sent as outputs of their own
    const EOutputs kLampOutputs[8] = { OutputUnknown, OutputUnknown, OutputLampStart, OutputLampView1, OutputLampView2,
                                       OutputLampView3, OutputLampView4, OutputLampLeader };
  }

  static const GameLamps *FindGameLamps(const Game &game)
  {
    for (const GameLamps &lamps : kGameLamps)
    {
      if (game.name == lamps.game || game.parent == lamps.game)
        return &lamps;
    }
    return nullptr;
  }

  void DrawOutputsTable(const Util::Config::Node &config, const Game &game, COutputs *outputs, bool inGame)
  {
    ImGui::TextUnformatted("What this game sends");
    if (game.name.empty())
    {
      ImGui::TextDisabled("Select a game in the list to see its outputs.");
      return;
    }
    const bool network = config["Outputs"].ValueAsDefault<std::string>("none") == "net";
    auto value = [outputs](EOutputs output)
    {
      if (outputs == nullptr || !outputs->HasValue(output))
        return std::string("-");
      char text[8];
      snprintf(text, sizeof(text), output == OutputM3ffb ? "%02X" : "%u", unsigned(outputs->GetValue(output)));
      return std::string(text);
    };

    std::vector<OutputLine> lines;
    lines.push_back({ "mame_start", "text", "The game's name, the first line a client gets", outputs ? game.name : "-" });
    lines.push_back({ "tcp", "number", "The TCP port, in the UDP broadcast announce",
                      outputs && network ? std::to_string(config["OutputsTCPPort"].ValueAsDefault<unsigned>(8000)) : "-" });
    lines.push_back({ "pause", "0 or 1", "1 while the emulation is paused", value(OutputPause) });

    // The cabinet's lamps, those this game lights
    if (const GameLamps *lamps = FindGameLamps(game))
    {
      lines.push_back({ "RawLamps", "byte, 0 to 255", "The lamp port, one bit per lamp (the lines below)", value(OutputRawLamps) });
      for (int bit = 2; bit < 8; bit++)
      {
        if (lamps->bits[bit])
          lines.push_back({ COutputs::GetOutputName(kLampOutputs[bit]), "0 or 1",
                            std::string(lamps->bits[bit]) + " (bit " + std::to_string(bit) + ")", value(kLampOutputs[bit]) });
      }
    }

    // The drive board's commands, for a game that has one
    const char *board = nullptr;
    switch (game.driveboard_type)
    {
    case Game::DRIVE_BOARD_WHEEL:     board = "The force feedback board's commands"; break;
    case Game::DRIVE_BOARD_JOYSTICK:  board = "The joystick board's commands"; break;
    case Game::DRIVE_BOARD_SKI:       board = "The ski board's commands"; break;
    case Game::DRIVE_BOARD_BILLBOARD: board = "The bill board's commands"; break;
    default: break;
    }
    if (board)
    {
      lines.push_back({ "RawDrive", "byte, 0 to 255", std::string(board) + ", one line each, up to 4 per frame", value(OutputRawDrive) });
      lines.push_back({ "m3ffb", "hex, 00 to FF", "The same byte in hexadecimal (BackForceFeeder's raw mode)", value(OutputM3ffb) });
    }

    // The fighting games' bill board: the win counters above the cabinet, 7-segment digits (bits hgfedcba, 0 lights)
    if (game.driveboard_type == Game::DRIVE_BOARD_BILLBOARD)
    {
      lines.push_back({ "BillDigit1", "byte, 0 to 255", "Player 1's first digit (segments, 0 lights)", value(OutputBill1) });
      lines.push_back({ "BillDigit2", "byte, 0 to 255", "Player 1's second digit (segments, 0 lights)", value(OutputBill2) });
      lines.push_back({ "BillDigit3", "byte, 0 to 255", "Player 2's first digit (segments, 0 lights)", value(OutputBill3) });
      lines.push_back({ "BillDigit4", "byte, 0 to 255", "Player 2's second digit (segments, 0 lights)", value(OutputBill4) });
      lines.push_back({ "BillDigit5", "byte, 0 to 255", "Its lamps: bit 0 player 1, bit 1 player 2 (0 lights)", value(OutputBill5) });
    }

    lines.push_back({ "mame_stop", "1", "When the game exits", "-" });

    const float height = ImGui::GetTextLineHeightWithSpacing() * 10.5f;
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##outputs", 4, flags, ImVec2(0.0f, height)))
    {
      ImGui::TableSetupScrollFreeze(0, 1);  // the header stays
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Meaning", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableHeadersRow();
      for (const OutputLine &line : lines)
      {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(line.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(line.type.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(line.meaning.c_str());
        ImGui::SetItemTooltip("%s", line.meaning.c_str());  // whole, when the column cuts it
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(line.value.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Each line goes as \"name = value\" on the TCP port, at each change (m3ffb in hexadecimal, the others "
                        "in decimal); a client that connects gets every known value first. Value: the last sent.");
    if (!inGame)
      ImGui::TextDisabled("The values show in the in-game menu (Tab), on its Outputs page, while the game runs.");
    else if (outputs == nullptr)
      ImGui::TextDisabled("The values show while the outputs are on (Misc > Outputs in the launcher, or -outputs=net, "
                          "from the next start).");
    ImGui::PopTextWrapPos();
  }

  static void DrawOutputsPage(const Util::Config::Node &config)
  {
    const std::string mode = config["Outputs"].ValueAsDefault<std::string>("none");
    if (mode == "net")
      ImGui::Text("Network outputs on: TCP port %u, UDP announce port %u", config["OutputsTCPPort"].ValueAsDefault<unsigned>(8000),
                  config["OutputsUDPBroadcastPort"].ValueAsDefault<unsigned>(8001));
    else if (mode == "win")
      ImGui::TextUnformatted("Outputs on, as Windows messages");
    else
      ImGui::TextUnformatted("Outputs off");
    ImGui::Spacing();
    DrawOutputsTable(config, s_game, s_outputs, true);
  }

  static void DrawGamePage()
  {
    if (ImGui::BeginTable("Description", 2, ImGuiTableFlags_SizingFixedFit))
    {
      for (const auto &[label, value] : s_info)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(value.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
    if (s_notes.empty())
    {
      ImGui::TextDisabled("No notes for this game in %s", s_notesFile.c_str());
      return;
    }

    // The notes, in at most half the screen's height: scrolled with Page Up and Page Down, or the mouse wheel
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.5f));
    bool scrolls = false;
    if (ImGui::BeginChild("Notes", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
      float page = ImGui::GetWindowHeight() * 0.8f;
      if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        ImGui::SetScrollY(ImGui::GetScrollY() + page);
      if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        ImGui::SetScrollY(ImGui::GetScrollY() - page);
      for (size_t i = 0; i < s_notes.size(); i++)
      {
        const Note &note = s_notes[i];
        bool afterHeading = i == 0 || s_notes[i - 1].kind == Note::Kind::Heading;
        switch (note.kind)
        {
        case Note::Kind::Heading:
          if (i > 0)
            ImGui::Spacing();
          ImGui::SeparatorText(note.text.c_str());
          break;
        case Note::Kind::Paragraph:
          if (!afterHeading)
            ImGui::Spacing();
          ImGui::TextWrapped("%s", note.text.c_str());
          break;
        case Note::Kind::Bullet:
          if (!afterHeading && s_notes[i - 1].kind == Note::Kind::Paragraph)
            ImGui::Spacing();
          ImGui::Bullet();
          ImGui::SameLine();
          ImGui::TextWrapped("%s", note.text.c_str());
          break;
        }
      }
      scrolls = ImGui::GetScrollMaxY() > 0.0f;
    }
    ImGui::EndChild();
    if (scrolls)
      ImGui::TextDisabled("Page Up / Page Down: scroll the notes");
  }

  // --- The Controls page, as linuxloader's (organised as Flycast's): the devices found, each with a page that binds
  // the game's inputs to its own; an input's binding (InputStart1 = KEY_1,JOY1_BUTTON9) holds all the devices'

  // "KEY_1,JOY1_BUTTON9" -> {"KEY_1", "JOY1_BUTTON9"}: the alternatives of a binding
  static std::vector<std::string> SplitBinding(const std::string &mapping)
  {
    std::vector<std::string> tokens;
    std::istringstream list(mapping);
    for (std::string token; std::getline(list, token, ',');)
    {
      token = Trim(token);
      if (!token.empty() && Lower(token) != "none")
        tokens.push_back(token);
    }
    return tokens;
  }

  static std::string JoinBinding(const std::vector<std::string> &tokens)
  {
    std::string mapping;
    for (const std::string &token : tokens)
      mapping += (mapping.empty() ? "" : ",") + token;
    return mapping.empty() ? "NONE" : mapping;
  }

  // Whether an alternative of a binding is the device's: its first input is one of the device's (JOY_..., any
  // joystick, is every joystick's)
  static bool Owns(const Device &device, const std::string &token)
  {
    size_t start = token.compare(0, 1, "!") == 0 ? 1 : 0;
    size_t plus = token.find('+');
    std::string first = Lower(token.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
    switch (device.kind)
    {
    case Device::Kind::Keyboard:
      return first.compare(0, 3, "key") == 0;
    case Device::Kind::Mouse:
      return first.compare(0, 5, "mouse") == 0;
    default:
    {
      std::string prefix = "joy" + std::to_string(device.index + 1) + "_";
      return first.compare(0, 4, "joy_") == 0 || first.compare(0, prefix.size(), prefix) == 0;
    }
    }
  }

  static std::vector<std::string> DeviceTokens(const Device &device, const std::string &mapping, bool owned)
  {
    std::vector<std::string> tokens;
    for (const std::string &token : SplitBinding(mapping))
    {
      if (Owns(device, token) == owned)
        tokens.push_back(token);
    }
    return tokens;
  }

  // The prefix of the device's inputs in the bindings
  static std::string DevicePrefix(const Device &device)
  {
    switch (device.kind)
    {
    case Device::Kind::Keyboard: return "KEY_";
    case Device::Kind::Mouse:    return "MOUSE_";
    default:                     return "JOY" + std::to_string(device.index + 1) + "_";
    }
  }

  // An input as the page shows it: "Left", "X axis +", "Button 3", "POV 1 up"... (the binding's own words, tidied)
  static std::string DescribePart(std::string part)
  {
    bool negated = !part.empty() && part[0] == '!';
    if (negated)
      part.erase(0, 1);
    size_t underscore = part.find('_');
    std::string name = underscore == std::string::npos ? part : part.substr(underscore + 1);
    for (char &c : name)
      c = char(std::toupper((unsigned char)c));

    auto takeEnd = [&name](const char *end)
    {
      size_t n = std::strlen(end);
      if (name.size() <= n || name.compare(name.size() - n, n, end) != 0)
        return false;
      name.erase(name.size() - n);
      return true;
    };
    std::string suffix;
    if (takeEnd("_INV"))
      suffix = " (inverted)";
    if (takeEnd("_POS"))
      suffix = " +" + suffix;
    else if (takeEnd("_NEG"))
      suffix = " -" + suffix;

    auto words = [](std::string s)
    {
      for (char &c : s)
        c = c == '_' ? ' ' : char(std::tolower((unsigned char)c));
      if (!s.empty())
        s[0] = char(std::toupper((unsigned char)s[0]));
      return s;
    };
    std::string text;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, "AXIS") == 0)
      text = name.substr(0, name.size() - 4) + " axis";
    else if (name.compare(0, 6, "BUTTON") == 0 && name.size() > 6)
      text = "Button " + name.substr(6);
    else if (name.compare(0, 6, "SLIDER") == 0 && name.size() > 6)
      text = "Slider " + name.substr(6);
    else if (name.compare(0, 3, "POV") == 0 && name.find('_') != std::string::npos)
      text = "POV " + name.substr(3, name.find('_') - 3) + " " + Lower(name.substr(name.find('_') + 1));
    else
      text = words(name);
    return (negated ? "not " : "") + text + suffix;
  }

  static std::string Describe(const std::string &token)
  {
    std::string text;
    std::istringstream parts(token);
    for (std::string part; std::getline(parts, part, '+');)
      text += (text.empty() ? "" : " + ") + DescribePart(Trim(part));
    return text;
  }

  static std::vector<Device> Devices(CInputSystem &system)
  {
    std::vector<Device> devices;
    if (system.GetNumKeyboards() != 0)
      devices.push_back({ Device::Kind::Keyboard, 0, "Keyboard" });
    if (system.GetNumMice() != 0)
      devices.push_back({ Device::Kind::Mouse, 0, "Mouse" });
    for (int i = 0; i < system.GetNumJoysticks(); i++)
    {
      const JoyDetails *details = system.GetJoyDetails(i);
      devices.push_back({ Device::Kind::Joystick, i, details && details->name[0] ? details->name : "Joystick " + std::to_string(i + 1) });
    }
    return devices;
  }

  // A title bar across the page, as Flycast's
  static void Header(const char *label)
  {
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::Spacing();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_Header), style.FrameRounding);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y));
    ImGui::TextUnformatted(label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + style.ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
  }

  // sm2-emu's centre bar (as linuxloader's): an axis around its middle, -1 to 1
  static void CentreBar(const char *id, float value)
  {
    const ImGuiStyle &style = ImGui::GetStyle();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight() * 0.6f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(std::max(width, 1.0f), height));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float mid = pos.x + width * 0.5f;
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    draw->AddLine(ImVec2(mid, pos.y), ImVec2(mid, pos.y + height), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    const float x = std::round(mid + width * 0.5f * std::clamp(value, -1.0f, 1.0f));
    draw->AddRectFilled(ImVec2(x - 2.0f, pos.y), ImVec2(x + 2.0f, pos.y + height), ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
  }

  // A dot lit while the input is pressed
  static void PressDot(bool on)
  {
    const float size = ImGui::GetFontSize() * 0.7f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, ImGui::GetFontSize()));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + size * 0.5f, pos.y + ImGui::GetFontSize() * 0.5f), size * 0.5f,
                                                ImGui::GetColorU32(on ? ImGuiCol_SliderGrabActive : ImGuiCol_FrameBg));
  }

  // The input's value now, from all its devices
  static void DrawValue(CInput &input)
  {
    if (auto *axis = dynamic_cast<CAxisInput *>(&input))
    {
      float off = axis->GetOffValue();
      float span = input.value >= off ? float(axis->GetMaxValue()) - off : off - float(axis->GetMinValue());
      CentreBar("##value", span > 0.0f ? (float(input.value) - off) / span : 0.0f);
    }
    else if (auto *analog = dynamic_cast<CAnalogInput *>(&input))
    {
      float span = float(analog->GetMaxValue()) - float(analog->GetMinValue());
      float fraction = span != 0.0f ? (float(input.value) - analog->GetMinValue()) / span : 0.0f;
      ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 0.6f), "");
    }
    else if (auto *button = dynamic_cast<CSwitchInput *>(&input))
      PressDot(input.value != button->GetOffValue());
    else if (dynamic_cast<CGearShift4Input *>(&input))
      ImGui::Text("Gear %u", unsigned(input.value));
    else
      PressDot(input.value != 0);
  }

  static void SetBinding(Util::Config::Node &config, CInput &input, const std::string &mapping)
  {
    input.SetMapping(mapping.c_str());
    std::string key = std::string("Input") + input.id;
    config.Set(key, std::string(input.GetMapping()));
    if (std::find(s_changedBindings.begin(), s_changedBindings.end(), key) == s_changedBindings.end())
      s_changedBindings.push_back(key);
    s_status = "Bindings changed: Save keeps them";
  }

  static void StartCapture(const std::shared_ptr<CInput> &input)
  {
    unsigned flags = s_device.kind == Device::Kind::Keyboard ? READ_KEYBOARD :
                     s_device.kind == Device::Kind::Mouse ? READ_MOUSE : READ_JOYSTICK;
    s_inputs->GetInputSystem()->BeginMappingRead(flags, s_device.index, (input->flags & INPUT_FLAGS_AXIS) != 0);
    s_capturing = true;
    s_captureInput = input;
  }

  // Each frame, the inputs polled: the input read replaces the device's ones in the binding; the other devices' stay
  static void ContinueCapture(Util::Config::Node &config)
  {
    std::string mapping;
    if (!s_capturing || !s_inputs || !s_inputs->GetInputSystem()->ContinueMappingRead(&mapping))
      return;
    std::vector<std::string> tokens = DeviceTokens(s_device, s_captureInput->GetMapping(), false);
    tokens.push_back(mapping);
    SetBinding(config, *s_captureInput, JoinBinding(tokens));
    EndCapture();
  }

  // The device's bindings back to Supermodel's defaults, for the game's inputs
  static void ResetDevice(Util::Config::Node &config)
  {
    const Util::Config::Node defaults = DefaultConfig();
    for (const std::shared_ptr<CInput> &input : s_inputs->GetGameInputs(s_game))
    {
      const Util::Config::Node *node = defaults.TryGet(std::string("Input") + input->id);
      std::vector<std::string> tokens = DeviceTokens(s_device, input->GetMapping(), false);
      for (const std::string &token : DeviceTokens(s_device, node ? node->ValueAs<std::string>() : "NONE", true))
        tokens.push_back(token);
      std::string mapping = JoinBinding(tokens);
      if (mapping != input->GetMapping())
        SetBinding(config, *input, mapping);
    }
  }

  // The game's inputs and their bindings to the device, under a title bar per group of controls
  static void DrawBindings(Util::Config::Node &config)
  {
    std::string group;
    bool tableOpen = false;
    for (const std::shared_ptr<CInput> &input : s_inputs->GetGameInputs(s_game))
    {
      if (!tableOpen || group != input->GetInputGroup())
      {
        if (tableOpen)
          ImGui::EndTable();
        group = input->GetInputGroup();
        Header(group.c_str());
        tableOpen = ImGui::BeginTable(group.c_str(), 4, ImGuiTableFlags_SizingStretchProp);
        if (tableOpen)
        {
          ImGui::TableSetupColumn("##input", ImGuiTableColumnFlags_WidthStretch, 0.34f);
          ImGui::TableSetupColumn("##binding", ImGuiTableColumnFlags_WidthStretch, 0.30f);
          ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.22f);
          ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthFixed);
        }
      }
      if (!tableOpen)
        continue;

      ImGui::PushID(input->id);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(input->label);
      ImGui::SetItemTooltip("Input%s", input->id);

      const std::vector<std::string> tokens = DeviceTokens(s_device, input->GetMapping(), true);
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      std::string text;
      for (const std::string &token : tokens)
        text += (text.empty() ? "" : ", ") + Describe(token);
      if (text.empty())
        ImGui::TextDisabled("-");
      else
      {
        ImGui::TextUnformatted(text.c_str());
        ImGui::SetItemTooltip("%s", JoinBinding(tokens).c_str());
      }

      ImGui::TableSetColumnIndex(2);
      DrawValue(*input);

      // A whole axis (Full Steering) comes from an axis: the keyboard has its own inputs for it (Steer Left...)
      ImGui::TableSetColumnIndex(3);
      bool axisOnKeyboard = (input->flags & INPUT_FLAGS_AXIS) && s_device.kind == Device::Kind::Keyboard;
      ImGui::BeginDisabled(axisOnKeyboard);
      if (ImGui::Button("Map"))
        StartCapture(input);
      ImGui::EndDisabled();
      if (axisOnKeyboard)
        ImGui::SetItemTooltip("A whole axis: the keyboard has an input for each direction");
      ImGui::SameLine();
      ImGui::BeginDisabled(tokens.empty());
      if (ImGui::Button("Unmap"))
        SetBinding(config, *input, JoinBinding(DeviceTokens(s_device, input->GetMapping(), false)));
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    if (tableOpen)
      ImGui::EndTable();
  }

  // A device's page, over the whole window: its inputs for each of the game's
  static void DrawMappingPage(Util::Config::Node &config)
  {
    if (s_mappingRequest)
    {
      ImGui::OpenPopup("###mapping");
      s_mappingRequest = false;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(1.0f);  // a page of its own: nothing shows through
    std::string title = "Bindings: " + s_device.name + "###mapping";
    s_mappingOpen = ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (!s_mappingOpen)
      return;

    if (ImGui::Button("Close") || s_mappingClose)
    {
      EndCapture();
      ImGui::CloseCurrentPopup();
    }
    s_mappingClose = false;
    ImGui::SameLine();
    ImGui::TextDisabled("Save or Save for this game, in the settings, keeps the bindings.");

    const char *resetLabel = "Reset...";
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(resetLabel).x - style.FramePadding.x * 2.0f);
    if (ImGui::Button(resetLabel))
      ImGui::OpenPopup("##reset");
    if (ImGui::BeginPopupModal("##reset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
      ImGui::TextUnformatted("Set this device's bindings back to Supermodel's defaults?");
      if (ImGui::Button("Yes"))
      {
        ResetDevice(config);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("No"))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (ImGui::BeginChild("##bindings", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
      DrawBindings(config);
    ImGui::EndChild();

    if (s_capturing)
      ImGui::OpenPopup("##capture");
    if (ImGui::BeginPopupModal("##capture", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
      if (!s_capturing)
        ImGui::CloseCurrentPopup();
      else
      {
        ImGui::Text("Press an input of %s for: %s", s_device.name.c_str(), s_captureInput->label);
        if (s_captureInput->flags & INPUT_FLAGS_AXIS)
          ImGui::TextDisabled("A whole axis: move it to one end and back");
        ImGui::TextDisabled("Escape: cancel");
        // A click would be the mouse's input: the mouse cancels with Escape only
        if (s_device.kind != Device::Kind::Mouse && ImGui::Button("Cancel"))
          EndCapture();
      }
      ImGui::EndPopup();
    }
    ImGui::EndPopup();
  }

  namespace
  {
    // The limits per effect of the input systems, in % (read at each effect sent)
    struct Limit
    {
      const char *key, *label;
    };
    const Limit kDirectInputLimits[] = { { "DirectInputConstForceLeftMax", "Constant force to the left" },
                                         { "DirectInputConstForceRightMax", "Constant force to the right" },
                                         { "DirectInputSelfCenterMax", "Spring" },
                                         { "DirectInputFrictionMax", "Friction" },
                                         { "DirectInputDamperMax", "Damper" },
                                         { "DirectInputVibrateMax", "Vibration" } };
    const Limit kXInputLimits[] = { { "XInputConstForceMax", "Constant force, as vibration" },
                                    { "XInputConstForceThreshold", "Constant force vibrates from" },
                                    { "XInputVibrateMax", "Vibration" } };
    const Limit kSDLLimits[] = { { "SDLConstForceMax", "Constant force" },
                                 { "SDLSelfCenterMax", "Spring" },
                                 { "SDLFrictionMax", "Friction" },
                                 { "SDLDamperMax", "Damper" },
                                 { "SDLVibrateMax", "Vibration" },
                                 { "SDLConstForceThreshold", "Constant force vibrates from (rumble only)" } };
  }

  static void DrawLimits(Util::Config::Node &config, const Limit *limits, size_t count)
  {
    for (size_t i = 0; i < count; i++)
    {
      if (config.TryGet(limits[i].key) == nullptr)
        continue;
      int value = std::clamp(config[limits[i].key].ValueAsDefault<int>(100), 0, 100);
      ImGui::PushID(limits[i].key);
      if (ImGui::SliderInt(limits[i].label, &value, 0, 100, "%d %%"))
      {
        config.Set(limits[i].key, value);
        s_changes |= ChangeForceFeedback;
      }
      ImGui::SetItemTooltip("%s", limits[i].key);
      ImGui::PopID();
    }
  }

  // linuxloader's force feedback section, with the device that plays the effects and what the game asks now
  static void DrawForceFeedback(Util::Config::Node &config, CInputSystem &system)
  {
    Header("Force feedback");
    bool enabled = config["ForceFeedback"].ValueAsDefault<bool>(false);
    if (ImGui::Checkbox("Force feedback", &enabled))
      config.Set("ForceFeedback", enabled);
    ImGui::SameLine();
    bool simulate = config["SimulateDriveBoard"].ValueAsDefault<bool>(false);
    if (ImGui::Checkbox("Simulate the drive board", &simulate))
      config.Set("SimulateDriveBoard", simulate);
    Wrapped("Force feedback: the game's drive board connected and its effects played. Simulated: the board's logic "
            "instead of its program run. Both from the next start.");

    // The device that plays the effects: by name, whatever its number at the next start
    std::string device = Trim(config["ForceFeedbackDevice"].ValueAsDefault<std::string>(""));
    if (Lower(device) == "auto")
      device.clear();
    const char *automatic = "Automatic";
    std::string preview = device.empty() ? automatic : device + (system.FindJoystick(device) < 0 ? " (not connected)" : "");
    bool any = false;
    if (ImGui::BeginCombo("Device", preview.c_str()))
    {
      auto choose = [&config](const std::string &name)
      {
        config.Set("ForceFeedbackDevice", name);
        s_changes |= ChangeForceFeedback;
      };
      if (ImGui::Selectable(automatic, device.empty()))
        choose(std::string());
      for (int joyNum = 0; joyNum < system.GetNumJoysticks(); joyNum++)
      {
        const JoyDetails *details = system.GetJoyDetails(joyNum);
        if (details == nullptr || !details->hasFFeedback)
          continue;
        std::string label = std::string(details->name) + " (JOY" + std::to_string(joyNum + 1) + ")";
        if (ImGui::Selectable(label.c_str(), !device.empty() && system.FindJoystick(device) == joyNum))
          choose(details->name);
      }
      ImGui::EndCombo();
    }
    for (int joyNum = 0; joyNum < system.GetNumJoysticks(); joyNum++)
    {
      const JoyDetails *details = system.GetJoyDetails(joyNum);
      any |= details != nullptr && details->hasFFeedback;
    }
    if (!any)
      ImGui::TextDisabled("No joystick with force feedback connected.");
    Wrapped("The device that plays the effects, whatever device the steering is read from (a wheel read through vJoy, "
            "another one playing the effects...): on the same axis as the steering's, or its first with force "
            "feedback. Automatic, or the device chosen not connected: the joysticks the steering is bound to.");

    int gain = std::clamp(config["ForceFeedbackGain"].ValueAsDefault<int>(100), 0, 300);
    if (ImGui::SliderInt("Strength", &gain, 0, 300, "%d %%"))
    {
      config.Set("ForceFeedbackGain", gain);
      s_changes |= ChangeForceFeedback;
    }
    bool invert = config["ForceFeedbackInvert"].ValueAsDefault<bool>(false);
    if (ImGui::Checkbox("Invert the direction", &invert))
    {
      config.Set("ForceFeedbackInvert", invert);
      s_changes |= ChangeForceFeedback;
    }
    Wrapped("Strength: of all the effects, in percent of what the game asks; the limits per effect below apply after "
            "it. Invert: if the wheel pulls away from the centre, or turns the wrong way in a collision.");

    // What the game asks now, as it asks it (before the strength and the limits)
    bool shown = false;
    if (s_inputs)
    {
      for (const std::shared_ptr<CInput> &input : s_inputs->GetGameInputs(s_game))
      {
        if (!input->HasForceFeedback())
          continue;
        shown = true;
        ImGui::TextDisabled("Asked by the game now, on %s:", input->label);
        if (ImGui::BeginTable(input->id, 2, ImGuiTableFlags_SizingStretchProp))
        {
          ImGui::TableSetupColumn("##effect", ImGuiTableColumnFlags_WidthStretch, 0.4f);
          ImGui::TableSetupColumn("##force", ImGuiTableColumnFlags_WidthStretch, 0.6f);
          auto row = [](const char *label)
          {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
          };
          // A constant force > 0 pushes to the left: drawn to the left
          float constant = input->GetForceFeedback(FFConstantForce);
          row("Constant force");
          CentreBar("##constant", -constant);
          const std::pair<EForceFeedback, const char *> others[] = { { FFSelfCenter, "Spring" }, { FFFriction, "Friction" },
                                                                     { FFDamper, "Damper" }, { FFVibrate, "Vibration" } };
          for (const auto &[effect, label] : others)
          {
            row(label);
            ImGui::ProgressBar(std::clamp(input->GetForceFeedback(effect), 0.0f, 1.0f), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 0.6f), "");
          }
          ImGui::EndTable();
        }
      }
    }
    if (!shown)
      ImGui::TextDisabled("The game has sent no force feedback yet.");

    // The limits of the input system in use
    std::string inputSystem = config["InputSystem"].ValueAsDefault<std::string>("");
    std::string title = "Limits per effect (" + inputSystem + ")";
    if (ImGui::TreeNode(title.c_str()))
    {
      if (inputSystem == "sdl" || inputSystem == "sdlgamepad")
        DrawLimits(config, kSDLLimits, IM_ARRAYSIZE(kSDLLimits));
      else
      {
        if (inputSystem == "xinput")
        {
          ImGui::TextDisabled("XInput controllers (vibration motors):");
          DrawLimits(config, kXInputLimits, IM_ARRAYSIZE(kXInputLimits));
          if (config.TryGet("XInputStereoVibration") != nullptr)
          {
            bool both = config["XInputStereoVibration"].ValueAsDefault<bool>(true);
            if (ImGui::Checkbox("Vibrate with both motors", &both))
            {
              config.Set("XInputStereoVibration", both);
              s_changes |= ChangeForceFeedback;
            }
          }
          ImGui::TextDisabled("The other joysticks:");
        }
        DrawLimits(config, kDirectInputLimits, IM_ARRAYSIZE(kDirectInputLimits));
      }
      ImGui::TreePop();
    }
  }

  static void DrawControlsPage(Util::Config::Node &config)
  {
    if (s_inputs == nullptr)
    {
      ImGui::TextDisabled("No inputs to bind.");
      return;
    }
    CInputSystem &system = *s_inputs->GetInputSystem();

    Header("Devices");
    bool joystick = false;
    if (ImGui::BeginTable("##devices", 3, ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.6f);
      ImGui::TableSetupColumn("In the settings", ImGuiTableColumnFlags_WidthStretch, 0.4f);
      ImGui::TableSetupColumn("##map", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableHeadersRow();
      int i = 0;
      for (const Device &device : Devices(system))
      {
        const JoyDetails *details = device.kind == Device::Kind::Joystick ? system.GetJoyDetails(device.index) : nullptr;
        joystick |= details != nullptr;
        ImGui::PushID(i++);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s%s", device.name.c_str(), details && details->hasFFeedback ? " (force feedback)" : "");
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s...", DevicePrefix(device).c_str());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("Map"))
        {
          s_device = device;
          s_mappingRequest = true;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!joystick)
      ImGui::TextDisabled("No joystick connected.");
    Wrapped("Map: the game's inputs, each bound to the device's own; what other devices are bound to stays.");

    Header("Options");
    bool background = config["BackgroundInput"].ValueAsDefault<bool>(false);
    if (ImGui::Checkbox("Read the controllers in the background", &background))
    {
      config.Set("BackgroundInput", background);
      s_changes |= ChangeInputSystem;
    }
    Wrapped("While another window has the focus, the joysticks are still read and their force feedback played "
            "(input systems dinput, xinput and rawinput; with sdl and sdlgamepad they always are).");
    ImGui::TextDisabled("Input system: %s (changed in the launcher, from the next start)", config["InputSystem"].ValueAsDefault<std::string>("").c_str());

    DrawForceFeedback(config, system);
    DrawMappingPage(config);
  }

  void Draw(Util::Config::Node &config, const std::string &title, const std::string &settingsFile, bool gameFile)
  {
    if (!s_open)
      return;

    ContinueCapture(config);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    Scale(ImGui::GetIO().DisplaySize.y);
    ImGui::NewFrame();

    // A fixed width, for the Game page's text to wrap in
    const ImGuiIO &io = ImGui::GetIO();
    float width = std::min(ImGui::GetFontSize() * 40.0f, io.DisplaySize.x * 0.9f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y * 0.9f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextWrapped("%s", title.c_str());

    bool selectDisplay = s_selectPage && s_page == Page::Display;
    bool selectControls = s_selectPage && s_page == Page::Controls;
    bool selectGame = s_selectPage && s_page == Page::Game;
    bool selectOutputs = s_selectPage && s_page == Page::Outputs;
    s_selectPage = false;
    if (ImGui::BeginTabBar("Pages"))
    {
      if (ImGui::BeginTabItem("Display", nullptr, selectDisplay ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawDisplayPage(config);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls", nullptr, selectControls ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawControlsPage(config);
        ImGui::EndTabItem();
      }
      else
        s_mappingOpen = false;
      if (ImGui::BeginTabItem("Game", nullptr, selectGame ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawGamePage();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Outputs", nullptr, selectOutputs ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawOutputsPage(config);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    // Saving
    ImGui::Separator();
    ImGui::TextDisabled("Settings file: %s%s", settingsFile.c_str(), gameFile ? " (this game's own)" : "");
    if (ImGui::Button("Save"))
      s_action = Action::Save;
    ImGui::SameLine();
    if (ImGui::Button("Save for this game"))
      s_action = Action::SaveForGame;
    ImGui::SameLine();
    if (ImGui::Button("Close"))
      s_closeRequested = true;
    if (!s_status.empty())
      ImGui::TextUnformatted(s_status.c_str());
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }

  namespace
  {
    // The internal resolutions offered: linuxloader's and windowsloader's, with Model 3's own first
    const Size kInternalSizes[] = { { 496, 384 }, { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 1024 },
                                    { 1360, 768 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };

    // DISPLAY_MODE's values (linuxloader's: WINDOW, WINDOW_STRETCH, FULLSCREEN...), as FullScreen, BorderlessFullScreen
    // and Stretch make them
    const char *const kDisplayModes[] = { "Window, square pixels", "Window, stretched", "Full screen", "Full screen, stretched",
                                          "Borderless fullscreen window", "Borderless fullscreen window, stretched" };
  }

  static std::string SizeText(unsigned width, unsigned height, const char *note = "")
  {
    return std::to_string(width) + " x " + std::to_string(height) + note;
  }

  // The size the image is drawn at (InternalWidth and InternalHeight, 0: the size it takes in the window)
  static void DrawInternalResolution(Util::Config::Node &config)
  {
    unsigned width = config["InternalWidth"].ValueAsDefault<unsigned>(0);
    unsigned height = config["InternalHeight"].ValueAsDefault<unsigned>(0);
    bool native = width == 0 || height == 0;
    bool listed = false;
    for (const Size &size : kInternalSizes)
      listed |= size.width == width && size.height == height;
    std::string preview = native ? "Native (the window's)" : SizeText(width, height, listed ? "" : " (custom)");
    if (ImGui::BeginCombo("Internal resolution", preview.c_str()))
    {
      auto choose = [&config](unsigned w, unsigned h)
      {
        config.Set("InternalWidth", w);
        config.Set("InternalHeight", h);
        s_changes |= ChangeLayout;
      };
      if (ImGui::Selectable("Native (the window's)", native))
        choose(0, 0);
      for (const Size &size : kInternalSizes)
      {
        std::string label = SizeText(size.width, size.height, size.width == 496 ? " (Model 3)" : "");
        if (ImGui::Selectable(label.c_str(), !native && size.width == width && size.height == height))
          choose(size.width, size.height);
      }
      ImGui::EndCombo();
    }
    if (s_renderWidth && s_renderHeight)
      ImGui::TextDisabled("Drawn at %u x %u now.", s_renderWidth, s_renderHeight);
    Wrapped("The size the game's image is drawn at: it fits in it at its own shape, then the aspect ratio and the display "
            "mode place it in the window or on the screen. Native: the size it has there. Smaller: lighter for a small "
            "graphics card; larger: smoother edges.");
  }

  // Windowed, full screen or borderless, and the window's size or the screen's resolution
  static void DrawDisplayMode(Util::Config::Node &config)
  {
    bool fullScreen = config["FullScreen"].ValueAsDefault<bool>(false);
    bool borderless = config["BorderlessFullScreen"].ValueAsDefault<bool>(false);
    bool stretch = config["Stretch"].ValueAsDefault<bool>(false);
    int mode = (!fullScreen ? 0 : borderless ? 4 : 2) + (stretch ? 1 : 0);
    if (ImGui::Combo("Display mode", &mode, kDisplayModes, IM_ARRAYSIZE(kDisplayModes)))
    {
      config.Set("FullScreen", mode >= 2);
      if (mode >= 2)
        config.Set("BorderlessFullScreen", mode >= 4);
      config.Set("Stretch", (mode & 1) != 0);
      s_changes |= ChangeWindow;
      fullScreen = mode >= 2;
      borderless = mode >= 4;
    }

    unsigned width = config["XResolution"].ValueAsDefault<unsigned>(496);
    unsigned height = config["YResolution"].ValueAsDefault<unsigned>(384);
    bool desktop = fullScreen && borderless;
    ImGui::BeginDisabled(desktop);
    std::string preview = desktop ? "The desktop's" : SizeText(width, height);
    if (ImGui::BeginCombo(fullScreen ? "Screen resolution" : "Window size", preview.c_str()))
    {
      for (const Size &size : s_resolutions)
      {
        std::string label = SizeText(size.width, size.height, size.width % 496 == 0 && size.width / 496 * 384 == size.height ? " (Model 3)" : "");
        if (ImGui::Selectable(label.c_str(), size.width == width && size.height == height))
        {
          config.Set("XResolution", size.width);
          config.Set("YResolution", size.height);
          s_changes |= ChangeWindow;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Wrapped("Full screen: the screen switches to the resolution chosen. Borderless: a window without borders covers the "
            "screen, at the desktop's resolution. Stretched: the frame fills the window or the screen, else it keeps its "
            "shape. A window's edges can be dragged to resize it. Alt+Enter: window / full screen.");
  }

  // The frame and how the image fills it, and how the frames are shown
  static void DrawDisplayPage(Util::Config::Node &config)
  {
    DrawInternalResolution(config);
    ImGui::Spacing();

    AspectRatio::Settings current;
    bool known = AspectRatio::Parse(&current, config["AspectRatio"].ValueAsDefault<std::string>("ORIGINAL"));
    int index = -1;
    for (int i = 0; known && i < 7; i++)
    {
      AspectRatio::Settings s;
      AspectRatio::Parse(&s, AspectRatio::kNames[i]);
      if (s.fit == current.fit && s.frameAspect == current.frameAspect)
        index = i;
    }
    if (ImGui::Combo("Aspect ratio", &index, AspectRatio::kLabels, 7))
    {
      config.Set("AspectRatio", std::string(AspectRatio::kNames[index]));
      AspectRatio::Parse(&current, AspectRatio::kNames[index]);
      s_changes |= ChangeLayout;
    }
    if (current.fit == AspectRatio::Fit::Crop)
    {
      Slider(config, "Crop position", "CropPosition", 0, -100, 100, "%d");
      ImGui::TextDisabled("-100: left or top, 0: the middle, 100: right or bottom.");
      bool barrel = config["CropBarrel"].ValueAsDefault<bool>(false);
      if (ImGui::Checkbox("Curve the cropped parts onto the edges", &barrel))
      {
        config.Set("CropBarrel", barrel);
        s_changes |= ChangeLayout;
      }
      if (barrel)
      {
        Slider(config, "Undistorted middle", "BarrelCenter", 60, 0, 100, "%d %%");
        Slider(config, "Curvature radius", "BarrelRadius", 50, 10, 300, "%d %%");
        Wrapped("The whole image stays visible: the middle (in % of the frame) as cropped, the parts the crop would cut "
                "squeezed onto the edges as on a cylinder seen from the front. A small radius bends hard at the edge, a "
                "large one squeezes evenly.");
      }
    }
    Wrapped("Original: the shape of the game's image. Black bars: the frame's shape, with the whole image inside at its "
            "own shape. Stretched: the image fills the frame, deformed. Cropped: the image fills the frame at its own "
            "shape, cut on the sides (or above and below); the crop position moves the part kept.");
    ImGui::Spacing();

    DrawDisplayMode(config);

    ImGui::Separator();
    bool throttle = config["Throttle"].ValueAsDefault<bool>(true);
    char limit[64];
    snprintf(limit, sizeof(limit), "Limit the frame rate (%.3f Hz)", std::abs(config["RefreshRate"].ValueAsDefault<float>(57.524f)));
    if (ImGui::Checkbox(limit, &throttle))
      config.Set("Throttle", throttle);
    bool vsync = config["VSync"].ValueAsDefault<bool>(true);
    if (ImGui::Checkbox("Vertical sync", &vsync))
    {
      config.Set("VSync", vsync);
      s_changes |= ChangeSwapInterval;
    }
    bool fps = config["ShowFrameRate"].ValueAsDefault<bool>(false);
    if (ImGui::Checkbox("Show the frame rate (in the window's title)", &fps))
      config.Set("ShowFrameRate", fps);
    // Over the window, in any display mode (always in full screen); applied when the menu closes, which shows the pointer
    // while it is open
    bool hideCursor = config["HideCursor"].ValueAsDefault<bool>(true);
    if (ImGui::Checkbox("Hide the mouse pointer (shown in this menu)", &hideCursor))
      config.Set("HideCursor", hideCursor);
  }

  unsigned TakeChanges()
  {
    unsigned changes = s_changes;
    s_changes = 0;
    return changes;
  }

  const std::vector<std::string> &ChangedBindings()
  {
    return s_changedBindings;
  }

  Action TakeAction()
  {
    Action action = s_action;
    s_action = Action::None;
    return action;
  }

  void SetStatus(const std::string &status)
  {
    s_status = status;
  }

  void Shutdown()
  {
    Close();
    if (!s_initialized)
      return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    s_initialized = false;
  }
}
