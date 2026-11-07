#ifndef CAR_SYNTH_H
#define CAR_SYNTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The sample rate of the output, in samples each second
// The loops that you give to the synth must use the same rate
#define CAR_SYNTH_SAMPLE_RATE 44100

// The engine speed at which the rev limiter operates, in RPM
// Define this macro before you include this file to use a different value
#ifndef CAR_SYNTH_RPM_LIMIT
#define CAR_SYNTH_RPM_LIMIT 6300.0f
#endif

// The values of the car that control the sound
typedef struct CarSynthInput
{
    float rpm;      // engine speed, in revolutions each minute
    float throttle; // throttle position, from 0 to 1
    float speed;    // speed of the car, always positive
    float slip;     // sideways speed of the tyres, always positive
    bool grass;     // false on the road, true on grass
    float stall;    // from 0 to 1, the engine sputters more when this value increases
    bool running;   // false when the engine is off, also while the starter motor turns it
    bool cranking;  // true while the starter motor turns the engine
} CarSynthInput;

// The state of the synth
// The synth makes the engine, the tyres on the ground, the slide and the screech
// The synth calculates the sound. Only the screech and the gear change use recordings.
// Do not change the fields. Use the functions below.
typedef struct CarSynth
{
    CarSynthInput smooth;  // the input after smoothing, which prevents clicks
    float fire_phase;      // from 0 to 1 between two firings of the engine
    float fire_time;       // seconds since the last firing
    float push_seconds;    // length of the push of the current firing
    float wander;          // slow random change of the idle speed, from -1 to 1
    float wander_target;   // the value that wander moves towards
    float crank_phase;     // from 0 to 1 for each turn of the crankshaft
    int cylinder;          // the cylinder that fired last
    float fire_gain;       // loudness of the current firing
    float shudder_phase;   // from 0 to 1, the shake of an engine that turns too slowly
    float limiter_phase;   // from 0 to 1, the rhythm of the rev limiter
    float limiter_gain;    // loudness that the rev limiter leaves
    float starter_phase;   // from 0 to 1, the turn of the starter motor
    float starter_level;   // loudness of the starter motor
    float resonator[3][4]; // the exhaust: the last two inputs and outputs of each resonant filter
    float engine_low;      // state of the low pass filter of the engine
    float dc_in;           // state of the DC blocking filter: last input
    float dc_out;          // state of the DC blocking filter: last output
    float ground_low[2];   // state of the filters for the ground noise
    float slide_low;       // state of the low filter for the slide noise
    float slide_high;      // state of the high filter for the slide noise
    const float *screech_samples; // loop of the tyre screech, mono at CAR_SYNTH_SAMPLE_RATE
    int screech_count;     // number of samples in the screech loop
    float screech_position; // sample position in the screech loop
    float clunk_time;      // seconds since the last gear clunk, negative when there is none
    const float *gear_samples; // sound of a gear change, mono at CAR_SYNTH_SAMPLE_RATE
    int gear_count;        // number of samples in the gear sound
    int gear_position;     // sample position in the gear sound, gear_count when it does not play
    uint32_t noise;        // state of the random number generator
} CarSynth;

// Set the synth to its initial state
// Call this function one time before you use the synth
void CarSynthInit(CarSynth *synth);

// Set the loop of the tyre screech
// The synth does not copy the samples. Keep them valid while the synth uses them.
// Give NULL to remove the loop. Then the tyres do not screech.
void CarSynthSetScreech(CarSynth *synth, const float *samples, int count);

// Set the sound of a gear change
// The synth does not copy the samples. Keep them valid while the synth uses them.
// Give NULL to remove the sound. Then the synth makes a short low thud.
void CarSynthSetGear(CarSynth *synth, const float *samples, int count);

// Start the sound of a gear change
void CarSynthClunk(CarSynth *synth);

// Write mono samples to out. Each sample is from -1 to 1.
// This function does not allocate memory and does not use locks.
// You can call it from an audio thread.
void CarSynthRender(CarSynth *synth, const CarSynthInput *input, float *out, int frames);

#ifdef __cplusplus
}
#endif

#endif
