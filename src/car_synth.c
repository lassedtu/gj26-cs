#include "car_synth.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.2831853f
#define DT (1.0f / CAR_SYNTH_SAMPLE_RATE)

#define RPM_SMOOTH_SECONDS 0.015f
#define THROTTLE_SMOOTH_SECONDS 0.04f
#define MOTION_SMOOTH_SECONDS 0.08f

#define PUSH_SECONDS 0.0012f  // shortest push of one firing into the exhaust, at high engine speed
#define PUSH_PART 0.12f       // the push is at least this part of the time between two firings
#define IDLE_SOFT_RPM 1800.0f // below this speed the engine sounds more like an idle
#define IDLE_JITTER 0.15f     // more random change of the loudness of each firing at idle
#define IDLE_WANDER 0.03f     // the idle speed changes slowly by this part
#define WANDER_SECONDS 0.25f  // time constant of the change of the idle speed
#define IDLE_QUIET 0.3f       // the idle is this much quieter
#define BURST_SECONDS 0.002f  // the noise of one firing decays in this time
#define FIRE_JITTER 0.15f     // random change of the loudness of each firing
#define LIMITER_HZ 14.0f      // the rev limiter cuts the engine this many times each second
#define LIMITER_CUT 0.45f     // part of each limiter cycle without firings
#define ENGINE_VOLUME 0.95f

#define STALL_MISS 1.2f      // at a full stall, this part of the firings miss (more than 1 means all of them)
#define CRANK_GAIN 0.35f     // the starter motor only compresses the air, it does not fire
#define STOP_FADE_RPM 500.0f // the firings of a stopping engine get weaker below this speed
#define STARTER_HZ 160.0f    // pitch of the starter motor
#define STARTER_VOLUME 0.06f

#define LUG_RPM 1500.0f  // below this speed the engine shakes when it pulls
#define LUG_RANGE 600.0f // the shake is at full strength this far below LUG_RPM
#define SHUDDER_HZ 9.0f  // speed of the shake

#define GROUND_SPEED 300.0f // the ground is at full loudness at this speed
#define GROUND_VOLUME 0.9f
#define SLIDE_START 8.0f    // a slide below this speed makes no sound
#define SLIDE_FULL 60.0f    // a slide at this speed has full loudness
#define SLIDE_VOLUME 0.15f
#define SCREECH_START 20.0f // a slide below this speed does not screech
#define SCREECH_FULL 90.0f  // a slide at this speed gives a full screech
#define SCREECH_PITCH 0.08f // the screech is this much higher at a full slide
#define SCREECH_VOLUME 0.9f
#define CLUNK_SECONDS 0.2f  // length of the synthetic gear thud
#define GEAR_VOLUME 0.6f

// Relative loudness of the four cylinders
static const float CYLINDER_GAIN[4] = {1.0f, 0.82f, 0.93f, 0.78f};

// The exhaust rings at these frequencies when a firing pushes into it
// The three frequencies are the body, the bark and the rasp.
static const float RESONATOR_HZ[3] = {95.0f, 320.0f, 900.0f};
static const float RESONATOR_WIDTH_HZ[3] = {50.0f, 140.0f, 500.0f};
static const float RESONATOR_LEVEL[3] = {1.0f, 0.7f, 0.3f};

// Return a random number from -1 to 1 (xorshift generator)
static float Noise(CarSynth *synth)
{
    uint32_t x = synth->noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    synth->noise = x;
    return (float)(x >> 8) / 8388608.0f - 1.0f;
}

static float Clamp01(float value)
{
    return fminf(fmaxf(value, 0.0f), 1.0f);
}

// Return the part of the way to the target that a one pole filter moves in one sample
static float Coefficient(float cutoff_hz)
{
    return 1.0f - expf(-TWO_PI * cutoff_hz * DT);
}

// Return the part of the way to the target that a smoother moves in one sample
static float Smoothing(float seconds)
{
    return 1.0f - expf(-DT / seconds);
}

void CarSynthInit(CarSynth *synth)
{
    memset(synth, 0, sizeof(*synth));
    synth->smooth.rpm = 900.0f;
    synth->smooth.running = true;
    synth->fire_gain = 1.0f;
    synth->limiter_gain = 1.0f;
    synth->push_seconds = PUSH_SECONDS;
    synth->clunk_time = -1.0f;
    synth->noise = 0x9E3779B9u;
}

void CarSynthSetScreech(CarSynth *synth, const float *samples, int count)
{
    synth->screech_samples = samples;
    synth->screech_count = samples != NULL ? count : 0;
    synth->screech_position = 0.0f;
}

