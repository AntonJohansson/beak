#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define NUM_COLORS 5

#define ARRLEN(arr) \
    (sizeof(arr) / sizeof((arr)[0]))

#define MAX(a, b) \
    ((a > b) ? a : b)

#define MIN(a, b) \
    ((a < b) ? a : b)

#define CLAMP(lower, x, upper) \
    (MIN(upper, MAX(lower, x)))

//
// UndoLog: Storage for all previous images you can get to via
//          undo/redo actions. We store one Image for each previous
//          stage.
//
// images: Array of `size` images describing undo states.
// used_size: Number of images actually used.
// top: Index of most recent image pushed (note this will wrap).
// selected: Index of the currently selected image. Used to keep track of which
//           entry is being viewed.
//
typedef struct UndoLog {
    Image *images;
    size_t size;
    size_t used_size;
    size_t top;
    size_t selected;
} UndoLog;

// Makes a GPU->CPU copy of framebuffer and pushes it onto the undo log
static inline void undo_log_push(UndoLog *log, RenderTexture2D framebuffer) {
    // If we're out of space we know the entry is already occupied
    // so unload it first.
    assert(log->used_size <= log->size);
    if (log->used_size == log->size)
        UnloadImage(log->images[log->top]);

    // Copy texture to log
    log->images[log->top] = LoadImageFromTexture(framebuffer.texture);

    // Update indices/used_size
    log->selected = log->top;
    log->top = (log->top + 1) % log->size;
    if (log->used_size < log->size)
        ++log->used_size;
}

// Makes a CPU->GPU copy of log entry to framebuffer
static inline void undo_log_copy(UndoLog *log, RenderTexture2D framebuffer, int offset) {
    // Select the log entry at the requested offset
    log->selected = (log->selected + log->size + offset) % log->size;

    // Load the image to a texture and draw it to the framebuffer
    Texture2D texture = LoadTextureFromImage(log->images[log->selected]);
    BeginTextureMode(framebuffer);
    // Negate the height of the texture to flip it vertically
    DrawTextureRec(texture, (Rectangle){0, 0, texture.width, -texture.height}, (Vector2){0, 0}, WHITE);
    EndTextureMode();

    UnloadTexture(texture);
}

// Mapping ints to linearly spaced HSV colors
static inline Color get_brush_color(int i) {
    return ColorFromHSV(fmodf(360.0f * ((float)i/(float)NUM_COLORS), 360.0f), 0.75f, 0.75f);
}

// Clear framebuffer to color and then push new state to undo log
static void clear_framebuffer(UndoLog *log, RenderTexture2D framebuffer, Color color) {
    BeginTextureMode(framebuffer);
    ClearBackground(color);
    EndTextureMode();
    undo_log_push(log, framebuffer);
}

typedef enum CmdLineOptionType {
    CMDLINE_OPTION_STR,
    CMDLINE_OPTION_ULONG,
    CMDLINE_OPTION_HEX,
} CmdLineOptionType;

typedef struct CmdLineOption {
    const char *name;
    const char *format;
    CmdLineOptionType type;
    union {
        const char **str;
        unsigned long *ulong;
    };
    unsigned int base;
} CmdLineOption;

