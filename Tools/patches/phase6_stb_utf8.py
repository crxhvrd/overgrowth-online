"""Phase 6: images under a non-Latin write dir (applied by hand, recorded here).

Source/Images/stbimage_wrapper.cpp defines STBI_WINDOWS_UTF8 before the stb
implementation: stbi_load opened UTF-8 paths with plain fopen, which fails
under C:\Users\<name>\OneDrive\Документы. Seen as "Asset Load Error ...
IMAGE_SAMPLER_ASSET __weights.png: can't fopen" loading Stucco Courtyard (the
terrain's first pass writes its weights to the write dir and reads them back).
"""