void CarSynthSetGear(CarSynth *synth, const float *samples, int count)
{
    synth->gear_samples = samples;
    synth->gear_count = samples != NULL ? count : 0;
    synth->gear_position = synth->gear_count;
}

void CarSynthClunk(CarSynth *synth)
{
    if (synth->gear_count > 0)
    {
        synth->gear_position = 0;
    }
    else
    {
        synth->clunk_time = 0.0f;
    }
}

// A band pass filter with a gain of about 1 at its centre
static float Resonate(float *state, float x, float hz, float width_hz)
{
    float r = expf(-3.1415927f * width_hz * DT);
    float y = 0.5f * (1.0f - r * r) * (x - state[1]) + 2.0f * r * cosf(TWO_PI * hz * DT) * state[2] -
              r * r * state[3];
    state[1] = state[0];
    state[0] = x;
    state[3] = state[2];
    state[2] = y;
    return y;
}

// Make one sample of the engine
static float Engine(CarSynth *synth)
{
    bool running = synth->smooth.running;
    float rpm = running ? fmaxf(synth->smooth.rpm, 600.0f) : synth->smooth.rpm;
    float load = synth->smooth.throttle;
    float stall = synth->smooth.stall;
    float lug = fmaxf(load * Clamp01((LUG_RPM - rpm) / LUG_RANGE), stall);
    float idle = (1.0f - load) * Clamp01((IDLE_SOFT_RPM - rpm) / (IDLE_SOFT_RPM - 900.0f));

    synth->wander += (synth->wander_target - synth->wander) * Smoothing(WANDER_SECONDS);
    rpm *= 1.0f + IDLE_WANDER * idle * synth->wander;

    // A four stroke engine with four cylinders fires two times in each turn
    synth->fire_phase += rpm >= 60.0f ? rpm / 30.0f * DT : 0.0f;
    synth->fire_time += DT;
    if (synth->fire_phase >= 1.0f)
    {
        synth->fire_phase -= 1.0f;
        synth->fire_time = 0.0f;
        synth->cylinder = (synth->cylinder + 1) & 3;
        float jitter = FIRE_JITTER * (0.4f + load) + IDLE_JITTER * idle + 0.5f * lug + 0.8f * stall;
        synth->fire_gain = CYLINDER_GAIN[synth->cylinder] * (1.0f + Noise(synth) * jitter);
        synth->push_seconds = fmaxf(PUSH_SECONDS, PUSH_PART * 30.0f / rpm);
        if (synth->cylinder == 0)
        {
            synth->wander_target = Noise(synth);
        }
        if (Noise(synth) < lug * 0.5f - 1.0f)
        {
            synth->fire_gain *= 0.2f;
        }
        if (Noise(synth) < stall * STALL_MISS * 2.0f - 1.0f)
        {
            synth->fire_gain *= 0.15f;
        }
        if (synth->smooth.cranking)
        {
            synth->fire_gain *= CRANK_GAIN;
        }
        else if (!running)
        {
            synth->fire_gain *= Clamp01(rpm / STOP_FADE_RPM);
        }
    }
    synth->crank_phase = fmodf(synth->crank_phase + rpm / 60.0f * DT, 1.0f);
    synth->shudder_phase = fmodf(synth->shudder_phase + SHUDDER_HZ * DT, 1.0f);

    float t = synth->fire_time;
    float push = t < synth->push_seconds ? sinf(3.1415927f * t / synth->push_seconds) : 0.0f;
    float burst = Noise(synth) * expf(-t / BURST_SECONDS) * (0.15f + 0.35f * load);
    float x = synth->fire_gain * (push + burst);

    float y = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        float level = RESONATOR_LEVEL[i] * (i == 2 ? 0.3f + 0.7f * load : 1.0f);
        y += level * Resonate(synth->resonator[i], x, RESONATOR_HZ[i], RESONATOR_WIDTH_HZ[i]);
    }
    y += 0.3f * lug * sinf(TWO_PI * synth->crank_phase);
    y *= 1.0f - 0.45f * lug * (0.5f + 0.5f * sinf(TWO_PI * synth->shudder_phase));

    // The rev limiter cuts the engine in a fast rhythm
    float limiter_target = 1.0f;
    if (rpm >= CAR_SYNTH_RPM_LIMIT - 40.0f && load > 0.5f)
    {
        synth->limiter_phase = fmodf(synth->limiter_phase + LIMITER_HZ * DT, 1.0f);
        limiter_target = synth->limiter_phase < LIMITER_CUT ? 0.2f : 1.0f;
    }
    synth->limiter_gain += (limiter_target - synth->limiter_gain) * Smoothing(0.003f);
    y *= synth->limiter_gain;

    synth->engine_low += (y - synth->engine_low) * Coefficient(1500.0f + 1500.0f * load);
    y = synth->engine_low;

    // Remove the DC offset
    float out = y - synth->dc_in + 0.995f * synth->dc_out;
    synth->dc_in = y;
    synth->dc_out = out;
    return out * ENGINE_VOLUME * (0.45f + 0.55f * load) * (1.0f - IDLE_QUIET * idle);
}

