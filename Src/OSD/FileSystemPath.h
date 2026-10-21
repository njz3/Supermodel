/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2022 The Supermodel Team
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
 * FileSystemPaths.h
 *
 * Header file for OS-dependent Supermodel files locations.
 */

#ifndef INCLUDED_FILESYSTEMPATH_H
#define INCLUDED_FILESYSTEMPATH_H

#include <string>

namespace FileSystemPath
{
    enum PathType { Analysis, Config, Log, NVRAM, Saves, Screenshots, Assets, ROMs }; // Filesystem path types
    bool PathExists(std::string fileSystemPath); // Checks if a directory exists (returns true if exists, false if it doesn't)
    int MakeDir(std::string dir); // Create a directory
    std::string GetPath(PathType pathType);  // Generates a path to be used by Supermodel files

    // The write directory (WriteDirectory.cpp, common to all systems): when set, everything Supermodel writes goes under
    // it, the user's settings at its root (Config/), each game's files in a folder named after it
    void SetWriteDirectory(const std::string &dir);   // empty: none, the usual places
    bool HasWriteDirectory();
    std::string GetWriteDirectory();                  // with a trailing '/', empty when none
    void SetGame(const std::string &game);            // the game whose folder takes its NVRAM, save states, screenshots
    std::string GetWritePath(PathType pathType);      // where to write: in the write directory (created), else GetPath
    std::string GetReadPath(PathType pathType, const std::string &file);  // the write directory's file if there, else GetPath's
}


#endif // INCLUDED_FILESYSTEMPATH_H
