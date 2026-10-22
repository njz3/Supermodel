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
 * Escape to close.
 */

#include "GameMenu.h"
#include "Graphics/AspectRatio.h"
#include "Util/NewConfig.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"

#include <algorithm>

namespace GameMenu
{
  namespace
  {
    bool s_initialized = false;
    bool s_open = false;
    bool s_closeRequested = false;
    bool s_changed = false;
    Action s_action = Action::None;
    std::string s_status;
    int s_cursor = SDL_ENABLE;    // the mouse cursor's visibility before it opened
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
    return true;
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

  void Draw(Util::Config::Node &config, const std::string &title, const std::string &settingsFile, bool gameFile)
  {
    if (!s_open)
      return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    // The frame and how the image fills it
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
