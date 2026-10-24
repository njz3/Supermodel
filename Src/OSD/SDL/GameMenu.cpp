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
 * Escape to close; Page Up and Page Down scroll the game's notes.
 */

#include "GameMenu.h"
#include "Game.h"
#include "Graphics/AspectRatio.h"
#include "Util/NewConfig.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <fstream>
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
    bool s_changed = false;
    Action s_action = Action::None;
    std::string s_status;
    int s_cursor = SDL_ENABLE;    // the mouse cursor's visibility before it opened
    bool s_selectPage = false;
    Page s_page = Page::Display;

    // The Game page
    std::vector<std::pair<std::string, std::string>> s_info;  // label, value
    std::vector<std::string> s_noteNames;                     // the game's ROM set, then its parent's
    std::string s_notesFile;
    std::vector<Note> s_notes;
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

    // Sized for the window: legible on a cabinet's 1080p or 4K screen
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    float scale = std::max(1.0f, height / 720.0f);
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    s_cursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    s_open = true;
    s_closeRequested = false;
    s_status.clear();
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
      // Escape closes it, unless it closes a list first
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && !e.key.repeat &&
          !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
      {
        s_closeRequested = true;
        continue;
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
      s_changed = true;
    }
    return value;
  }

  static void DrawDisplayPage(Util::Config::Node &config);

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

  void Draw(Util::Config::Node &config, const std::string &title, const std::string &settingsFile, bool gameFile)
  {
    if (!s_open)
      return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // A fixed width, for the Game page's text to wrap in
    const ImGuiIO &io = ImGui::GetIO();
    float width = std::min(ImGui::GetFontSize() * 40.0f, io.DisplaySize.x * 0.9f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, io.DisplaySize.y * 0.9f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(title.c_str());

    bool selectDisplay = s_selectPage && s_page == Page::Display;
    bool selectGame = s_selectPage && s_page == Page::Game;
    s_selectPage = false;
    if (ImGui::BeginTabBar("Pages"))
    {
      if (ImGui::BeginTabItem("Display", nullptr, selectDisplay ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawDisplayPage(config);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Game", nullptr, selectGame ? ImGuiTabItemFlags_SetSelected : 0))
      {
        DrawGamePage();
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

  // The frame and how the image fills it
  static void DrawDisplayPage(Util::Config::Node &config)
  {
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
      s_changed = true;
    }
    if (current.fit == AspectRatio::Fit::Crop)
    {
      Slider(config, "Crop position", "CropPosition", 0, -100, 100, "%d");
      bool barrel = config["CropBarrel"].ValueAsDefault<bool>(false);
      if (ImGui::Checkbox("Curve the cropped parts onto the edges", &barrel))
      {
        config.Set("CropBarrel", barrel);
        s_changed = true;
      }
      if (barrel)
      {
        Slider(config, "Undistorted middle", "BarrelCenter", 60, 0, 100, "%d %%");
        Slider(config, "Curvature radius", "BarrelRadius", 50, 10, 300, "%d %%");
      }
    }
  }

  bool TakeChanged()
  {
    bool changed = s_changed;
    s_changed = false;
    return changed;
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
