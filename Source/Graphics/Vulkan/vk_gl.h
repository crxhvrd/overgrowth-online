//-----------------------------------------------------------------------------
//           Name: vk_gl.h
//      Developer: Overgrowth Online (crxhvrd)
//    Description: The Vulkan renderer's resources (stage 5.3 of
//                 docs_gl46_plan.md), shaped like the GL calls the engine makes:
//                 each vkgl:: function takes what its GL twin takes, keyed by the
//                 GL-style names the engine keeps (gl_texture_id, gl_VBO, ...),
//                 so a GL call site turns into GLVK(TextureStorage2D)(...).
//                 Textures are images (3-channel ones as 4, cleared when made,
//                 resting in their sampled layout between operations), buffers
//                 are VMA buffers (host-visible when GL would map them), samplers
//                 are made from their GL parameters, fences are submit serials.
//                 Transfers go into the frame's command buffer in GL's order,
//                 through staging memory
//        License: Apache-2.0, like the rest of the engine (see LICENSE)
//-----------------------------------------------------------------------------
#pragma once

#include <Graphics/render_backend.h>

#include <opengl.h>

#include <cstddef>
#include <cstdint>

// The GL call, or its Vulkan twin on the Vulkan renderer:
//   GLVK(TextureStorage2D)(id, levels, format, width, height);
#define GLVK(name) (UseVulkan() ? &vkgl::name : gl##name)

namespace vkgl {

// ---- textures
void APIENTRY CreateTextures(GLenum target, GLsizei n, GLuint* textures);
void APIENTRY GenTextures(GLsizei n, GLuint* textures);  // (a name with no target yet: TextureView's)
void APIENTRY DeleteTextures(GLsizei n, const GLuint* textures);
GLboolean APIENTRY IsTexture(GLuint texture);
void APIENTRY TextureStorage2D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
void APIENTRY TextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
void APIENTRY TextureStorage2DMultisample(GLuint texture, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height,
                                          GLboolean fixedsamplelocations);
void APIENTRY TextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,
                                GLenum type, const void* pixels);
void APIENTRY TextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                GLsizei depth, GLenum format, GLenum type, const void* pixels);
void APIENTRY CompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                                          GLenum format, GLsizei imageSize, const void* data);
void APIENTRY CompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width,
                                          GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void* data);
void APIENTRY GenerateTextureMipmap(GLuint texture);
void APIENTRY TextureView(GLuint texture, GLenum target, GLuint origtexture, GLenum internalformat, GLuint minlevel, GLuint numlevels,
                          GLuint minlayer, GLuint numlayers);
void APIENTRY TextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer);
void APIENTRY GetTextureLevelParameteriv(GLuint texture, GLint level, GLenum pname, GLint* params);
// Readbacks wait for the GPU (like GL's)
void APIENTRY GetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels);
void APIENTRY GetTextureSubImage(GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height,
                                 GLsizei depth, GLenum format, GLenum type, GLsizei bufSize, void* pixels);
void APIENTRY GetCompressedTextureImage(GLuint texture, GLint level, GLsizei bufSize, void* pixels);

// ---- buffers
void APIENTRY CreateBuffers(GLsizei n, GLuint* buffers);
void APIENTRY GenBuffers(GLsizei n, GLuint* buffers);
void APIENTRY DeleteBuffers(GLsizei n, const GLuint* buffers);
GLboolean APIENTRY IsBuffer(GLuint buffer);
void APIENTRY NamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags);
void APIENTRY NamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage);
void APIENTRY NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data);
void* APIENTRY MapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLboolean APIENTRY UnmapNamedBuffer(GLuint buffer);
void APIENTRY GetNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, void* data);

// ---- samplers
void APIENTRY CreateSamplers(GLsizei n, GLuint* samplers);
void APIENTRY DeleteSamplers(GLsizei count, const GLuint* samplers);
void APIENTRY SamplerParameteri(GLuint sampler, GLenum pname, GLint param);
void APIENTRY SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);

// ---- fences (a submit's serial)
GLsync APIENTRY FenceSync(GLenum condition, GLbitfield flags);
GLenum APIENTRY ClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout);
void APIENTRY DeleteSync(GLsync sync);

// ---- limits (the device's, as GL names them)
void APIENTRY GetIntegerv(GLenum pname, GLint* data);
void APIENTRY GetFloatv(GLenum pname, GLfloat* data);

// ---- for the passes (stages 5.4-5.6) and the checks
struct Stats {
    size_t images = 0, image_bytes = 0;
    size_t buffers = 0, buffer_bytes = 0;
    size_t samplers = 0;
    size_t staging_bytes = 0;  // staging memory held (in use or kept)
    uint64_t uploads = 0, upload_bytes = 0;
};
Stats GetStats();

// The next texture made is written from compute too (an upscaler's output, FSR's reactive
// mask): storage usage on it. Only those: on others it costs AMD's colour compression
void NextTextureWritable();

#ifdef VK_DEFINE_HANDLE
struct ImageInfo {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;  // the texture's whole range, its target's view type
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;  // between operations
    uint32_t width = 0, height = 0, levels = 0, layers = 0, samples = 1;
};
bool Image(GLuint texture, ImageInfo* out);
VkBuffer Buffer(GLuint buffer, VkDeviceSize* size);
VkBufferView TexelBuffer(GLuint texture);  // a TextureBuffer's view of its buffer as it is now
VkSampler Sampler(GLuint sampler);
// (diagnostics: a sampler's GL compare mode and function, min filter; false: no such sampler)
bool SamplerParams(GLuint sampler, GLint* compare_mode, GLint* compare_func, GLint* min_filter);

// A texture's level and layer (-1: the texture itself) drawn into: moved to the
// attachment layout until EndAttachment (read_only, a depth texture: the read-only depth
// layout, which shaders may sample while it's attached)
struct Attachment {
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t width = 0, height = 0, samples = 1;
    bool depth = false;
};
bool BeginAttachment(GLuint texture, int level, int layer, Attachment* out, bool read_only = false);
void EndAttachment(GLuint texture, int level, int layer);
// Overgrowth Online (TAA): a texture's level 0, layer 0 for work outside the passes (an
// upscaler's own compute, recorded into the frame's commands): moved to `layout` until
// EndExternal puts it back to rest. `view` is of that level alone. `whole`: every level and
// layer moved (FidelityFX's barriers take the whole image, whatever it's told)
bool BeginExternal(GLuint texture, VkImageLayout layout, ImageInfo* out, bool whole = false);
void EndExternal(GLuint texture, bool whole = false);

// Graphics::BlitRenderTarget: rects are x0, y0, x1, y1 (GL's: rows counted as
// stored); a multisampled source is resolved
void Blit(GLuint src, int src_level, int src_layer, const int src_rect[4], GLuint dst, int dst_level, int dst_layer, const int dst_rect[4],
          bool depth, bool linear);
// The window's frame into a swapchain image (in TRANSFER_DST), flipped
void BlitToSwapchain(GLuint src, VkImage dst, uint32_t width, uint32_t height);
#endif

}  // namespace vkgl
