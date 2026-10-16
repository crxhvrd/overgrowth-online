//-----------------------------------------------------------------------------
//           Name: image_export_test.cpp
//      Developer: Overgrowth Online (crxhvrd)
//    Description: ImageExport::SavePNG takes BGRA, 4 bytes a pixel (the
//                 terrain's normal and weight maps) and must write the colours
//                 back as they were: written as 3 bytes a pixel over the
//                 4-byte stride, every pixel slid by a byte (striped normal
//                 maps, dark bands over the terrain).
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#include <Images/image_export.hpp>
#include <Internal/filesystem.h>
#include <Internal/modid.h>

#include <tut/tut.hpp>
#include <Images/stbimage_wrapper.h>

#include <cstdio>
#include <string>
#include <vector>

namespace tut {
struct dataimageexport {
    int placeholder;
};

typedef test_group<dataimageexport> tg_image_export;
tg_image_export test_group_image_export("ImageExport");

typedef tg_image_export::object imageexport;

template <>
template <>
void imageexport::test<1>() {
    // 3 x 2 pixels, BGRA: each a different colour
    const int w = 3, h = 2;
    const unsigned char rgb[w * h][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {10, 20, 30}, {200, 100, 50}, {1, 128, 254}};
    std::vector<unsigned char> bgra(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        bgra[i * 4 + 0] = rgb[i][2];
        bgra[i * 4 + 1] = rgb[i][1];
        bgra[i * 4 + 2] = rgb[i][0];
        bgra[i * 4 + 3] = 255;
    }

    std::string path = std::string(GetWritePath(CoreGameModID).c_str()) + "image_export_test.png";
    ImageExport::SavePNG(path.c_str(), bgra.data(), w, h);

    int rw = 0, rh = 0, n = 0;
    unsigned char* back = stbi_load(path.c_str(), &rw, &rh, &n, 3);
    ensure("read back", back != nullptr);
    ensure("size", rw == w && rh == h);
    bool same = true;
    for (int i = 0; i < w * h; ++i) {
        for (int c = 0; c < 3; ++c) {
            same = same && back[i * 3 + c] == rgb[i][c];
        }
    }
    stbi_image_free(back);
    remove(path.c_str());
    ensure("colours as given", same);
}
}  // namespace tut
