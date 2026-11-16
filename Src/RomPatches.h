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
 * RomPatches.h
 *
 * ROM patches: changes to a game's ROM files, applied as the files load
 * (GameLoader::SetRomPatcher), which the emulator can also write out as
 * patched files (to program the EPROMs of a real board).
 *
 * A patch is an XML file in the game's folder of patches (Assets/Patches/<ROM
 * set>/<patch>.xml):
 *
 *   <patch name="forest" title="FOREST, the hidden fifth course">
 *     <description>What it does (shown in Game Options).</description>
 *     <requires>other</requires>       another patch it needs (any number)
 *     <conflicts>other</conflicts>     another it cannot go with (any number)
 *     <file name="epr-20632.17" crc32="6829A801">   the changes it always makes
 *       <data offset="1A2B4">0011AABB</data>        bytes at that offset (hex)
 *     </file>
 *     <option name="championship" title="..." default="false">   on or off
 *       <description>...</description>
 *       <requires>rain</requires>     another option of the patch it needs
 *       <conflicts>...</conflicts>
 *       <file ...>...</file>          the changes it makes when on
 *     </option>
 *     <option name="music" title="Music" default="mountain">  one of its values
 *       <value name="mountain" title="MOUNTAIN's"><description/></value>
 *       <value name="other" title="..."><file ...>...</file></value>
 *     </option>
 *   </patch>
 *
 * A file's changes apply only to the file the game's ROMs have (its CRC32): a
 * patch holds none of the game's own data. The settings, in the game's
 * section: Patch_<patch> = true (on), Patch_<patch>_<option> = true, false or
 * a value's name (else the option's default).
 */

#ifndef INCLUDED_ROMPATCHES_H
#define INCLUDED_ROMPATCHES_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace RomPatches
{
  struct Change
  {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
  };

  // The changes to one ROM file
  struct FileChanges
  {
    std::string file;       // its name (as the game's definition has it)
    uint32_t crc32 = 0;     // the file they are for
    std::vector<Change> changes;
  };

  struct Value
  {
    std::string name, title, description;
    std::vector<FileChanges> files;
  };

  struct Option
  {
    std::string name, title, description;
    std::string defaultValue;                   // "true" or "false"; for a choice, a value's name
    std::vector<std::string> requires, conflicts;   // options of the same patch (on, or not the default value)
    std::vector<FileChanges> files;             // on or off
    std::vector<Value> values;                  // a choice: one of these
    bool IsChoice() const { return !values.empty(); }
  };

  struct Patch
  {
    std::string name, title, description;
    std::string path;                           // its file
    std::vector<std::string> requires, conflicts;   // other patches of the game
    std::vector<FileChanges> files;             // always, when on
    std::vector<Option> options;
    std::string error;                          // why it could not be read (then unusable)
  };

  // The patches of a folder (its *.xml), by name
  std::vector<Patch> Read(const std::string &folder);
  bool ReadPatch(const std::string &path, Patch *patch);

  // Settings
  std::string PatchKey(const Patch &patch);
  std::string OptionKey(const Patch &patch, const Option &option);
  bool IsTrue(const std::string &value);

  // An option's value as set: "true"/"false", or a value's name (the default when not set or unknown)
  std::string OptionValue(const Option &option, const std::string &setting);

  // The patches turned on (setting(key) gives a setting's value, "" when not set), their options as set, applied to
  // the game's files as they load
  class Patcher
  {
  public:
    Patcher(const std::vector<Patch> &patches, const std::function<std::string(const std::string &key)> &setting);

    // The game's files (name and CRC32): a patch whose changes are for other files, or whose requirements are not met,
    // or whose changes overlap another's, is left out (all of it). Before the files load
    void Check(const std::map<std::string, uint32_t> &files);

    // A file's changes applied (false if none apply to it); written to the dump folder too, if any
    bool Apply(const std::string &file, uint8_t *data, size_t size);

    // Where to write the patched files (a folder), "" for nowhere
    void SetDumpFolder(const std::string &folder) { m_dumpFolder = folder; }
    const std::vector<std::string> &Dumped() const { return m_dumped; }

    // The patches applied ("title (option, option = value)") and the problems found
    const std::vector<std::string> &Applied() const { return m_applied; }
    const std::vector<std::string> &Errors() const { return m_errors; }
    bool Empty() const { return m_selected.empty(); }

  private:
    struct Selected
    {
      const Patch *patch;
      std::vector<const FileChanges *> files;   // the patch's and its options' (as set)
      std::string label;
    };
    std::vector<Patch> m_patches;
    std::vector<Selected> m_selected;
    std::map<std::string, std::vector<const FileChanges *>> m_byFile;   // by file name (lower case)
    std::vector<std::string> m_applied, m_errors, m_dumped;
    std::string m_dumpFolder;
  };
}

#endif  // INCLUDED_ROMPATCHES_H
