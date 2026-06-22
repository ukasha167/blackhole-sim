#include <raylib.h>

#include "defines.hpp"

int main()
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "HER EYES.");
    SetTargetFPS(62);

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(BLACK);

        DrawText(TextFormat("FPS: %d\n", GetFPS()), 10, 10, 24, WHITE);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
