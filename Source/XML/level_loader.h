//-----------------------------------------------------------------------------
//           Name: level_loader.h
//      Developer: Wolfire Games LLC
//    Description:
//        License: Read below
//-----------------------------------------------------------------------------
//
//   Copyright 2022 Wolfire Games LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//-----------------------------------------------------------------------------
#pragma once

#include <Editors/entity_type.h>

class TiXmlDocument;
class TiXmlNode;
class SceneGraph;

namespace LevelLoader {
bool LoadLevel(const Path& level_path, SceneGraph& s);
enum SaveLevelType {
    kSaveInPlace,
    kSaveAs
};

void SaveLevel(SceneGraph& s, SaveLevelType type);
// Overgrowth Online: the level as it is now, written to another file (an autosave while building);
// the scene keeps its own path and name
void SaveLevelCopy(SceneGraph& s, const std::string& abs_path);
// Overgrowth Online: whether saving in place would ask where (Save As): a level never saved from the
// game's own folders, or none. PointLevelAt makes a file (in the write folder) the level's own, so the
// next save goes there (false when its path doesn't resolve)
bool SaveNeedsDialog(SceneGraph& s);
bool PointLevelAt(SceneGraph& s, const std::string& abs_path);
void SaveTerrain(TiXmlNode* doc, SceneGraph* s);
}  // namespace LevelLoader

EntityType CheckGenericType(TiXmlDocument& doc);
