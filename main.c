#include <stdio.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <raylib.h>

typedef uint8_t u8;
typedef uint16_t u16;

// display res
#define RESX 64
#define RESY 32

#define WIN_WIDTH (RESX * 15)
#define WIN_HEIGHT (RESY * 15)

#define TIMER_TICK_TIME (1.0 / 60.0)
#define BEEP_VOLUME 0.2f
#define CLOCK_SPEED 700 // Hz

#define MEM_SIZE 4096
#define PROGRAM_ADDR 0x200

#define STACK_CAP 32

#define ERROR(fmt, ...) ({ fprintf(stderr, "ERROR: "fmt"\n", ##__VA_ARGS__); exit(1); })
#define LOG(fmt, ...) printf("LOG: "fmt"\n", ##__VA_ARGS__)

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

    double last_delay_tick;
    double last_sound_tick;
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
#define COLOR_ON RAYWHITE
#define COLOR_OFF BLACK
static bool screen[RESX*RESY] = {0};
static bool screen_should_refresh = true;

void stack_push(Stack *s, u16 data)
{
    assert(s->idx < STACK_CAP);
    s->buffer[s->idx++] = data;
}

u16 stack_pop(Stack *s)
{
    assert(s->idx - 1 >= 0);
    // need to decrement before reading value on top
    u16 data = *(s->buffer + --s->idx);
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

// simulates hex numpad with left side of keyboard
bool is_key_pressed(u8 key)
{
    switch (key) {
        case 0x1: return IsKeyDown(KEY_ONE);
        case 0x2: return IsKeyDown(KEY_TWO);
        case 0x3: return IsKeyDown(KEY_THREE);
        case 0xC: return IsKeyDown(KEY_FOUR);
        case 0x4: return IsKeyDown(KEY_Q);
        case 0x5: return IsKeyDown(KEY_W);
        case 0x6: return IsKeyDown(KEY_E);
        case 0xD: return IsKeyDown(KEY_R);
        case 0x7: return IsKeyDown(KEY_A);
        case 0x8: return IsKeyDown(KEY_S);
        case 0x9: return IsKeyDown(KEY_D);
        case 0xE: return IsKeyDown(KEY_F);
        case 0xA: return IsKeyDown(KEY_Z);
        case 0x0: return IsKeyDown(KEY_X);
        case 0xB: return IsKeyDown(KEY_C);
        case 0xF: return IsKeyDown(KEY_V);
        default: return false;
    }
}

// get poll for *released* key
int get_key()
{
    if (IsKeyReleased(KEY_ONE)) return 0x1;
    if (IsKeyReleased(KEY_TWO)) return 0x2;
    if (IsKeyReleased(KEY_THREE)) return 0x3;
    if (IsKeyReleased(KEY_FOUR)) return 0xC;
    if (IsKeyReleased(KEY_Q)) return 0x4;
    if (IsKeyReleased(KEY_W)) return 0x5;
    if (IsKeyReleased(KEY_E)) return 0x6;
    if (IsKeyReleased(KEY_R)) return 0xD;
    if (IsKeyReleased(KEY_A)) return 0x7;
    if (IsKeyReleased(KEY_S)) return 0x8;
    if (IsKeyReleased(KEY_D)) return 0x9;
    if (IsKeyReleased(KEY_F)) return 0xE;
    if (IsKeyReleased(KEY_Z)) return 0xA;
    if (IsKeyReleased(KEY_X)) return 0x0;
    if (IsKeyReleased(KEY_C)) return 0xB;
    if (IsKeyReleased(KEY_V)) return 0xF;

    return -1;
}

// each bit represents a pixel on/off
// sprites width is 1 byte
void draw_sprite(Cpu *cpu, u8 x, u8 y, u16 addr, u8 nbytes)
{

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
            } else if (instruction == 0x00EE) {
                u16 addr = stack_pop(&cpu->stack);
                cpu->pc = addr;
            }
        }
        break;

        // Jump
        case 0x1: {
            u16 addr = instruction & 0xFFF;
            cpu->pc = addr;
        }
        break;

        // execute subroutine
        case 0x2: {
            u16 addr = instruction & 0xFFF;
            stack_push(&cpu->stack, cpu->pc);
            cpu->pc = addr;
        }
        break;

        // skip next instruction if X == N
        case 0x3: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 value = instruction & 0xFF;
            if (cpu->registers[idx] == value) {
                cpu->pc += 2;
            }
        }
        break;

        // skip next instruction if X != N
        case 0x4: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 value = instruction & 0xFF;
            if (cpu->registers[idx] != value) {
                cpu->pc += 2;
            }
        }
        break;

        // skip next instruction if X == Y
        case 0x5: {
            u8 x_idx = (instruction >> 8) & 0xF;
            u8 y_idx = (instruction >> 4) & 0xF;
            if (cpu->registers[x_idx] == cpu->registers[y_idx]) {
                cpu->pc += 2;
            }
        }
        break;

        // Load register with value
        case 0x6: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 value = instruction & 0xFF;
            cpu->registers[idx] = value;
        }
        break;

        // Add to register
        case 0x7: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 value = instruction & 0xFF;
            cpu->registers[idx] += value;
        }
        break;

        //---Register manipulation---
        case 0x8: {
            u8 x_idx = (instruction >> 8) & 0xF;
            u8 y_idx = (instruction >> 4) & 0xF;
            u8 sub_op = instruction & 0xF;
            u8 *reg = cpu->registers;
            switch (sub_op) {
                // set
                case 0x0:
                    reg[x_idx] = reg[y_idx];
                break;
                // or
                case 0x1:
                    reg[x_idx] |= reg[y_idx];
                    reg[0xF] = 0;
                break;
                // and
                case 0x2:
                    reg[x_idx] &= reg[y_idx];
                    reg[0xF] = 0;
                break;
                // xor
                case 0x3:
                    reg[x_idx] ^= reg[y_idx];
                    reg[0xF] = 0;
                break;
                // add
                case 0x4: {
                    u8 x_val = reg[x_idx];
                    reg[x_idx] += reg[y_idx];
                    if (x_val > reg[x_idx]) {
                        reg[0xF] = 1;
                    } else {
                        reg[0xF] = 0;
                    }
                }
                break;
                // sub
                case 0x5: {
                    u8 x_val = reg[x_idx];
                    reg[x_idx] -= reg[y_idx];
                    if (x_val < reg[x_idx]) {
                        reg[0xF] = 1;
                    } else {
                        reg[0xF] = 0;
                    }
                }
                break;
                // shr
                case 0x6: {
                    // lsb in flag
                    reg[0xF] = reg[y_idx] & 0x1;
                    reg[x_idx] = reg[y_idx] >> 1;
                }
                break;
                // sub from y
                case 0x7: {
                    u8 y_val = reg[y_idx];
                    reg[x_idx] = reg[y_idx] - reg[x_idx];
                    if (y_val < reg[y_idx]) {
                        reg[0xF] = 1;
                    } else {
                        reg[0xF] = 0;
                    }
                }
                break;
                case 0xE: {
                    // msb in flag
                    reg[0xF] = reg[y_idx] >> 7;
                    reg[x_idx] = reg[y_idx] << 1;
                }
                break;

                default: ERROR("invalid instruction %x", instruction);
            }

        }
        break;

        // skip next instruction if X != Y
        case 0x9: {
            u8 x_idx = (instruction >> 8) & 0xF;
            u8 y_idx = (instruction >> 4) & 0xF;
            if (cpu->registers[x_idx] != cpu->registers[y_idx]) {
                cpu->pc += 2;
            }
        }
        break;

        // Store address in index
        case 0xA: {
            u16 addr = instruction & 0xFFF;
            cpu->index = addr;
        }
        break;

        // jump to address + V0
        case 0xB : {
            u16 addr = instruction & 0xFFF;
            cpu->pc = addr + cpu->registers[0];
        }
        break;

        // set register to random number & NN
        case 0xC: {
            u8 idx = (instruction >> 8) & 0xF;
            u16 val = instruction & 0xFF;
            cpu->registers[idx] = rand() & val;
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

        // skip next instruction if key in register is pressed or not pressed
        case 0xE: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 subop = instruction & 0xFF;
            u8 key = cpu->registers[idx];
            if (subop == 0x9E) {
                if (is_key_pressed(key)) {
                    cpu->pc += 2;
                }
            } else if (subop == 0xA1) {
                if (!is_key_pressed(key)) {
                    cpu->pc += 2;
                }
            } else {
                ERROR("invalid instruction %x", instruction);
            }
        }
        break;

        case 0xF: {
            u8 idx = (instruction >> 8) & 0xF;
            u8 subop = instruction & 0xFF;
            switch (subop) {

                // store delay timer in register
                case 0x07: 
                    cpu->registers[idx] = cpu->delay_timer;
                break;

                // get key (waits until there is input)
                case 0x0A: {
                    int key = get_key();
                    if (key < 0) {
                        // this blocks on this op until there is key in queue
                        cpu->pc -= 2;
                    } else {
                        cpu->registers[idx] = (u8)key;
                    }
                }
                break;

                // set delay timer
                case 0x15:
                    cpu->delay_timer = cpu->registers[idx];
                    if (!cpu->delay_timer)
                        cpu->last_delay_tick = GetTime();
                break;
                // set sound timer
                case 0x18:
                    cpu->sound_timer = cpu->registers[idx];
                    if (!cpu->sound_timer)
                        cpu->last_sound_tick = GetTime();
                break;
                // add to index
                case 0x1E:
                    cpu->index += cpu->registers[idx];
                break;

                // set index register to addr of font sprite for char in register
                case 0x29: {
                    u16 addr = FONT_ADDR + cpu->registers[idx]*BYTES_PER_CHARACTER;
                    cpu->index = addr;
                }
                break;
                // store value in register as BCD at index addr
                case 0x33: {
                    u16 index = cpu->index;
                    u8 value = cpu->registers[idx];
                    cpu->memory[index + 2] = value % 10;
                    value /= 10;
                    cpu->memory[index + 1] = value % 10;
                    value /= 10;
                    cpu->memory[index] = value % 10;
                }
                break;
                // write register values to index addr
                case 0x55: {
                    for (int i = 0; i <= idx; i++)
                        cpu->memory[cpu->index + i] = cpu->registers[i];
                    cpu->index += idx + 1;
                }
                break;
                // read index addr values into registers
                case 0x65: {
                    for (int i = 0; i <= idx; i++)
                         cpu->registers[i] = cpu->memory[cpu->index + i];
                    cpu->index += idx + 1;
                }
                break;

                default: ERROR("invalid instruction %x", instruction);
            }
        }
        break;

        default: ERROR("invalid instruction %x", instruction);
    }
}

