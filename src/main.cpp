/*
 * Arduino FM Radio Tuner with Si5351 and OLED Display - Low-Side Injection
 * Configurable for Rotary Encoder or Push Buttons
 */

#include <Arduino.h>
#include <Wire.h>
#include <si5351.h>
#include <U8g2lib.h>

// --- Configuration ---
#define USE_BUTTON_MODE 1  // Set to 1 for Buttons (D2, D3, D6), Set to 0 for Encoder (D2, D3, D7)

// --- Pin Definitions ---
#if USE_BUTTON_MODE
  #define BTN_MINUS 3
  #define BTN_PLUS  2
  #define BTN_SW    6
#else
  #define ENCODER_DT  2
  #define ENCODER_CLK 3
  #define ENCODER_SW  7  // This was previously mismatched
#endif

// --- Object Creation ---
Si5351 si5351;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// --- Tuning & Preset Configuration ---
const unsigned long long IF_FREQUENCY_HZ = 10700000ULL; 
const unsigned long STEP_HZ = 100000; 
const unsigned long long MIN_LO_FREQ_HZ = 76800000ULL; 
const unsigned long long MAX_LO_FREQ_HZ = 97300000ULL;

const int PRESET_FM_STATIONS[] = {910, 912, 935, 983, 1005, 1018, 1030, 1064}; 
const int PRESET_COUNT = sizeof(PRESET_FM_STATIONS) / sizeof(PRESET_FM_STATIONS[0]);

// --- State Variables ---
bool direct_tune_mode = false; 
int preset_index = 4; 
unsigned long long lo_frequency_hz = 0ULL; 

// Input Tracking
int lastClkState;
int lastBtnMinusState = HIGH;
int lastBtnPlusState = HIGH;

// Debouncing
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
int lastSwReading = HIGH;
int swState = HIGH;

// --- Helper Functions Prototypes ---
void setFrequencyFromPreset(int index);
void updateLOFrequency(unsigned long long new_lo);
void showBootAnimation();
void updateOledDisplay();
void handleNavigation(int direction);

void setup() {
    u8g2.begin();
    showBootAnimation();
    delay(1500); 

    if (!si5351.init(SI5351_CRYSTAL_LOAD_8PF, 25000000, 0)) {
        while (1);
    }

    #if USE_BUTTON_MODE
        pinMode(BTN_MINUS, INPUT_PULLUP);
        pinMode(BTN_PLUS, INPUT_PULLUP);
        pinMode(BTN_SW, INPUT_PULLUP);
    #else
        pinMode(ENCODER_CLK, INPUT_PULLUP);
        pinMode(ENCODER_DT, INPUT_PULLUP);
        pinMode(ENCODER_SW, INPUT_PULLUP);
        lastClkState = digitalRead(ENCODER_CLK);
    #endif

    setFrequencyFromPreset(preset_index); 
    si5351.set_freq(lo_frequency_hz * 100ULL, SI5351_CLK0);
    si5351.output_enable(SI5351_CLK0, 1);
    updateOledDisplay();
}

void loop() {
    bool frequencyHasChanged = false;
    int direction = 0;

    // --- 1. Handle Navigation Input ---
    #if USE_BUTTON_MODE
        // Read Plus Button
        int readingPlus = digitalRead(BTN_PLUS);
        if (readingPlus == LOW && lastBtnPlusState == HIGH) {
            direction = 1;
            delay(50); // Simple debounce
        }
        lastBtnPlusState = readingPlus;

        // Read Minus Button
        int readingMinus = digitalRead(BTN_MINUS);
        if (readingMinus == LOW && lastBtnMinusState == HIGH) {
            direction = -1;
            delay(50); // Simple debounce
        }
        lastBtnMinusState = readingMinus;
    #else
        // Encoder Logic
        int currentClkState = digitalRead(ENCODER_CLK);
        if (currentClkState != lastClkState) {
            direction = (digitalRead(ENCODER_DT) != currentClkState) ? 1 : -1;
        }
        lastClkState = currentClkState;
    #endif

    if (direction != 0) {
        handleNavigation(direction);
        frequencyHasChanged = true;
    }

    // --- 2. Read Switch Button (Toggle Mode) ---
    // This line now correctly references the names defined above
    int swPin;
    #if USE_BUTTON_MODE
      swPin = BTN_SW;
    #else
      swPin = ENCODER_SW;
    #endif

    int currentSwReading = digitalRead(swPin);

    if (currentSwReading != lastSwReading) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (currentSwReading != swState) {
            swState = currentSwReading;
            if (swState == LOW) {
                direct_tune_mode = !direct_tune_mode;
                frequencyHasChanged = true; 
            }
        }
    }
    lastSwReading = currentSwReading;

    // --- 3. Update Hardware if needed ---
    if (frequencyHasChanged) {
        si5351.set_freq(lo_frequency_hz * 100ULL, SI5351_CLK0);
        updateOledDisplay();
    }
    
    delay(5);
}

void handleNavigation(int direction) {
    if (direct_tune_mode) {
        unsigned long long new_lo = lo_frequency_hz + ((long long)direction * STEP_HZ);
        updateLOFrequency(new_lo); 
    } else {
        preset_index += direction;
        if (preset_index >= PRESET_COUNT) preset_index = 0;
        if (preset_index < 0) preset_index = PRESET_COUNT - 1;
        setFrequencyFromPreset(preset_index);
    }
}

void setFrequencyFromPreset(int index) {
    unsigned long long rf_freq_hz = (unsigned long long)PRESET_FM_STATIONS[index] * 100000ULL;
    lo_frequency_hz = rf_freq_hz - IF_FREQUENCY_HZ;
}

void updateLOFrequency(unsigned long long new_lo) {
    if (new_lo > MAX_LO_FREQ_HZ) lo_frequency_hz = MAX_LO_FREQ_HZ;
    else if (new_lo < MIN_LO_FREQ_HZ) lo_frequency_hz = MIN_LO_FREQ_HZ;
    else lo_frequency_hz = new_lo;
}

void showBootAnimation() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_ncenB12_tr);
        u8g2.drawStr(15, 20, "FM Radio");
        u8g2.drawRFrame(34, 30, 60, 30, 4);
        u8g2.drawCircle(54, 45, 10);
        u8g2.drawDisc(80, 45, 4);
    } while (u8g2.nextPage());
}

void updateOledDisplay() {
    unsigned long long rf_freq_hz = lo_frequency_hz + IF_FREQUENCY_HZ; 
    float rf_mhz = rf_freq_hz / 1000000.0;
    float lo_mhz = lo_frequency_hz / 1000000.0;
    
    char rf_str[10], lo_str[10];
    dtostrf(rf_mhz, 5, 1, rf_str);
    dtostrf(lo_mhz, 5, 1, lo_str);

    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        if (direct_tune_mode) {
            u8g2.drawStr(0, 8, "Tune");
        } else {
            char preset_status[12];
            sprintf(preset_status, "P: %d/%d", preset_index + 1, PRESET_COUNT);
            u8g2.drawStr(0, 8, preset_status);
        }
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(40, 15, "FM Tuner");
        u8g2.drawHLine(0, 18, 128);
        u8g2.setFont(u8g2_font_ncenB18_tr);
        u8g2.drawStr(0, 45, rf_str); 
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(80, 45, "MHz");
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(0, 62, "LO:");
        u8g2.drawStr(20, 62, lo_str); 
        u8g2.drawStr(58, 62, "MHz");
    } while (u8g2.nextPage());
}