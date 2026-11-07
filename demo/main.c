// Demo of the car synth library
// The demo has no car graphics. A small simulation of a car makes the input values for the synth.
// The keys are the same as in the game Holy Shift.

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#include "car_synth.h"
#include "raylib.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define STREAM_BUFFER_FRAMES 1024 // about 23 ms of sound

#define SCREECH_PATH "demo/assets/tire_screech.wav"
#define GEAR_PATH "demo/assets/gear_change.wav"

#define GEAR_NEUTRAL 0
#define GEAR_TOP 5
#define RPM_IDLE 900.0f
#define RPM_STALL 600.0f        // in gear, the engine stalls below this wheel speed
#define SHIFT_SECONDS 0.15f     // time with the clutch down after a gear change
#define START_SECONDS 1.2f      // time that the starter motor turns the engine
#define BRAKE_DECEL 400.0f      // deceleration of the brake, in speed units each second
#define HANDBRAKE_DECEL 250.0f  // deceleration of the handbrake
#define COAST_DECEL 40.0f       // deceleration of the engine brake in gear
#define DRAG 0.15f              // air drag, for each speed unit
#define STEER_SLIP 0.35f        // part of the speed that becomes slip with full steering
#define HANDBRAKE_SLIP 0.4f     // part of the speed that becomes slip with the handbrake

// Engine speed for each unit of car speed, for each gear
static const float RPM_PER_SPEED[GEAR_TOP + 1] = {0.0f, 60.0f, 38.0f, 27.0f, 20.0f, 15.0f};

// Acceleration at full throttle, for each gear
static const float ACCELERATION[GEAR_TOP + 1] = {0.0f, 260.0f, 180.0f, 130.0f, 95.0f, 70.0f};

// The state of the simulated car
typedef struct DemoCar
{
    int gear;
    float rpm;
    float throttle;
    float speed;
    float slip;
    float stall;         // from 0 to 1
    bool engine_off;
    float starting;      // seconds left of the starter motor
    float shift_left;    // seconds left of the clutch down time
    bool grass;
} DemoCar;

// The audio thread reads these values. The main thread writes them.
static CarSynth synth;
static float *screech_samples;
static float *gear_samples;
static _Atomic float shared_rpm;
static _Atomic float shared_throttle;
static _Atomic float shared_speed;
static _Atomic float shared_slip;
static atomic_bool shared_grass;
static _Atomic float shared_stall;
static atomic_bool shared_running;
static atomic_bool shared_cranking;
static atomic_int clunk_requests;

// Raylib calls this function when the audio stream needs more samples
static void RenderAudio(void *buffer, unsigned int frames)
{
    CarSynthInput input = {
        .rpm = atomic_load(&shared_rpm),
        .throttle = atomic_load(&shared_throttle),
        .speed = atomic_load(&shared_speed),
        .slip = atomic_load(&shared_slip),
        .grass = atomic_load(&shared_grass),
        .stall = atomic_load(&shared_stall),
        .running = atomic_load(&shared_running),
        .cranking = atomic_load(&shared_cranking),
    };
    if (atomic_exchange(&clunk_requests, 0) > 0)
    {
        CarSynthClunk(&synth);
    }
    CarSynthRender(&synth, &input, buffer, (int)frames);
}

// Return the samples of a sound file in the format of the synth
// Return NULL when the file cannot be loaded.
static float *LoadSamples(const char *path, int *count)
{
    *count = 0;
    Wave wave = LoadWave(path);
    if (!IsWaveValid(wave))
    {
        return NULL;
    }
    WaveFormat(&wave, CAR_SYNTH_SAMPLE_RATE, 32, 1);
    float *samples = LoadWaveSamples(wave);
    *count = (int)wave.frameCount;
    UnloadWave(wave);
    return samples;
}

// Copy the car values to the values that the audio thread reads
static void ShareCar(const DemoCar *car)
{
    atomic_store(&shared_rpm, car->rpm);
    atomic_store(&shared_throttle, car->throttle);
    atomic_store(&shared_speed, fabsf(car->speed));
    atomic_store(&shared_slip, fabsf(car->slip));
    atomic_store(&shared_grass, car->grass);
    atomic_store(&shared_stall, car->stall);
    atomic_store(&shared_running, !car->engine_off);
    atomic_store(&shared_cranking, car->starting > 0.0f);
}

