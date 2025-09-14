/**
 * holo.c - 14-Segment ASCII Art Renderer
 *
 * Copyright (c) 2025 Olivier Coz
 *
 * This software is released under the MIT License.
 * See the LICENSE file for details.
 */

/**
 * 14-Segment ASCII Art Renderer
 *
 * A C program that renders rotating 3D text using a 14-segment display
 * model, outputting to the terminal as ASCII art.
 *
 * This project is heavily inspired by the mechanics and design of the famous donut.c.
 *
 * --- Inspiration & Credits ---
 * 1. Donut Math: The core 3D projection and ASCII rendering concepts are
 *    based on the principles explained in Andy Sloane's "Donut math: how
 *    donut.c works".
 *    https://www.a1k0n.net/2011/07/20/donut-math.html
 *
 * 2. 14-Segment Font Data: The bit-packed font data for the 14-segment
 *    display characters is adapted from Dave Madison's LED-Segment-ASCII
 *    library.
 *    https://github.com/dmadison/LED-Segment-ASCII/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <getopt.h>
#include <signal.h> // For graceful exit and window resizing

// For precise timing and date/time functions
#ifdef _WIN32
#include <windows.h>
#include <time.h> // For time() and strftime()
#else
#include <time.h> // For nanosleep, clock_gettime, time(), and strftime()
#endif

// For M_PI on some compilers
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Constants & Configuration ---
#define DEFAULT_SPEED_A         0.04f
#define DEFAULT_SPEED_B         0.02f
#define DEFAULT_WIDTH           8.0f
#define DEFAULT_HEIGHT          12.0f
#define DEFAULT_TILT            0.3f
#define DEFAULT_SPACING_FACTOR  1.5f
#define DEFAULT_SEG_WIDTH       1.75f
#define DEFAULT_SEG_THICK       1.75f
#define DEFAULT_POINT_LEN       0.85f
#define DEFAULT_LIGHT_X         0.3f
#define DEFAULT_LIGHT_Y         0.7f
#define DEFAULT_CONTRAST        20.0f
#define DEFAULT_PALETTE         ".,-~:;=!*#$@"
#define DEFAULT_DENSITY         0.1f
#define DEFAULT_TIME_FORMAT     "%H:%M"

#define NUM_SEGMENTS 14
#define ASCII_OFFSET 32
#define SUPPORTED_CHARS 96 // Number of characters in our font data (from ASCII 32 to 127)
#define CAMERA_DISTANCE 25.0f
#define TARGET_FPS 30 // Desired frames per second for the animation
#define SCREEN_PADDING_FACTOR 0.85f // Use 85% of the smaller screen dimension for auto-zoom


// --- Globals for Signal Handling ---
volatile int running = 1;
volatile int terminal_resized = 1; // Start at 1 to trigger initial setup

void handle_sigint(int sig) {
    (void)sig; // Unused parameter
    running = 0;
}

void handle_sigwinch(int sig) {
    (void)sig; // Unused parameter
    terminal_resized = 1;
}


// --- Platform-Specific Terminal Size Detection ---
#ifdef _WIN32
void get_terminal_size(int* width, int* height) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}
#else
#include <sys/ioctl.h>
void get_terminal_size(int* width, int* height) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    *width = w.ws_col;
    *height = w.ws_row;
}
#endif


// --- Data Structures ---

/**
 * @brief Holds all necessary state for rendering a single frame.
 * This includes screen buffers, dimensions, pre-calculated animation values,
 * and configuration for geometry, projection, and lighting. Bundling this
 * state prevents passing a dozen arguments to every rendering function.
 */
typedef struct {
    // Buffers and screen dimensions
    float* zbuffer;
    char*  bbuffer;
    int    sw, sh;

    // Pre-calculated animation state for the current frame
    float cosA, sinA, cosB, sinB;

    // Configuration for geometry and projection
    float zoom;
    float tilt_factor;

    // Configuration for lighting and appearance
    float light_x, light_y, contrast;
    const char* palette;
    size_t palette_len;
} RenderContext;

/**
 * @brief Defines a single segment's position and orientation.
 * Pre-calculating the rotation sine and cosine saves computation in the render loop.
 */
typedef struct {
    float pos_x, pos_y;
    float rot_z_rad;
    float cos_ra, sin_ra; // Pre-calculated cos and sin of rot_z_rad
} SegmentDef;


// --- Core Rendering Functions ---