// Make one sample of the starter motor
static float Starter(CarSynth *synth)
{
    float target = synth->smooth.cranking ? 1.0f : 0.0f;
    synth->starter_level += (target - synth->starter_level) * Smoothing(0.02f);
    if (synth->starter_level < 0.001f)
    {
        return 0.0f;
    }
    synth->starter_phase = fmodf(synth->starter_phase + STARTER_HZ * DT, 1.0f);
    float whine = sinf(TWO_PI * synth->starter_phase) + 0.5f * sinf(2.0f * TWO_PI * synth->starter_phase);
    float strain = 0.6f + 0.4f * expf(-synth->fire_time / 0.03f);
    return whine * strain * synth->starter_level * STARTER_VOLUME;
}

// Make one sample of the tyres on the ground
static float Ground(CarSynth *synth)
{
    bool grass = synth->smooth.grass;
    float amount = Clamp01(synth->smooth.speed / GROUND_SPEED);
    float a = Coefficient(grass ? 250.0f : 400.0f);
    synth->ground_low[0] += (Noise(synth) - synth->ground_low[0]) * a;
    synth->ground_low[1] += (synth->ground_low[0] - synth->ground_low[1]) * a;
    return synth->ground_low[1] * amount * GROUND_VOLUME;
}

// Make one sample of the tyre screech
// The tyres do not screech on grass.
static float Screech(CarSynth *synth)
{
    float amount = Clamp01((synth->smooth.slip - SCREECH_START) / (SCREECH_FULL - SCREECH_START));
    if (synth->screech_count < 2 || synth->smooth.grass || amount <= 0.0f)
    {
        return 0.0f;
    }
    int index = (int)synth->screech_position;
    float part = synth->screech_position - (float)index;
    float a = synth->screech_samples[index];
    float b = synth->screech_samples[(index + 1) % synth->screech_count];
    synth->screech_position += 1.0f + SCREECH_PITCH * amount;
    if (synth->screech_position >= (float)synth->screech_count)
    {
        synth->screech_position -= (float)synth->screech_count;
    }
    return (a + (b - a) * part) * amount * amount * SCREECH_VOLUME;
}

// Make one sample of the slide noise
static float Slide(CarSynth *synth)
{
    float amount = Clamp01((synth->smooth.slip - SLIDE_START) / (SLIDE_FULL - SLIDE_START));
    float n = Noise(synth);
    synth->slide_low += (n - synth->slide_low) * Coefficient(1800.0f);
    synth->slide_high += (n - synth->slide_high) * Coefficient(300.0f);
    return (synth->slide_low - synth->slide_high) * amount * SLIDE_VOLUME;
}

// Make one sample of the gear change
static float Clunk(CarSynth *synth)
{
    if (synth->gear_position < synth->gear_count)
    {
        return synth->gear_samples[synth->gear_position++] * GEAR_VOLUME;
    }
    if (synth->clunk_time < 0.0f)
    {
        return 0.0f;
    }
    float t = synth->clunk_time;
    synth->clunk_time = t + DT > CLUNK_SECONDS ? -1.0f : t + DT;
    return 0.35f * sinf(TWO_PI * 65.0f * t) * expf(-t * 35.0f);
}

void CarSynthRender(CarSynth *synth, const CarSynthInput *input, float *out, int frames)
{
    float rpm_rate = Smoothing(RPM_SMOOTH_SECONDS);
    float throttle_rate = Smoothing(THROTTLE_SMOOTH_SECONDS);
    float motion_rate = Smoothing(MOTION_SMOOTH_SECONDS);
    synth->smooth.grass = input->grass;
    synth->smooth.running = input->running;
    synth->smooth.cranking = input->cranking;

    for (int i = 0; i < frames; i++)
    {
        CarSynthInput *s = &synth->smooth;
        s->rpm += (input->rpm - s->rpm) * rpm_rate;
        s->throttle += (input->throttle - s->throttle) * throttle_rate;
        s->speed += (input->speed - s->speed) * motion_rate;
        s->slip += (input->slip - s->slip) * motion_rate;
        s->stall += (input->stall - s->stall) * throttle_rate;

        float mix = Engine(synth) + Ground(synth) + Slide(synth) + Screech(synth) + Starter(synth) + Clunk(synth);
        out[i] = tanhf(mix * 1.2f) * 0.8f;
    }
}