// Change the gear by one step
// A change to a lower gear also starts a stopped engine.
static void Shift(DemoCar *car, int direction)
{
    int gear = car->gear + direction;
    if (gear < GEAR_NEUTRAL || gear > GEAR_TOP)
    {
        return;
    }
    car->gear = gear;
    car->shift_left = SHIFT_SECONDS;
    atomic_fetch_add(&clunk_requests, 1);
    if (direction < 0 && car->engine_off && car->starting <= 0.0f)
    {
        car->starting = START_SECONDS;
    }
}

// Move the car forward in time
static void UpdateCar(DemoCar *car, float dt)
{
    bool clutch = car->shift_left > 0.0f || car->gear == GEAR_NEUTRAL;
    bool engine_on = !car->engine_off;
    float steer = (float)((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)));
    bool brake = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    bool handbrake = IsKeyDown(KEY_SPACE);
    float throttle_target = (engine_on && (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))) ? 1.0f : 0.0f;

    car->shift_left = fmaxf(0.0f, car->shift_left - dt);
    car->throttle = throttle_target;

    // The starter motor turns the engine, then the engine runs
    if (car->starting > 0.0f)
    {
        car->starting -= dt;
        car->rpm += (250.0f - car->rpm) * fminf(1.0f, dt * 6.0f);
        if (car->starting <= 0.0f)
        {
            car->starting = 0.0f;
            car->engine_off = false;
            car->stall = 0.0f;
            car->rpm = RPM_IDLE;
        }
    }
    else if (car->engine_off)
    {
        car->rpm += (0.0f - car->rpm) * fminf(1.0f, dt * 3.0f);
    }
    else
    {
        // The engine speed follows the throttle (clutch down) or the wheels (clutch up)
        float wheel_rpm = car->speed * RPM_PER_SPEED[car->gear];
        float target;
        if (clutch)
        {
            target = RPM_IDLE + car->throttle * (CAR_SYNTH_RPM_LIMIT - RPM_IDLE);
        }
        else
        {
            target = fmaxf(wheel_rpm, RPM_IDLE);
            if (car->throttle > 0.0f)
            {
                target = fmaxf(target, 1000.0f);
            }
            target = fminf(target, CAR_SYNTH_RPM_LIMIT);
        }
        car->rpm += (target - car->rpm) * fminf(1.0f, dt * (clutch ? 6.0f : 15.0f));

        // The engine stalls when the wheels turn it too slowly and the throttle is closed
        if (!clutch && wheel_rpm < RPM_STALL && car->throttle == 0.0f)
        {
            car->stall = fminf(1.0f, car->stall + dt * 1.5f);
        }
        else
        {
            car->stall = fmaxf(0.0f, car->stall - dt * 3.0f);
        }
        if (car->stall >= 1.0f)
        {
            car->engine_off = true;
        }
    }

    // The speed changes with the throttle and the brakes
    float accel = 0.0f;
    if (!clutch && engine_on && car->rpm < CAR_SYNTH_RPM_LIMIT - 40.0f)
    {
        accel += car->throttle * ACCELERATION[car->gear];
    }
    if (!clutch && car->gear > GEAR_NEUTRAL)
    {
        accel -= COAST_DECEL * (car->throttle > 0.0f ? 0.0f : 1.0f);
    }
    accel -= DRAG * car->speed;
    if (brake)
    {
        accel -= BRAKE_DECEL;
    }
    if (handbrake)
    {
        accel -= HANDBRAKE_DECEL;
    }
    car->speed = fmaxf(0.0f, car->speed + accel * dt);

    // The slip follows the steering and the handbrake
    float slip_target = fabsf(steer) * car->speed * STEER_SLIP;
    if (handbrake)
    {
        slip_target += car->speed * HANDBRAKE_SLIP;
    }
    car->slip += (slip_target - car->slip) * fminf(1.0f, dt * 8.0f);
}

// Return a text bar that shows a value from 0 to 1
static const char *Bar(float value)
{
    static char text[33];
    int filled = (int)(fminf(fmaxf(value, 0.0f), 1.0f) * 30.0f + 0.5f);
    text[0] = '[';
    for (int i = 0; i < 30; i++)
    {
        text[i + 1] = i < filled ? '#' : '-';
    }
    text[31] = ']';
    text[32] = '\0';
    return text;
}

