#include <wayfire/util/log.hpp>
#include "wayfire/img.hpp"
#include "wayfire/opengl.hpp"

#include <config.h>

#ifdef BUILD_WITH_IMAGEIO
 #include <png.h>
 #include <jpeglib.h>
 #include <jerror.h>
#endif

#include <stdint.h>
#include <unistd.h>
#include <cstdio>
#include <unordered_map>
#include <functional>

#define TEXTURE_LOAD_ERROR 0

namespace image_io
{
using Loader = std::function<bool (const char*, GLuint)>;
using Writer = std::function<void (const char*name, uint8_t*pixels, unsigned long,
    unsigned long)>;
namespace
{
std::unordered_map<std::string, Loader> loaders;
std::unordered_map<std::string, Writer> writers;
}

#ifdef BUILD_WITH_IMAGEIO
/* All backend functions are taken from the internet.
 * If you want to be credited, contact me */
bool texture_from_png(const char *filename, GLuint target)
{
    FILE *fp = fopen(filename, "rb");
    int width, height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
    {
        return false;
    }

    png_infop infos = png_create_info_struct(png);
    if (!infos)
    {
        return false;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, infos);

    width  = png_get_image_width(png, infos);
    height = png_get_image_height(png, infos);
    color_type = png_get_color_type(png, infos);
    bit_depth  = png_get_bit_depth(png, infos);

    if (bit_depth == 16)
    {
        png_set_strip_16(png);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(png);
    }

    // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
    if ((color_type == PNG_COLOR_TYPE_GRAY) && (bit_depth < 8))
    {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    if (png_get_valid(png, infos, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(png);
    }

    // These color_type don't have an alpha channel then fill it with 0xff.
    if ((color_type == PNG_COLOR_TYPE_RGB) ||
        (color_type == PNG_COLOR_TYPE_GRAY) ||
        (color_type == PNG_COLOR_TYPE_PALETTE))
    {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    if ((color_type == PNG_COLOR_TYPE_GRAY) ||
        (color_type == PNG_COLOR_TYPE_GRAY_ALPHA))
    {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, infos);

    row_pointers = new png_bytep[height];
    png_byte *data = new png_byte[height * png_get_rowbytes(png, infos)];

    for (int i = 0; i < height; i++)
    {
        row_pointers[i] = data + i * png_get_rowbytes(png, infos);
    }

    png_read_image(png, row_pointers);
    GL_CALL(glTexImage2D(target, 0, GL_RGBA, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)data));

    png_destroy_read_struct(&png, &infos, NULL);
    delete[] row_pointers;
    delete[] data;

    fclose(fp);

    return true;
}

void texture_to_png(const char *name, uint8_t *pixels, int w, int h)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
        nullptr, nullptr);
    if (!png)
    {
        return;
    }

    png_infop infot = png_create_info_struct(png);
    if (!infot)
    {
        png_destroy_write_struct(&png, &infot);

        return;
    }

    FILE *fp = fopen(name, "wb");
    if (!fp)
    {
        png_destroy_write_struct(&png, &infot);

        return;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, infot, w, h, 8 /* depth */, PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_colorp palette =
        (png_colorp)png_malloc(png, PNG_MAX_PALETTE_LENGTH * sizeof(png_color));
    if (!palette)
    {
        fclose(fp);
        png_destroy_write_struct(&png, &infot);

        return;
    }

    png_set_PLTE(png, infot, palette, PNG_MAX_PALETTE_LENGTH);
    png_write_info(png, infot);
    png_set_packing(png);

    png_bytepp rows = (png_bytepp)png_malloc(png, h * sizeof(png_bytep));
    for (int i = 0; i < h; ++i)
    {
        rows[i] = (png_bytep)(pixels + (h - i) * w * 4);
    }

    png_write_image(png, rows);
    png_write_end(png, infot);
    png_free(png, palette);
    png_destroy_write_struct(&png, &infot);

    fclose(fp);
    delete[] rows;
}

bool texture_from_jpeg(const char *FileName, GLuint target)
{
    unsigned long data_size;
    unsigned char *rowptr[1];
    unsigned char *jdata;
    struct jpeg_decompress_struct infot;
    struct jpeg_error_mgr err;

    std::FILE *file = fopen(FileName, "rb");
    infot.err = jpeg_std_error(&err);
    jpeg_create_decompress(&infot);

    if (!file)
    {
        LOGE("failed to read JPEG file ", FileName);

        return false;
    }

    jpeg_stdio_src(&infot, file);
    jpeg_read_header(&infot, TRUE);
    jpeg_start_decompress(&infot);

    data_size = infot.output_width * infot.output_height * 3;

    jdata = new unsigned char [data_size];
    while (infot.output_scanline < infot.output_height)
    {
        rowptr[0] = (unsigned char*)jdata + 3 * infot.output_width *
            infot.output_scanline;
        jpeg_read_scanlines(&infot, rowptr, 1);
    }

    jpeg_finish_decompress(&infot);

    GL_CALL(glTexImage2D(target, 0, GL_RGB,
        infot.output_width, infot.output_height,
        0, GL_RGB, GL_UNSIGNED_BYTE, jdata));

    fclose(file);
    delete[] jdata;

    return true;
}

#endif

bool load_from_file(std::string name, GLuint target)
{
    if (access(name.c_str(), F_OK) == -1)
    {
        if (!name.empty())
        {
            LOGE(__func__, "() cannot access ", name);
        }

        return false;
    }

    int len = name.length();
    if ((len < 4) || (name[len - 4] != '.'))
    {
        LOGE(
            "load_from_file() called with file without extension or with invalid extension!");

        return false;
    }

    auto ext = name.substr(len - 3, 3);
    for (int i = 0; i < 3; i++)
    {
        ext[i] = std::tolower(ext[i]);
    }

    auto it = loaders.find(ext);
    if (it == loaders.end())
    {
        LOGE("load_from_file() called with unsupported extension ", ext);

        return false;
    } else
    {
        return it->second(name.c_str(), target);
    }
}

void write_to_file(std::string name, uint8_t *pixels, int w, int h, std::string type)
{
    auto it = writers.find(type);

    if (it == writers.end())
    {
        LOGE("unsupported image_writer backend");
    } else
    {
        it->second(name.c_str(), pixels, w, h);
    }
}

void init()
{
    LOGD("init ImageIO");
#ifdef BUILD_WITH_IMAGEIO
    loaders["png"] = Loader(texture_from_png);
    loaders["jpg"] = Loader(texture_from_jpeg);
    writers["png"] = Writer(texture_to_png);
#endif
}
}
