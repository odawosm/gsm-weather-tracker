// Weather station configuration: pins, phone numbers, thresholds, timings.
#pragma once

// ---------- Pins ----------
#define PIN_DHT       2
#define PIN_RAIN_DIG  3    // rain board D0 (LOW = wet), not used for logic, kept for wiring reference
#define PIN_RAIN_PWR  4    // rain board VCC, HIGH only while reading
#define PIN_SIM_RX    7    // Arduino RX <- SIM800L TXD
#define PIN_SIM_TX    8    // Arduino TX -> SIM800L RXD
#define PIN_LDR       A0
#define PIN_RAIN_A    A1
#define OLED_ADDR     0x3C

// ---------- Phone numbers (international format, as the network reports them) ----------
// May send WEATHER / STATUS / HELP and receive alerts. Numbers containing 'X' are skipped.
static const char *const AUTHORIZED[] = { "+2547XXXXXXXX" };

// Hourly report to SUBSCRIBERS: 1 = on, 0 = off.
#define HOURLY_REPORT 0
static const char *const SUBSCRIBERS[] = { "+2547XXXXXXXX" };

// ---------- Calibration ----------
// Rain sensor raw value (A1): at or above a threshold falls in that class. Calibrate with tests/06_rain.
#define RAIN_DRY    900    // >= dry
#define RAIN_DAMP   700    // >= damp
#define RAIN_LIGHT  400    // >= light rain, below = heavy rain
// LDR raw value mapped to 0-100 %. Calibrate with tests/05_ldr (covered = dark, daylight = bright).
#define LIGHT_RAW_DARK    0
#define LIGHT_RAW_BRIGHT  1023
// DHT11 corrections against a reference thermometer/hygrometer.
#define TEMP_OFFSET_C10   0    // tenths of a degree, e.g. -15 = -1.5 C
#define HUM_OFFSET        0    // %RH

// ---------- Alerts ----------
#define TEMP_HIGH_C        30
#define TEMP_LOW_C         10
#define LIGHT_DROP_PCT     40      // light falls by more than this many % points...
#define LIGHT_DROP_SAMPLES 4       // ...compared with the brightest of the last 4 trend samples (4 x 5 min = 20 min)
#define ALERT_COOLDOWN_MS  1800000UL  // 30 min per alert type

// ---------- Trend (compares now with the oldest sample in the ring buffer) ----------
#define RING_SIZE        12         // 12 x 5 min = 1 hour
#define TREND_TEMP_C10   10         // 1.0 C
#define TREND_HUM        5          // %RH
#define TREND_LIGHT      15         // % points

// ---------- Timings ----------
#define READ_INTERVAL_MS  5000UL
#define TREND_SAMPLE_MS   300000UL   // 5 min
#define GSM_CHECK_MS      30000UL    // signal/registration poll, and re-init if the module stops answering
#define HOURLY_MS         3600000UL
// The laptop dashboard sends an online forecast over USB every 5 min; WEATHER reports include it while it's fresh.
#define FORECAST_MAX_AGE_MS 5400000UL  // 90 min