#define SAMPLE_RATE 44100
#define BEEP_DURATION 1
#define BEEP_FREQUENCY 240
#define SAMPLES (int)(SAMPLE_RATE * BEEP_DURATION)
Sound load_sound()
{
    short *data = malloc(SAMPLES * sizeof(short));
    for (int i = 0; i < SAMPLES; i++) {
        float amplitude = 20000.0f;
        float time = (float)i / SAMPLE_RATE;
        data[i] = (short)(amplitude * sinf(2.0f * PI * BEEP_FREQUENCY * time));
    }

    Wave wave = {
        .frameCount = SAMPLES,
        .sampleRate = SAMPLE_RATE,
        .sampleSize = 16,
        .channels = 1,
        .data = data,
    };
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);

    return sound;
}

int main(int argc, char *argv[])
{

    if (argc < 2) {
        printf("Usage: emu <rom.ch8>\n");
        return 1;
    }

    long clock_speed = CLOCK_SPEED;
    if (argc > 3) {
        // clock speed
        if (strcmp(argv[1], "-c") == 0) {
            long input = strtol(argv[2], NULL, 10);
            if (input > 0) {
                clock_speed = input;
            }
        }
    }

    char *filepath = argv[argc - 1];
    int program_len;
    u8 *program_data = LoadFileData(filepath, &program_len);
    if (program_data == NULL) {
        ERROR("Could not open file %s", filepath);
    }

    // init computer
    Cpu cpu = {0};
    load_program(&cpu, program_data, program_len);
    // load the font
    memcpy(&cpu.memory[FONT_ADDR], font_data, sizeof(font_data));
    // randomize seed
    srand(time(NULL));


    SetTraceLogLevel(LOG_WARNING);
    const char *game_name = GetFileNameWithoutExt(GetFileName(filepath));
    InitWindow(WIN_WIDTH, WIN_HEIGHT, game_name);

    InitAudioDevice();
    Sound sound = load_sound();
    SetSoundVolume(sound, BEEP_VOLUME);

    Color pixels[RESX*RESY];
    Texture render_target = LoadTextureFromImage((Image) {
            .data = pixels,
            .width = RESX,
            .height = RESY,
            .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        });

    // event loop
    double last_op = 0;
    while(!WindowShouldClose()) {

        if (GetTime() - last_op > (1.0 / clock_speed)) {
            last_op = GetTime();
            u16 instruction = read_instruction(&cpu);
            execute_instruction(&cpu, instruction);
        }

        // update timers
        double time = GetTime();
        if (cpu.delay_timer > 0) {
            if (time - cpu.last_delay_tick > TIMER_TICK_TIME) {
                cpu.delay_timer--;
                cpu.last_delay_tick = time;
            }
        }
        if (cpu.sound_timer > 1) {
            if (GetTime() - cpu.last_sound_tick > TIMER_TICK_TIME) {
                cpu.sound_timer--;
                cpu.last_sound_tick = time;
                if (!IsSoundPlaying(sound)) {
                    PlaySound(sound);
                }
            }
        } else if (IsSoundPlaying(sound)) {
            StopSound(sound);
        }

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

        // polls for input
        EndDrawing();
    }

    return 0;
}