/**
 * @brief Projects a 3D point onto the 2D screen buffer.
 * Handles Z-buffering, lighting, and character selection from the palette.
 * @param x, y, z The coordinates of the point in character-local space.
 * @param nx, ny, nz The components of the surface normal vector for lighting.
 * @param ctx The RenderContext containing all state for the current frame.
 */
void project_and_draw(float x, float y, float z, float nx, float ny, float nz,
                      const RenderContext* ctx) {
    // Apply shear transformation for an italic/tilted effect
    x += y * ctx->tilt_factor;

    // Rotate around Y axis (B - yaw)
    float rot_x = x * ctx->cosB - z * ctx->sinB;
    float rot_z = x * ctx->sinB + z * ctx->cosB;

    // Rotate around X axis (A - pitch) and translate forward
    float final_y = y * ctx->cosA - rot_z * ctx->sinA;
    float final_z = y * ctx->sinA + rot_z * ctx->cosA + CAMERA_DISTANCE;

    // Don't render points behind the camera
    if (final_z <= 0) return;

    // Perspective projection
    float ooz = 1.0f / final_z; // one over z
    // Stretch horizontally to compensate for non-square terminal characters
    int xp = (int)(ctx->sw / 2.0f + (ctx->zoom * 2.0f) * rot_x * ooz);
    int yp = (int)(ctx->sh / 2.0f - ctx->zoom * final_y * ooz);

    // Bounds and Z-buffer check
    int buffer_idx = xp + ctx->sw * yp;
    if (xp < 0 || xp >= ctx->sw || yp < 0 || yp >= ctx->sh || ooz <= ctx->zbuffer[buffer_idx]) {
        return;
    }

    // Rotate the normal vector to match the world orientation for lighting calculation
    float n_rot_x = nx * ctx->cosB - nz * ctx->sinB;
    float n_rot_z = nx * ctx->sinB + nz * ctx->cosB;
    float n_final_y = ny * ctx->cosA - n_rot_z * ctx->sinA;

    // Simple dot product for luminance
    float L = n_final_y * ctx->light_y + n_rot_x * ctx->light_x;

    // Update buffers
    ctx->zbuffer[buffer_idx] = ooz;
    int palette_idx = (int)(L * ctx->contrast);
    palette_idx = palette_idx < 0 ? 0 : (palette_idx >= ctx->palette_len ? ctx->palette_len - 1 : palette_idx); // Clamp
    ctx->bbuffer[buffer_idx] = ctx->palette[palette_idx];
}


// --- Geometry Drawing ---

/**
 * @brief Helper to rotate a point/normal from segment-local space to character space.
 * This reduces code duplication within draw_pointy_segment.
 * @param px, py, pz Point coordinates relative to the segment's center.
 * @param nx, ny Normal vector components (2D, as Z is 0 in local space).
 * @param def The segment's definition (position and pre-calculated rotation).
 * @param char_center_x The X-offset of the character this segment belongs to.
 * @param ctx The RenderContext for the current frame.
 */
static void draw_rotated_point(
    float px, float py, float pz,      // Point coords relative to segment center
    float nx, float ny,                // Normal vector (2D, as Z is always 0 in local space)
    const SegmentDef* def, float char_center_x, // Segment and character definitions
    const RenderContext* ctx
) {
    // Rotate segment points and normals into character-local orientation
    float rpx = px * def->cos_ra - py * def->sin_ra;
    float rpy = px * def->sin_ra + py * def->cos_ra;
    float rnx = nx * def->cos_ra - ny * def->sin_ra;
    float rny = nx * def->sin_ra + ny * def->cos_ra;

    // Translate to final position and project
    project_and_draw(rpx + def->pos_x + char_center_x, rpy + def->pos_y, pz,
                     rnx, rny, 0, ctx);
}

/**
 * @brief Draws a single 3D segment with flat faces and pointy ends.
 * This function iterates over the surface of the segment, calling the projection
 * function for each point.
 */