int main(int argc, char **argv) {
    unsigned long canvas_width  = 2560;
    unsigned long canvas_height = 1440;
    unsigned long window_width  = 800;
    unsigned long window_height = 600;
    unsigned long undo_log_size = 16;
    unsigned long background_hexcolor = 0x111600FF;
    const char *save_path = "beak.png";

    CmdLineOption options[] = {
        {"--canvas-width",  "ulong",               CMDLINE_OPTION_ULONG, .ulong = &canvas_width},
        {"--canvas-height", "ulong",               CMDLINE_OPTION_ULONG, .ulong = &canvas_height},
        {"--window-width",  "ulong",               CMDLINE_OPTION_ULONG, .ulong = &window_width},
        {"--window-height", "ulong",               CMDLINE_OPTION_ULONG, .ulong = &window_height},
        {"--undo-log-size", "ulong",               CMDLINE_OPTION_ULONG, .ulong = &undo_log_size},
        {"--background",    "0xRRGGBBAA",          CMDLINE_OPTION_HEX,   .ulong = &background_hexcolor},
        {"--save-path",     "/save/path/file.png", CMDLINE_OPTION_STR,   .str   = &save_path},
    };

    //
    // Parse commandline args
    //
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (strncmp(option, "--help", 6) == 0) {
            puts("beak [options]\n");
            printf("%-16s%-24s%-16s\n", "option", "format", "default");
            for (unsigned i = 0; i < ARRLEN(options); ++i) {
                printf("%-16s%-24s", options[i].name, options[i].format);
                switch (options[i].type) {
                case CMDLINE_OPTION_STR:
                    printf("%-16s", *options[i].str);
                    break;
                case CMDLINE_OPTION_ULONG:
                    printf("%-16lu", *options[i].ulong);
                    break;
                case CMDLINE_OPTION_HEX:
                    printf("0x%-14lx", *options[i].ulong);
                    break;
                }
                putchar('\n');
            }
            putchar('\n');
            puts("keybinds:");
            puts("1-5           Use n:th color");
            puts("q, mouse 4    Undo");
            puts("w, mouse 5    Redo");
            puts("c             Clear");
            puts("s             Save image to --save-path");
            puts("mouse wheel   Change brush size");
            puts("mouse 1       Paint");
            puts("mouse 2       Erase");
            puts("mouse 3       Pan");
            return 0;
        }

        if (i+1 >= argc) {
            fprintf(stderr,"[error]: Missing value for option %s\n", option);
            return -1;
        }

        const char *value = argv[++i];

        for (unsigned i = 0; i < ARRLEN(options); ++i) {
            if (strcmp(option, options[i].name) != 0)
                continue;
            switch (options[i].type) {
            case CMDLINE_OPTION_STR:
                *options[i].str = value;
                break;
            case CMDLINE_OPTION_ULONG:
                *options[i].ulong = strtoul(value, NULL, 10);
                if (*options[i].ulong == ULONG_MAX) {
                    fprintf(stderr, "[error]: Invalid %s option %s\n", option, value);
                    return -1;
                }
                break;
            case CMDLINE_OPTION_HEX:
                *options[i].ulong = strtoul(value, NULL, 16);
                if (*options[i].ulong == ULONG_MAX) {
                    fprintf(stderr, "[error]: Invalid %s option %s\n", option, value);
                    return -1;
                }
                break;
            }
        }
    }

    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(window_width, window_height, "floating");
    HideCursor();

    UndoLog log = {
        .images = malloc(undo_log_size * sizeof(Image)),
        .size = undo_log_size,
    };

    RenderTexture2D framebuffer = LoadRenderTexture(canvas_width, canvas_height);

    Color background = GetColor(background_hexcolor);

    clear_framebuffer(&log, framebuffer, background);

    int target_x = 400;
    int target_y = 300;

    float brush_radius = 10.0f;

    Vector2 prev_mouse_pos = {0};
    Vector2 mouse_pos = {0};

    Color brush_color = get_brush_color(0);
    while (!WindowShouldClose()) {
        const int w = GetScreenWidth();
        const int h = GetScreenHeight();

        // Handle chaning of brush color
        const int key = GetKeyPressed();
        if (key >= KEY_ONE && key <= KEY_FIVE)
            brush_color = get_brush_color(key - KEY_ONE);

        const float scroll = GetMouseWheelMove();
        if (scroll != 0.0f) {
            brush_radius += 5.0f*scroll;
            if (brush_radius <= 0.0f)
                brush_radius = 1.0f;
        }

        if (IsKeyPressed(KEY_C)) {
            clear_framebuffer(&log, framebuffer, background);
        }

        if (IsKeyPressed(KEY_S)) {
            Image image = LoadImageFromTexture(framebuffer.texture);
            ExportImage(image, save_path);
            UnloadImage(image);
        }

        //
        // Handle interactivity related setting/copying/clearing
        // the undo log.
        //

        // Compute distance between the `top` of the undo log, and the `selected` entry.
        const size_t log_top_dist = (log.top + log.size - log.selected) % log.size;

        // If the distance to the top of the log is > 1 then it means
        // we have selected a previous entry, if so we need to handle
        // going forwards in the log, but also clearing the log from
        // the selected entry to the top if the user starts drawing
        // again.
        if (log_top_dist > 1) {
            // Clear the log from selected -> top
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                for (size_t i = (log.selected + 1) % log.size; i != log.top; i = (i + 1) % log.size) {
                    UnloadImage(log.images[i]);
                }
                // Set the new top to be one past the selected entry;
                log.top = (log.selected + 1) % log.size;
                // We remove log_top_dist-1 entries since we want to keep the log entry
                // for undo_log_selected.
                log.used_size -= log_top_dist - 1;
            }

            // Handle going forwards in the log
            if (IsKeyPressed(KEY_W) || IsMouseButtonPressed(MOUSE_BUTTON_EXTRA)) {
                undo_log_copy(&log, framebuffer, 1);
            }
        }

        // Handle going backwards in the log
        if (log_top_dist < log.used_size && (IsKeyPressed(KEY_Q) || IsMouseButtonPressed(MOUSE_BUTTON_SIDE))) {
            undo_log_copy(&log, framebuffer, -1);
        }

        // When the user releases the mouse we want to push a new entry
        // into the undo log.
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            undo_log_push(&log, framebuffer);
        }

        //
        // Handle camera panning
        //

        prev_mouse_pos = mouse_pos;
        mouse_pos = GetMousePosition();

        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            target_x -= mouse_pos.x - prev_mouse_pos.x;
            target_y -= mouse_pos.y - prev_mouse_pos.y;

            // Don't allow the camera target to stray outside the canvas
            target_x = CLAMP(w/2, target_x, MAX(0, (int)canvas_width  - w/2));
            target_y = CLAMP(h/2, target_y, MAX(0, (int)canvas_height - h/2));
        }

        if (IsWindowResized()) {
            // Don't allow the camera target to stray outside the canvas
            target_x = CLAMP(w/2, target_x, MAX(0, (int)canvas_width  - w/2));
            target_y = CLAMP(h/2, target_y, MAX(0, (int)canvas_height - h/2));
        }

        //
        // Drawing
        //

        BeginTextureMode(framebuffer);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            // On left-click draw with selected color, otherwise draw with the background color
            // to "erase."
            Color color = (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) ? brush_color : background;
            // Since the framebuffer we're drawing to is much larger than the size of the
            // window (probably), we need to calculate some new coordinates.
            Vector2 offset = {target_x - w/2, target_y - h/2};
            Vector2 start = Vector2Add(offset, prev_mouse_pos);
            Vector2 end = Vector2Add(offset, mouse_pos);
            // Invert y-coord as texture/raylib y-coords are inverted.
            start.y = canvas_height - start.y;
            end.y = canvas_height - end.y;
            // Draw a capsule from the previous mouse position to the current
            // one.
            DrawCircleV(start, brush_radius, color);
            DrawLineEx(start, end, 2.0f*brush_radius, color);
            DrawCircleV(end, brush_radius, color);
        }
        EndTextureMode();

        BeginDrawing();
        ClearBackground(background);
        // Draw what the use has painted
        DrawTextureRec(framebuffer.texture, (Rectangle){target_x-w/2,target_y-h/2,w,h}, (Vector2){0,0}, WHITE);
        // Draw cursor
        DrawCircleLines(mouse_pos.x, mouse_pos.y, brush_radius, WHITE);
        DrawCircleLines(mouse_pos.x, mouse_pos.y, 1.2f*brush_radius, brush_color);
        EndDrawing();
    }

    free(log.images);

    UnloadRenderTexture(framebuffer);
    ShowCursor();
    CloseWindow();

    return 0;
}
