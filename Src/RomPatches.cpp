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
 * RomPatches.cpp
 *
 * See RomPatches.h for the format.
 */

#include "RomPatches.h"
#include "OSD/Logger.h"
#include "Util/ConfigBuilders.h"
#include "Util/NewConfig.h"
#include "Util/Format.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace RomPatches
{
  static std::string Trim(const std::string &s)
  {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
      a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
      b--;
    return s.substr(a, b - a);
  }

  // Text as written in the file: lines joined, paragraphs (an empty line) kept apart
  static std::string Paragraphs(const std::string &text)
  {
    std::string out, paragraph, line;
    std::istringstream in(text);
    auto flush = [&]() {
      if (!paragraph.empty())
        out += (out.empty() ? "" : "\n\n") + paragraph;
      paragraph.clear();
    };
    while (std::getline(in, line))
    {
      line = Trim(line);
      if (line.empty())
        flush();
      else
        paragraph += (paragraph.empty() ? "" : " ") + line;
    }
    flush();
    return out;
  }

  static std::string Text(const Util::Config::Node &node, const std::string &key)
  {
    const Util::Config::Node *child = node.TryGet(key);
    return child && child->Exists() ? Trim(child->ValueAs<std::string>()) : std::string();
  }

  static bool Hex(const std::string &text, uint32_t *value)
  {
    std::string s = Trim(text);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
      s = s.substr(2);
    if (s.empty() || s.size() > 8 || !std::all_of(s.begin(), s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
      return false;
    *value = uint32_t(std::stoul(s, nullptr, 16));
    return true;
  }

  static bool HexBytes(const std::string &text, std::vector<uint8_t> *bytes)
  {
    std::string digits;
    for (char c : text)
    {
      if (std::isspace(static_cast<unsigned char>(c)))
        continue;
      if (!std::isxdigit(static_cast<unsigned char>(c)))
        return false;
      digits += c;
    }
    if (digits.empty() || digits.size() % 2)
      return false;
    for (size_t i = 0; i < digits.size(); i += 2)
      bytes->push_back(uint8_t(std::stoul(digits.substr(i, 2), nullptr, 16)));
    return true;
  }

  // The <file> elements of a node
  static bool ReadFiles(const Util::Config::Node &node, std::vector<FileChanges> *files, std::string *error)
  {
    for (const Util::Config::Node &child : node)
    {
      if (child.Key() != "file")
        continue;
      FileChanges file;
      file.file = Text(child, "name");
      if (file.file.empty() || !Hex(Text(child, "crc32"), &file.crc32))
      {
        *error = "a <file> without a name or a CRC32";
        return false;
      }
      for (const Util::Config::Node &data : child)
      {
        if (data.Key() != "data")
          continue;
        Change change;
        if (!Hex(Text(data, "offset"), &change.offset) || !HexBytes(data.ValueAs<std::string>(), &change.bytes))
        {
          *error = "bad <data> in " + file.file + " (an offset and hexadecimal bytes)";
          return false;
        }
        file.changes.push_back(std::move(change));
      }
      files->push_back(std::move(file));
    }
    return true;
  }

  static void ReadNames(const Util::Config::Node &node, const std::string &key, std::vector<std::string> *names)
  {
    for (const Util::Config::Node &child : node)
    {
      if (child.Key() == key && !Trim(child.ValueAs<std::string>()).empty())
        names->push_back(Trim(child.ValueAs<std::string>()));
    }
  }

  bool ReadPatch(const std::string &path, Patch *patch)
  {
    *patch = Patch();
    patch->path = path;
    patch->name = std::filesystem::path(path).stem().string();
    patch->title = patch->name;
    Util::Config::Node xml("xml");
    if (Util::Config::FromXMLFile(&xml, path))
    {
      patch->error = "not a readable XML file";
      return false;
    }
    const Util::Config::Node *root = xml.TryGet("patch");
    if (!root)
    {
      patch->error = "no <patch> element";
      return false;
    }
    if (!Text(*root, "name").empty())
      patch->name = Text(*root, "name");
    if (!Text(*root, "title").empty())
      patch->title = Text(*root, "title");
    patch->description = Paragraphs(Text(*root, "description"));
    ReadNames(*root, "requires", &patch->requires);
    ReadNames(*root, "conflicts", &patch->conflicts);
    if (!ReadFiles(*root, &patch->files, &patch->error))
      return false;
    for (const Util::Config::Node &node : *root)
    {
      if (node.Key() != "option")
        continue;
      Option option;
      option.name = Text(node, "name");
      option.title = Text(node, "title").empty() ? option.name : Text(node, "title");
      option.description = Paragraphs(Text(node, "description"));
      option.defaultValue = Text(node, "default");
      ReadNames(node, "requires", &option.requires);
      ReadNames(node, "conflicts", &option.conflicts);
      if (option.name.empty())
      {
        patch->error = "an <option> without a name";
        return false;
      }
      if (!ReadFiles(node, &option.files, &patch->error))
        return false;
      for (const Util::Config::Node &v : node)
      {
        if (v.Key() != "value")
          continue;
        Value value;
        value.name = Text(v, "name");
        value.title = Text(v, "title").empty() ? value.name : Text(v, "title");
        value.description = Paragraphs(Text(v, "description"));
        if (value.name.empty() || !ReadFiles(v, &value.files, &patch->error))
        {
          if (patch->error.empty())
            patch->error = "a <value> without a name in option " + option.name;
          return false;
        }
        option.values.push_back(std::move(value));
      }
      if (option.IsChoice())
      {
        if (std::none_of(option.values.begin(), option.values.end(), [&](const Value &v) { return v.name == option.defaultValue; }))
          option.defaultValue = option.values.front().name;
      }
      else if (option.defaultValue != "true")
        option.defaultValue = "false";
      patch->options.push_back(std::move(option));
    }
    return true;
  }

  std::vector<Patch> Read(const std::string &folder)
  {
    std::vector<Patch> patches;
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec))
      return patches;
    std::vector<std::filesystem::path> paths;
    for (auto it = std::filesystem::directory_iterator(folder, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
    {
      if (it->is_regular_file(ec) && Util::ToLower(it->path().extension().string()) == ".xml")
        paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto &path : paths)
    {
      Patch patch;
      ReadPatch(path.string(), &patch);
      patches.push_back(std::move(patch));
    }
    return patches;
  }

  std::string PatchKey(const Patch &patch)
  {
    return "Patch_" + patch.name;
  }

  std::string OptionKey(const Patch &patch, const Option &option)
  {
    return "Patch_" + patch.name + "_" + option.name;
  }

  bool IsTrue(const std::string &value)
  {
    std::string v = Util::ToLower(Trim(value));
    return v == "true" || v == "1" || v == "yes" || v == "on";
  }

  std::string OptionValue(const Option &option, const std::string &setting)
  {
    if (Trim(setting).empty())
      return option.defaultValue;
    if (!option.IsChoice())
      return IsTrue(setting) ? "true" : "false";
    for (const Value &value : option.values)
    {
      if (value.name == Trim(setting))
        return value.name;
    }
    return option.defaultValue;
  }

  // An option counts as on (for requirements and conflicts) when true, or for a choice, not at its default
  static bool OptionOn(const Option &option, const std::string &value)
  {
    return option.IsChoice() ? value != option.defaultValue : value == "true";
  }

  Patcher::Patcher(const std::vector<Patch> &patches, const std::function<std::string(const std::string &key)> &setting)
    : m_patches(patches)
  {
    // The patches turned on, then left out while one's requirements or conflicts are not met
    std::set<std::string> on;
    for (const Patch &patch : m_patches)
    {
      if (IsTrue(setting(PatchKey(patch))))
      {
        if (!patch.error.empty())
          m_errors.push_back(patch.title + ": " + patch.error);
        else
          on.insert(patch.name);
      }
    }
    for (bool changed = true; changed; )
    {
      changed = false;
      for (const Patch &patch : m_patches)
      {
        if (!on.count(patch.name))
          continue;
        std::string why;
        for (const std::string &name : patch.requires)
        {
          if (!on.count(name))
            why = "it needs the patch " + name + ", which is not on";
        }
        for (const std::string &name : patch.conflicts)
        {
          if (on.count(name))
            why = "it cannot go with the patch " + name;
        }
        if (!why.empty())
        {
          m_errors.push_back(patch.title + " not applied: " + why);
          on.erase(patch.name);
          changed = true;
        }
      }
    }

    for (const Patch &patch : m_patches)
    {
      if (!on.count(patch.name))
        continue;
      Selected selected{ &patch, {}, patch.title };
      for (const FileChanges &file : patch.files)
        selected.files.push_back(&file);
      std::map<std::string, std::string> values;
      for (const Option &option : patch.options)
        values[option.name] = OptionValue(option, setting(OptionKey(patch, option)));
      std::string list;
      for (const Option &option : patch.options)
      {
        const std::string &value = values[option.name];
        if (!OptionOn(option, value))
          continue;
        std::string why;
        for (const std::string &name : option.requires)
        {
          auto it = std::find_if(patch.options.begin(), patch.options.end(), [&](const Option &o) { return o.name == name; });
          if (it == patch.options.end() || !OptionOn(*it, values[name]))
            why = "it needs the option " + name;
        }
        for (const std::string &name : option.conflicts)
        {
          auto it = std::find_if(patch.options.begin(), patch.options.end(), [&](const Option &o) { return o.name == name; });
          if (it != patch.options.end() && OptionOn(*it, values[name]))
            why = "it cannot go with the option " + name;
        }
        if (!why.empty())
        {
          m_errors.push_back(patch.title + ", option " + option.title + " not applied: " + why);
          continue;
        }
        if (option.IsChoice())
        {
          for (const Value &v : option.values)
          {
            if (v.name == value)
            {
              for (const FileChanges &file : v.files)
                selected.files.push_back(&file);
              list += (list.empty() ? "" : ", ") + option.title + " = " + v.title;
            }
          }
        }
        else
        {
          for (const FileChanges &file : option.files)
            selected.files.push_back(&file);
          list += (list.empty() ? "" : ", ") + option.title;
        }
      }
      if (!list.empty())
        selected.label += " (" + list + ")";
      m_selected.push_back(std::move(selected));
    }
  }

  void Patcher::Check(const std::map<std::string, uint32_t> &files)
  {
    std::map<std::string, uint32_t> lower;
    for (const auto &[name, crc] : files)
      lower[Util::ToLower(name)] = crc;

    // Each file the game's (as its ROMs have it); then no byte changed twice (the first patch kept)
    std::map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> used;   // file: [start, end)
    std::vector<Selected> kept;
    for (Selected &selected : m_selected)
    {
      std::string why;
      std::map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> mine;
      for (const FileChanges *file : selected.files)
      {
        std::string name = Util::ToLower(file->file);
        auto it = lower.find(name);
        if (it == lower.end())
          why = file->file + " is not one of the game's files";
        else if (it->second != file->crc32)
          why = file->file + " is another version (CRC32 " + Util::Hex(it->second, 8) + ", the patch's is for " + Util::Hex(file->crc32, 8) + ")";
        for (const Change &change : file->changes)
          mine[name].push_back({ change.offset, change.offset + uint32_t(change.bytes.size()) });
      }
      for (auto &[name, ranges] : mine)
      {
        std::vector<std::pair<uint32_t, uint32_t>> all = used[name];
        all.insert(all.end(), ranges.begin(), ranges.end());
        std::sort(all.begin(), all.end());
        for (size_t i = 1; i < all.size() && why.empty(); i++)
        {
          if (all[i].first < all[i - 1].second)
            why = "it changes bytes of " + name + " another change does (at " + Util::Hex(all[i].first, 8) + ")";
        }
      }
      if (!why.empty())
      {
        m_errors.push_back(selected.patch->title + " not applied: " + why);
        continue;
      }
      for (auto &[name, ranges] : mine)
        used[name].insert(used[name].end(), ranges.begin(), ranges.end());
      kept.push_back(std::move(selected));
    }
    m_selected = std::move(kept);

    m_byFile.clear();
    m_applied.clear();
    for (const Selected &selected : m_selected)
    {
      for (const FileChanges *file : selected.files)
        m_byFile[Util::ToLower(file->file)].push_back(file);
      m_applied.push_back(selected.label);
    }
    for (const std::string &error : m_errors)
      ErrorLog("ROM patch: %s.", error.c_str());
  }

  bool Patcher::Apply(const std::string &file, uint8_t *data, size_t size)
  {
    auto it = m_byFile.find(Util::ToLower(file));
    if (it == m_byFile.end())
      return false;
    size_t count = 0;
    for (const FileChanges *changes : it->second)
    {
      for (const Change &change : changes->changes)
      {
        if (size_t(change.offset) + change.bytes.size() > size)
        {
          ErrorLog("ROM patch: a change past the end of %s (at 0x%X) not applied.", file.c_str(), change.offset);
          continue;
        }
        std::copy(change.bytes.begin(), change.bytes.end(), data + change.offset);
        count += change.bytes.size();
      }
    }
    InfoLog("ROM patch: %u bytes of %s changed.", unsigned(count), file.c_str());
    if (!m_dumpFolder.empty())
    {
      std::error_code ec;
      std::filesystem::create_directories(m_dumpFolder, ec);
      std::filesystem::path path = std::filesystem::path(m_dumpFolder) / file;
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char *>(data), std::streamsize(size));
      if (out)
      {
        m_dumped.push_back(path.string());
        InfoLog("ROM patch: %s written as patched to '%s'.", file.c_str(), path.string().c_str());
      }
      else
        ErrorLog("ROM patch: unable to write '%s'.", path.string().c_str());
    }
    return true;
  }
}