void draw_pointy_segment(float length, float seg_w, float seg_t, float point_len,
                         const SegmentDef* def, float char_center_x, float density,
                         const RenderContext* ctx)
{
    // Draw the two main flat faces of the segment
    for (float i = -length / 2.0f; i < length / 2.0f; i += density) {
        for (float j = -seg_t / 2.0f; j < seg_t / 2.0f; j += density) {
            // Top face (normal points up in local Y)
            draw_rotated_point(i, seg_w / 2.0f, j, 0, 1, def, char_center_x, ctx);
            // Bottom face (normal points down in local Y)
            draw_rotated_point(i, -seg_w / 2.0f, j, 0, -1, def, char_center_x, ctx);
        }
    }

    // Draw the four triangular faces of the pointy ends
    const float half_w = seg_w / 2.0f;
    float nl = sqrtf(half_w * half_w + point_len * point_len); // Normal vector length
    if (nl < 1e-5) return; // Avoid division by zero
    float cnx = half_w / nl;  // Normal X component for the slope
    float cny = point_len / nl; // Normal Y component for the slope

    for (float u = 0; u < point_len; u += density) {
        for (float pz = -seg_t / 2.0f; pz < seg_t / 2.0f; pz += density) {
            float yp = half_w * (1.0f - u / point_len); // Y-position on the triangle slope
            float p1 = length / 2.0f + u;
            float p2 = -length / 2.0f - u;

            // End 1, Top Face
            draw_rotated_point(p1, yp, pz, cnx, cny, def, char_center_x, ctx);
            // End 1, Bottom Face
            draw_rotated_point(p1, -yp, pz, cnx, -cny, def, char_center_x, ctx);
            // End 2, Top Face
            draw_rotated_point(p2, yp, pz, -cnx, cny, def, char_center_x, ctx);
            // End 2, Bottom Face
            draw_rotated_point(p2, -yp, pz, -cnx, -cny, def, char_center_x, ctx);
        }
    }
}


// --- Font Data & Usage ---

// Segments are bit-packed: 0=A, 1=B, 2=C, 3=D, 4=E, 5=F, 6=G1, 7=G2, 8=H, 9=I, 10=J, 11=K, 12=L, 13=M
const uint16_t FourteenSegmentASCII[SUPPORTED_CHARS] = {
    0b00000000000000, 0b10000000000110, 0b00001000000010, 0b01001011001110, 0b01001011101101, 0b11111111100100, 0b10001101011001, 0b00001000000000,
    0b10010000000000, 0b00100100000000, 0b11111111000000, 0b01001011000000, 0b00100000000000, 0b00000011000000, 0b10000000000000, 0b00110000000000,
    0b00110000111111, 0b00010000000110, 0b00000011011011, 0b00000010001111, 0b00000011100110, 0b10000001101001, 0b00000011111101, 0b00000000000111,
    0b00000011111111, 0b00000011101111, 0b01001000000000, 0b00101000000000, 0b10010001000000, 0b00000011001000, 0b00100110000000, 0b11000010000011,
    0b00001010111011, 0b00000011110111, 0b01001010001111, 0b00000000111001, 0b01001000001111, 0b00000001111001, 0b00000001110001, 0b00000010111101,
    0b00000011110110, 0b01001000001001, 0b00000000011110, 0b10010001110000, 0b00000000111000, 0b00010100110110, 0b10000100110110, 0b00000000111111,
    0b00000011110011, 0b10000000111111, 0b10000011110011, 0b00000011101101, 0b01001000000001, 0b00000000111110, 0b00110000110000, 0b10100000110110,
    0b10110100000000, 0b00000011101110, 0b00110000001001, 0b00000000111001, 0b10000100000000, 0b00000000001111, 0b10100000000000, 0b00000000001000,
    0b00000100000000, 0b01000001011000, 0b10000001111000, 0b00000011011000, 0b00100010001110, 0b00100001011000, 0b01010011000000, 0b00010010001110,
    0b01000001110000, 0b01000000000000, 0b00101000010000, 0b11011000000000, 0b00000000110000, 0b01000011010100, 0b01000001010000, 0b00000011011100,
    0b00000101110000, 0b00010010000110, 0b00000001010000, 0b10000010001000, 0b00000001111000, 0b00000000011100, 0b00100000010000, 0b10100000010100,
    0b10110100000000, 0b00001010001110, 0b00100001001000, 0b00100101001001, 0b01001000000000, 0b10010010001001, 0b00110011000000, 0b00000000000000
};

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [options] [TEXT TO DISPLAY...]\n", prog_name);
    fprintf(stderr, "If no text is provided, the current date and time are displayed by default.\n\n");
    fprintf(stderr, "Animation & Geometry:\n");
    fprintf(stderr, " -a <val>   A-axis (pitch) speed. Default: %.2f\n", DEFAULT_SPEED_A);
    fprintf(stderr, " -b <val>   B-axis (yaw) speed. Default: %.2f\n", DEFAULT_SPEED_B);
    fprintf(stderr, " -s <val>   Set both speeds (a=val, b=val/2).\n");
    fprintf(stderr, " -w <val>   Character width. Default: %.1f\n", DEFAULT_WIDTH);
    fprintf(stderr, " -h <val>   Character height. Default: %.1f\n", DEFAULT_HEIGHT);
    fprintf(stderr, " -S <val>   Character spacing multiplier. Default: %.2f\n", DEFAULT_SPACING_FACTOR);
    fprintf(stderr, " -t <val>   Italic/tilt factor. Default: %.1f\n", DEFAULT_TILT);
    fprintf(stderr, " -z <val>   Manual zoom, overrides auto-sizing.\n");
    fprintf(stderr, "\nRendering & Appearance:\n");
    fprintf(stderr, " -W <val>   Segment width (fatness). Default: %.1f\n", DEFAULT_SEG_WIDTH);
    fprintf(stderr, " -T <val>   Segment thickness (depth). Default: %.1f\n", DEFAULT_SEG_THICK);
    fprintf(stderr, " -p <val>   Pointy end length. Default: %.2f\n", DEFAULT_POINT_LEN);
    fprintf(stderr, " -d <val>   Drawing density (step rate). Smaller is denser. Default: %.1f\n", DEFAULT_DENSITY);
    fprintf(stderr, " -L <x,y>   Light vector (no spaces). Default: %.1f,%.1f\n", DEFAULT_LIGHT_X, DEFAULT_LIGHT_Y);
    fprintf(stderr, " -c <val>   Shading contrast. Default: %.1f\n", DEFAULT_CONTRAST);
    fprintf(stderr, " -P <str>   Shading character palette. Default: \"%s\"\n", DEFAULT_PALETTE);
    fprintf(stderr, " -f <fmt>   Set the date/time format (strftime). Default: \"%s\"\n", DEFAULT_TIME_FORMAT);
    fprintf(stderr, "            Examples: \"%%Y-%%m-%%d\" (date), \"%%I:%%M %%p\" (12h), \"%%Y-%%m-%%d %%H:%%M\" (both)\n");
    fprintf(stderr, "\n -?         Display this help message.\n");
}