static void DrawScreen(const DemoCar *car, bool audio_ready)
{
    int x = 40;
    int y = 30;
    const int line = 26;
    const int size = 20;
    Color text = RAYWHITE;
    Color dim = GRAY;

    ClearBackground((Color){20, 22, 26, 255});
    DrawText("CAR SYNTH DEMO", x, y, 32, GOLD);
    y += 56;

    DrawText("W / Up       throttle", x, y, size, dim);
    y += line;
    DrawText("S / Down     brake", x, y, size, dim);
    y += line;
    DrawText("A D / Left Right   steer (makes the tyres slide)", x, y, size, dim);
    y += line;
    DrawText("Space        handbrake", x, y, size, dim);
    y += line;
    DrawText("E            shift up", x, y, size, dim);
    y += line;
    DrawText("Q            shift down (also starts a stopped engine)", x, y, size, dim);
    y += line;
    DrawText("G            change between road and grass", x, y, size, dim);
    y += line * 2;

    DrawText(TextFormat("Gear      %s", car->gear == GEAR_NEUTRAL ? "N" : TextFormat("%d", car->gear)), x, y, size, text);
    y += line;
    DrawText(TextFormat("RPM       %5.0f  %s", car->rpm, Bar(car->rpm / CAR_SYNTH_RPM_LIMIT)), x, y, size, text);
    y += line;
    DrawText(TextFormat("Throttle  %5.2f  %s", car->throttle, Bar(car->throttle)), x, y, size, text);
    y += line;
    DrawText(TextFormat("Speed     %5.0f  %s", car->speed, Bar(car->speed / 400.0f)), x, y, size, text);
    y += line;
    DrawText(TextFormat("Slip      %5.0f  %s", car->slip, Bar(car->slip / 90.0f)), x, y, size, text);
    y += line;
    DrawText(TextFormat("Surface   %s", car->grass ? "grass" : "road"), x, y, size, text);
    y += line;

    const char *engine = "running";
    Color engine_color = GREEN;
    if (car->starting > 0.0f)
    {
        engine = "starting";
        engine_color = YELLOW;
    }
    else if (car->engine_off)
    {
        engine = "stopped (press Q to start)";
        engine_color = RED;
    }
    DrawText(TextFormat("Engine    %s", engine), x, y, size, engine_color);
    y += line * 2;

    if (!audio_ready)
    {
        DrawText("The audio stream cannot start. There is no sound.", x, y, size, RED);
    }
    DrawText("Part of Holy Shift, a game for Game Jam 2026", x, SCREEN_HEIGHT - 40, 16, dim);
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Car synth demo");
    SetTargetFPS(60);
    InitAudioDevice();

    // Set up the synth and the audio stream
    CarSynthInit(&synth);
    int count = 0;
    screech_samples = LoadSamples(SCREECH_PATH, &count);
    CarSynthSetScreech(&synth, screech_samples, count);
    gear_samples = LoadSamples(GEAR_PATH, &count);
    CarSynthSetGear(&synth, gear_samples, count);

    DemoCar car = {.rpm = RPM_IDLE};
    ShareCar(&car);

    AudioStream stream = {0};
    bool audio_ready = false;
    if (IsAudioDeviceReady())
    {
        SetAudioStreamBufferSizeDefault(STREAM_BUFFER_FRAMES);
        stream = LoadAudioStream(CAR_SYNTH_SAMPLE_RATE, 32, 1);
        if (IsAudioStreamValid(stream))
        {
            SetAudioStreamCallback(stream, RenderAudio);
            PlayAudioStream(stream);
            audio_ready = true;
        }
    }

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_E))
        {
            Shift(&car, 1);
        }
        if (IsKeyPressed(KEY_Q))
        {
            Shift(&car, -1);
        }
        if (IsKeyPressed(KEY_G))
        {
            car.grass = !car.grass;
        }
        UpdateCar(&car, GetFrameTime());
        ShareCar(&car);

        BeginDrawing();
        DrawScreen(&car, audio_ready);
        EndDrawing();
    }

    // Stop the audio stream before the program frees the samples
    if (audio_ready)
    {
        StopAudioStream(stream);
        UnloadAudioStream(stream);
    }
    if (screech_samples != NULL)
    {
        UnloadWaveSamples(screech_samples);
    }
    if (gear_samples != NULL)
    {
        UnloadWaveSamples(gear_samples);
    }
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
