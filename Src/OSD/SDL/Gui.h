#ifndef _GUI_H_
#define _GUI_H_

#include <string>
#include <vector>

namespace Util
{
  namespace Config
  {
    class Node;
  }
}

// Runs the settings GUI. fileConfig is the complete contents of the config
// file; only its global section is edited, all other sections are preserved
// when the file is written back out.
// Returns the zip file to load (none to quit), and in *game the game to load from it
std::vector<std::string> RunGUI(const std::string& configPath, const Util::Config::Node& fileConfig, std::string* game);

#endif // !_GUI_H_