// --- Main Program Logic ---

int main(int argc, char* argv[]) {
    // --- Configuration Variables ---
    float speedA = DEFAULT_SPEED_A, speedB = DEFAULT_SPEED_B, W = DEFAULT_WIDTH, H = DEFAULT_HEIGHT, tilt = DEFAULT_TILT;
    float spacing_factor = DEFAULT_SPACING_FACTOR;
    float seg_w = DEFAULT_SEG_WIDTH, seg_t = DEFAULT_SEG_THICK, point_len = DEFAULT_POINT_LEN;
    float light_x = DEFAULT_LIGHT_X, light_y = DEFAULT_LIGHT_Y, contrast = DEFAULT_CONTRAST, density = DEFAULT_DENSITY;
    const char* palette = DEFAULT_PALETTE;
    const char* time_date_format = DEFAULT_TIME_FORMAT;
    float manual_zoom = -1.0f;

    // --- Argument Parsing ---
    int opt;
    while ((opt = getopt(argc, argv, "s:a:b:w:h:z:t:?W:T:p:L:P:c:d:S:f:")) != -1) {
        switch (opt) {
            case 's': speedA = atof(optarg); speedB = atof(optarg) / 2.0f; break;
            case 'a': speedA = atof(optarg); break;
            case 'b': speedB = atof(optarg); break;
            case 'w': W = atof(optarg); break;
            case 'h': H = atof(optarg); break;
            case 'z': manual_zoom = atof(optarg); break;
            case 't': tilt = atof(optarg); break;
            case 'W': seg_w = atof(optarg); break;
            case 'T': seg_t = atof(optarg); break;
            case 'p': point_len = atof(optarg); break;
            case 'P': palette = optarg; break;
            case 'c': contrast = atof(optarg); break;
            case 'd': density = atof(optarg); if(density <= 0) { fprintf(stderr, "Density must be > 0\n"); return 1; } break;
            case 'L': if (sscanf(optarg, "%f,%f", &light_x, &light_y) != 2) { fprintf(stderr, "Invalid light vector. Use x,y\n"); return 1; } break;
            case 'S': spacing_factor = atof(optarg); break;
            case 'f': time_date_format = optarg; break;
            case '?': default: print_usage(argv[0]); return (opt == '?') ? 0 : 1;
        }
    }

    // --- Text Handling ---
    // By default, show the current date/time. If user provides arguments, show that text instead.
    int show_time_date = (argc <= optind);
    const char* text_to_display;
    char time_buffer[64]; // Buffer for date/time string, large enough for custom formats
    char* combined_args = NULL;

    if (show_time_date) {
        text_to_display = time_buffer;
    } else {
        size_t total_len = 0;
        for (int i = optind; i < argc; i++) total_len += strlen(argv[i]) + 1;
        if (!(combined_args = malloc(total_len))) { fprintf(stderr, "Memory allocation failed\n"); return 1; }
        char* current_pos = combined_args;
        for (int i = optind; i < argc; i++) {
            strcpy(current_pos, argv[i]);
            current_pos += strlen(argv[i]);
            if (i < argc - 1) *current_pos++ = ' ';
        }
        *current_pos = '\0';
        text_to_display = combined_args;
    }
    const size_t palette_len = strlen(palette);

    // --- Pre-calculate Program-Level Geometry (do this once!) ---
    const float quarter_w = W / 4.0f;
    const float quarter_h = H / 4.0f;
    const float diag_angle_rad = atan2f(quarter_h, quarter_w);

    SegmentDef seg_defs_init[NUM_SEGMENTS] = {
        {0, H/2, 0}, {W/2, H/4, 90}, {W/2, -H/4, 90}, {0, -H/2, 0}, {-W/2, -H/4, 90}, {-W/2, H/4, 90},
        {-quarter_w, 0, 0}, {quarter_w, 0, 0}, {-quarter_w, quarter_h, -diag_angle_rad*180.0f/M_PI}, {0, quarter_h, 90}, {quarter_w, quarter_h, diag_angle_rad*180.0f/M_PI},
        {-quarter_w, -quarter_h, diag_angle_rad*180.0f/M_PI}, {0, -quarter_h, 90}, {quarter_w, -quarter_h, -diag_angle_rad*180.0f/M_PI}
    };
    SegmentDef seg_defs[NUM_SEGMENTS];
    for(int i = 0; i < NUM_SEGMENTS; ++i) {
        seg_defs[i] = seg_defs_init[i];
        seg_defs[i].rot_z_rad = seg_defs[i].rot_z_rad * M_PI / 180.0f; // Convert degrees to radians
        seg_defs[i].cos_ra = cosf(seg_defs[i].rot_z_rad);
        seg_defs[i].sin_ra = sinf(seg_defs[i].rot_z_rad);
    }
    const float horiz_len = W / 2.0f - seg_w / 2.0f, vert_outer_len = H / 2.0f - seg_w;
    const float vert_inner_len = quarter_h - seg_w / 2.0f, diag_len = sqrtf(quarter_w * quarter_w + quarter_h * quarter_h) - seg_w;
    const float segment_lengths[NUM_SEGMENTS] = {
        horiz_len, vert_outer_len, vert_outer_len, horiz_len, vert_outer_len, vert_outer_len,
        horiz_len, horiz_len, diag_len, vert_inner_len, diag_len, diag_len, vert_inner_len, diag_len
    };
    const float char_spacing = W * spacing_factor;

    // --- Setup Rendering Buffers & State ---
    int sw = 0, sh = 0;
    float zoom = 1.0f;
    float* zbuffer = NULL;
    char* bbuffer = NULL;
    float A = 0, B = 0;

    // Setup for Frame Rate Control
#ifdef _WIN32
    LARGE_INTEGER freq, frame_start;
    QueryPerformanceFrequency(&freq);
    const double target_frame_ms = 1000.0 / TARGET_FPS;
#else
    const long target_frame_ns = 1000000000L / TARGET_FPS;
    struct timespec frame_start;
#endif

    // Setup for Graceful Exit
    signal(SIGINT, handle_sigint);
    #ifndef _WIN32
    signal(SIGWINCH, handle_sigwinch);
    #endif
    printf("\x1b[?25l\x1b[2J"); // Hide cursor and clear screen

    // --- MAIN RENDER LOOP ---
    while (running) {
        // --- Per-frame text and geometry setup ---
        int text_len;
        if (show_time_date) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(time_buffer, sizeof(time_buffer), time_date_format, tm_info);
            text_len = strlen(time_buffer);
        } else {
            text_len = strlen(text_to_display);
        }
        // These must be recalculated each frame in time mode as text length can change
        const float start_x = -(text_len - 1) * char_spacing / 2.0f;
        const float total_text_3d_width = (text_len > 1) ? (text_len - 1) * char_spacing + W : W;

        // Get frame start time for FPS limiting
#ifdef _WIN32
        QueryPerformanceCounter(&frame_start);
#else
        clock_gettime(CLOCK_MONOTONIC, &frame_start);
#endif
        // Handle Terminal Resizing
        if (terminal_resized) {
            get_terminal_size(&sw, &sh);
            sh -= 1; // Avoid scrolling on some terminals

            size_t buffer_size = sw * sh;
            float* new_zbuffer = realloc(zbuffer, buffer_size * sizeof(float));
            char*  new_bbuffer = realloc(bbuffer, buffer_size * sizeof(char));

            if (!new_zbuffer || !new_bbuffer) {
                fprintf(stderr, "Buffer reallocation failed. Exiting.\n");
                free(new_zbuffer); free(new_bbuffer);
                free(zbuffer); free(bbuffer);
                running = 0; continue;
            }
            zbuffer = new_zbuffer;
            bbuffer = new_bbuffer;

            if (manual_zoom <= 0) {
                // Auto-zoom calculation now uses the per-frame text width
                float zoom_h = (sh * SCREEN_PADDING_FACTOR) * CAMERA_DISTANCE / H;
                float zoom_w = (sw * SCREEN_PADDING_FACTOR) * CAMERA_DISTANCE / (total_text_3d_width * 2.0f);
                zoom = fminf(zoom_h, zoom_w) / 1;
            } else {
                zoom = manual_zoom;
            }
            printf("\x1b[2J");
            terminal_resized = 0;
        }

        // Create and populate the RenderContext for this frame
        RenderContext ctx = {
            .zbuffer = zbuffer, .bbuffer = bbuffer,
            .sw = sw, .sh = sh,
            .cosA = cosf(A), .sinA = sinf(A),
            .cosB = cosf(B), .sinB = sinf(B),
            .zoom = zoom, .tilt_factor = tilt,
            .light_x = light_x, .light_y = light_y,
            .contrast = contrast,
            .palette = palette, .palette_len = palette_len
        };

        // Clear buffers for the new frame
        memset(bbuffer, ' ', sw * sh);
        memset(zbuffer, 0, sw * sh * sizeof(float));

        // Iterate through each character in the input string
        for (int char_idx = 0; char_idx < text_len; char_idx++) {
            char c = text_to_display[char_idx];
            if (c < ASCII_OFFSET || c >= ASCII_OFFSET + SUPPORTED_CHARS) c = ' ';
            uint16_t seg_data = FourteenSegmentASCII[c - ASCII_OFFSET];
            float char_center_x = start_x + char_idx * char_spacing;

            // Iterate through the 14 possible segments for the character
            for (int i = 0; i < NUM_SEGMENTS; i++) {
                if ((seg_data >> i) & 1) { // Check if this segment should be drawn
                    draw_pointy_segment(segment_lengths[i], seg_w, seg_t, point_len, &seg_defs[i], char_center_x, density, &ctx);
                }
            }
        }

        // Print the buffer to the screen
        printf("\x1b[H");
        for (int y = 0; y < sh; y++) {
            fwrite(bbuffer + y * sw, 1, sw, stdout);
            putchar('\n');
        }
        fflush(stdout);

        // Update animation angles for the next frame
        A += speedA;
        B += speedB;

        // Calculate elapsed time and sleep for the remainder to cap FPS
#ifdef _WIN32
        LARGE_INTEGER frame_end;
        QueryPerformanceCounter(&frame_end);
        double elapsed_ms = (frame_end.QuadPart - frame_start.QuadPart) * 1000.0 / freq.QuadPart;
        if (elapsed_ms < target_frame_ms) {
            Sleep((DWORD)(target_frame_ms - elapsed_ms));
        }
#else
        struct timespec frame_end;
        clock_gettime(CLOCK_MONOTONIC, &frame_end);
        long elapsed_ns = (frame_end.tv_sec - frame_start.tv_sec) * 1000000000L + (frame_end.tv_nsec - frame_start.tv_nsec);

        if (elapsed_ns < target_frame_ns) {
            struct timespec sleep_time;
            long remainder_ns = target_frame_ns - elapsed_ns;
            sleep_time.tv_sec = remainder_ns / 1000000000L;
            sleep_time.tv_nsec = remainder_ns % 1000000000L;
            nanosleep(&sleep_time, NULL);
        }
#endif
    }

    // --- Cleanup ---
    printf("\x1b[?25h\n"); // Show cursor again and move to a new line
    free(zbuffer);
    free(bbuffer);
    if (combined_args) free(combined_args);

    return 0;
}
