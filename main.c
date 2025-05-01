#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <raylib.h>

typedef uint8_t u8;
typedef uint16_t u16;

// display res
#define RESX 64
#define RESY 32

#define SPRITE_WIDTH 8

#define WIN_WIDTH (RESX * 10)
#define WIN_HEIGHT (RESY * 10)

#define MEM_SIZE 4096
#define PROGRAM_ADDR 0x200

#define STACK_CAP 32

typedef struct Stack {
    u16 buffer[STACK_CAP];
    int idx;
} Stack;

typedef struct Cpu {
    u8 memory[MEM_SIZE];
    u16 pc; // program counter points to current instruction

    Stack stack;
    u16 index;
    u8 registers[16];

    // when not zero these timers count down at 60Hz
    u8 delay_timer;
    u8 sound_timer;
} Cpu;

#define BYTES_PER_CHARACTER 5
#define FONT_ADDR 0x050
static u8 font_data[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// Screen buffer
#define COLOR_ON WHITE
#define COLOR_OFF BLACK
static bool screen[RESX*RESY] = {0};
static bool screen_should_refresh = true;

void stack_push(Stack *s, u16 data)
{
    assert(s->idx + 1 < STACK_CAP);
    s->buffer[s->idx++] = data;
}

u16 stack_pop(Stack *s)
{
    assert(s->idx - 1 >= 0);
    u16 data = s->buffer[s->idx--];
    return data;
}

void load_program(Cpu *cpu, u8 *program_data, int program_len)
{
    assert(program_len < MEM_SIZE - PROGRAM_ADDR);
    memcpy(&cpu->memory[PROGRAM_ADDR], program_data, program_len);
    cpu->pc = PROGRAM_ADDR;
}

u16 read_instruction(Cpu *cpu)
{
    // Think program is big endian?
    u16 hi = (u16)cpu->memory[cpu->pc++];
    u16 lo = (u16)cpu->memory[cpu->pc++];
    return hi << 8 | lo;
}

// each bit represents a pixel on/off
// sprites width is 1 byte
void draw_sprite(Cpu *cpu, u8 x, u8 y, u16 addr, u8 nbytes) {

    cpu->registers[0xF] = 0;
    for (int row = 0; row < nbytes; row++) {
        if (y + row >= RESY) continue;

        u8 byte = cpu->memory[addr + row];
        for (int col = 0; col < 8; col++) {
            if (x + col >= RESX) continue;
            u8 value = (byte >> (7 - col)) & 0x1;
            // value will be zero or one
            int idx = (y + row) * RESX + (x + col);

            // bitwise xor instead of setting value
            bool is_set = screen[idx];
            screen[idx] ^= value;

            // set flag
            if (is_set && !screen[idx]) {
                cpu->registers[0xF] = 1;
            }
        }
    }
}

void execute_instruction(Cpu *cpu, u16 instruction)
{
    u8 op = ((instruction >> 12) & 0xF);
    switch (op) {
        case 0x0: {
            // NOTE:: 0xNNN is not implemented

            // Clear Screen
            if (instruction == 0x00E0) {
                memset(screen, false, RESX*RESY);
                screen_should_refresh = true;
            // Return from subroutine
            } else if (0x00EE) {
                u16 addr = stack_pop(&cpu->stack);
                cpu->pc = addr;
            }
        }
        break;

        // Jump
        case 0x1: {
            u16 addr = instruction & 0x0FFF;
            cpu->pc = addr;
        }
        break;

        case 0x2: {

        }
        break;
        case 0x3: {

        }
        break;

        // Load register with value
        case 0x6: {
            u8 reg_idx = (instruction >> 8) & 0xF;
            u8 value = instruction & 0xFF;
            cpu->registers[reg_idx] = value;
        }
        break;

        // Store address in index
        case 0xA: {
            u16 addr = instruction & 0xFFF;
            cpu->index = addr;
        }
        break;

        // Draw
        case 0xD: {
            u8 x_idx = (instruction >> 8) & 0xF;
            u8 y_idx = (instruction >> 4) & 0xF;
            u8 nbytes = instruction & 0xF;
            u8 x = cpu->registers[x_idx] % RESX;
            u8 y = cpu->registers[y_idx] % RESY;
            draw_sprite(cpu, x, y, cpu->index, nbytes);
            screen_should_refresh = true;
        }
        break;
    }
}

void update_pixels(Color *pixels)
{
    for (int i = 0; i < RESX*RESY; i++) {
        if (screen[i]) {
            pixels[i] = COLOR_ON;
        } else {
            pixels[i] = COLOR_OFF;
        }
    }
}

int main(int argc, char *argv[])
{

    if (argc < 2) {
        printf("Usage: emu <rom.ch8>\n");
        return 1;
    }

    char *filepath = argv[1];
    int program_len;
    u8 *program_data = LoadFileData(filepath, &program_len);

    // init computer
    Cpu cpu = {0};
    load_program(&cpu, program_data, program_len);
    // load the font
    memcpy(&cpu.memory[FONT_ADDR], font_data, sizeof(font_data));

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_WIDTH, WIN_HEIGHT, "Chip-8");

    Color pixels[RESX*RESY];
    Texture render_target = LoadTextureFromImage((Image) {
            .data = pixels,
            .width = RESX,
            .height = RESY,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        });

    while(!WindowShouldClose()) {

        u16 instruction = read_instruction(&cpu);
        execute_instruction(&cpu, instruction);

        if (screen_should_refresh) {
            screen_should_refresh = false;
            update_pixels(pixels);
            UpdateTexture(render_target, pixels);
        }

        // Rendering
        BeginDrawing();

        Rectangle src = {0, 0, RESX, RESY};
        Rectangle dst = {0, 0, WIN_WIDTH, WIN_HEIGHT};
        DrawTexturePro(render_target, src, dst, (Vector2){0}, 0.0f, WHITE);

        EndDrawing();
    }

    return 0;
}
