/* Minimal freestanding <math.h> for wasm32-unknown-unknown. sqrt, floor, ceil,
   trunc, fabs and their f32 forms lower to single wasm instructions; the
   transcendental ones are defined in goose_wasm_rt.c. */
#pragma once
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define HUGE_VAL (__builtin_huge_val())
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double sqrt(double);  float sqrtf(float);
double fabs(double);  float fabsf(float);
double floor(double); float floorf(float);
double ceil(double);  float ceilf(float);
double trunc(double); float truncf(float);
double round(double); float roundf(float);
double fmod(double, double); float fmodf(float, float);
double sin(double);   float sinf(float);
double cos(double);   float cosf(float);
double tan(double);   float tanf(float);
double asin(double);  float asinf(float);
double acos(double);  float acosf(float);
double atan(double);  float atanf(float);
double atan2(double, double); float atan2f(float, float);
double exp(double);   float expf(float);
double log(double);   float logf(float);
double log2(double);  float log2f(float);
double log10(double); float log10f(float);
double pow(double, double); float powf(float, float);
