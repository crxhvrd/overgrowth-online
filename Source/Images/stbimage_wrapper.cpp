//-----------------------------------------------------------------------------
//           Name: stbimage_wrapper.cpp
//      Developer: Wolfire Games LLC
//         Author: Stephan Vedder
//    Description: Wraps up stb_image library
//        License: Read below
//-----------------------------------------------------------------------------
//
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

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
// Overgrowth Online: our paths are UTF-8; stb's plain fopen failed under a
// non-Latin folder (OneDrive\Документы: "can't fopen" __weights.png in Stucco
// Courtyard; SavePNG works around it with short paths, loading didn't)
#define STBI_WINDOWS_UTF8
#include "stbimage_wrapper.h"
