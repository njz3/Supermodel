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
 * WriteDirectory.cpp
 *
 * The write directory (WriteDirectory, -write-dir), common to all systems:
 * when set, everything Supermodel writes goes under it, so that the rest of
 * the disk can be read only (a cabinet under Windows' Unified Write Filter,
 * the games on a network share). The user's settings at its root (Config/),
 * each game's NVRAM, save states, screenshots and dumps in a folder named
 * after the game. The files only read are taken from it first, else from
 * their usual places (GetPath).
 */

#include "FileSystemPath.h"

#include <filesystem>
#include <system_error>

namespace FileSystemPath
{
    namespace
    {
        std::filesystem::path s_writeDir;   // empty: none
        std::string s_game;                 // the game whose folder takes its files; empty before one is loaded
    }

    void SetWriteDirectory(const std::string &dir)
    {
        s_writeDir = dir.empty() ? std::filesystem::path() : std::filesystem::path(dir);
    }

    bool HasWriteDirectory()
    {
        return !s_writeDir.empty();
    }

    std::string GetWriteDirectory()
    {
        return s_writeDir.empty() ? std::string() : s_writeDir.generic_string() + "/";
    }

    void SetGame(const std::string &game)
    {
        s_game = game;
    }

    std::string GetWritePath(PathType pathType)
    {
        if (s_writeDir.empty())
            return GetPath(pathType);

        std::filesystem::path dir = s_writeDir;
        switch (pathType)
        {
        case Config:        // the user's settings, for all the games
            dir /= "Config";
            break;
        case NVRAM:
        case Saves:
        case Analysis:
            if (!s_game.empty())
                dir /= s_game;
            dir /= pathType == NVRAM ? "NVRAM" : pathType == Saves ? "Saves" : "Analysis";
            break;
        case Log:           // at the root until a game is loaded
        case Screenshots:
            if (!s_game.empty())
                dir /= s_game;
            break;
        default:            // Assets, ROMs: only read
            return GetPath(pathType);
        }

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir.generic_string() + "/";
    }

    std::string GetReadPath(PathType pathType, const std::string &file)
    {
        if (!s_writeDir.empty())
        {
            std::string written = GetWritePath(pathType) + file;
            std::error_code ec;
            if (std::filesystem::exists(written, ec))
                return written;
        }
        return GetPath(pathType) + file;
    }
}
