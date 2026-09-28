#include "sotc/window_icon.h"
#include "raylib.h"

namespace sotc
{
    void applyWindowIcon(const unsigned char *bytes, int size)
    {
        Image icon = LoadImageFromMemory(".png", bytes, size);
        if (icon.data)
        {
            SetWindowIcon(icon);
            UnloadImage(icon);
        }
    }
}
