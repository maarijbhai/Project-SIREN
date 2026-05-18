#include <Wire.h>
#include <math.h>

// --- Pin Definitions ---
#define LED_PIN             PC13
#define NTC_PIN             PA0
// I2C uses default PB6 (SCL) and PB7 (SDA)
// PWM uses default PA8 (CH1) and PA7 (CH1N)

// --- System Thresholds ---
#define TEMP_START_HEATING  -10.0f
#define TEMP_STOP_HEATING   0.0f
#define TEMP_FAULT          45.0f
#define CURRENT_FAULT_MA    2000.0f
#define VOLTAGE_CUTOFF_MV   3000.0f
#define MONITOR_INTERVAL_MS 1000

// --- NTC Thermistor Constants ---
#define ADC_VREF            3.3f
#define ADC_RESOLUTION      4095.0f
#define NTC_R_FIXED         10000.0f
#define NTC_R25             10000.0f
#define NTC_B               3900.0f
#define NTC_T25_K           298.15f

// --- INA219 Constants ---
#define INA219_ADDR         0x40 
#define INA219_REG_CONFIG   0x00
#define INA219_REG_BUS_V    0x02
#define INA219_REG_CURRENT  0x04
#define INA219_REG_CALIB    0x05
#define INA219_CONFIG_VAL   0x399F
#define INA219_CAL_VAL      4096
#define INA219_LSB_MA       0.1f

// --- State Machine ---
enum SystemState {
    STATE_IDLE    = 0,
    STATE_HEATING = 1,
    STATE_FAULT   = 2
};

SystemState g_state = STATE_IDLE;
float g_temperature_c = 0.0f;
float g_current_ma    = 0.0f;
float g_voltage_mv    = 0.0f;
uint32_t g_last_monitor = 0;
char g_tx_buf[128];

// ==========================================
// TIM1 PWM & Deadband Initialization (CMSIS)
// ==========================================
void BPC_Init() {
    // Enable TIM1 and GPIOA clocks
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPAEN;

    // Configure PA8 (CH1) and PA7 (CH1N) as Alternate Function Push-Pull (50MHz)
    GPIOA->CRH = (GPIOA->CRH & ~(0xF << 0)) | (0xB << 0);   // PA8
    GPIOA->CRL = (GPIOA->CRL & ~(0xF << 28)) | (0xB << 28); // PA7

    // Set 1kHz Frequency at 72MHz
    TIM1->PSC = 71;
    TIM1->ARR = 999;
    TIM1->CCR1 = 499; // 50% Duty Cycle

    // PWM Mode 1
    TIM1->CCMR1 = (6 << 4) | TIM_CCMR1_OC1PE; 
    
    // Enable CH1 and CH1N Outputs
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1NE;

    // Set Hardware Deadtime (14 = ~194ns)
    TIM1->BDTR = 14;

    // Start the counter (but keep Main Output Enable off for now)
    TIM1->CR1 |= TIM_CR1_CEN;
}

void BPC_Start() {
    TIM1->BDTR |= TIM_BDTR_MOE; // Enable Main Output
}

void BPC_Stop() {
    TIM1->BDTR &= ~TIM_BDTR_MOE; // Disable Main Output
}

// ==========================================
// INA219 Sensor Functions
// ==========================================
void INA219_WriteReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(INA219_ADDR);
    Wire.write(reg);
    Wire.write((value >> 8) & 0xFF);
    Wire.write(value & 0xFF);
    Wire.endTransmission();
}

uint16_t INA219_ReadReg(uint8_t reg) {
    Wire.beginTransmission(INA219_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    
    Wire.requestFrom((uint8_t)INA219_ADDR, (uint8_t)2);
    if (Wire.available() >= 2) {
        uint16_t val = Wire.read() << 8;
        val |= Wire.read();
        return val;
    }
    return 0;
}

void INA219_Init() {
    Wire.begin(); 
    INA219_WriteReg(INA219_REG_CALIB, INA219_CAL_VAL);
    INA219_WriteReg(INA219_REG_CONFIG, INA219_CONFIG_VAL);
}

float INA219_ReadCurrent_mA() {
    int16_t raw = (int16_t)INA219_ReadReg(INA219_REG_CURRENT);
    return (float)raw * INA219_LSB_MA;
}

float INA219_ReadBusVoltage_mV() {
    uint16_t raw = INA219_ReadReg(INA219_REG_BUS_V);
    return (float)(raw >> 3) * 4.0f;
}

// ==========================================
// ADC NTC Thermistor Function
// ==========================================
float ADC_ReadTemperature_C() {
    int raw = analogRead(NTC_PIN);
    float vadc = ((float)raw / ADC_RESOLUTION) * ADC_VREF;
    
    if (vadc <= 0.01f || vadc >= (ADC_VREF - 0.01f)) return -99.0f;
    
    float r_ntc = NTC_R_FIXED * vadc / (ADC_VREF - vadc);
    float inv_T = (1.0f / NTC_T25_K) + (1.0f / NTC_B) * log(r_ntc / NTC_R25);
    return (1.0f / inv_T) - 273.15f;
}

// ==========================================
// Core State Machine
// ==========================================
void StateMachine_Update() {
    switch (g_state) {
        case STATE_IDLE:
            if (g_temperature_c < TEMP_START_HEATING && g_voltage_mv > VOLTAGE_CUTOFF_MV) {
                BPC_Start();
                g_state = STATE_HEATING;
                digitalWrite(LED_PIN, LOW); // Active Low LED ON
            }
            break;

        case STATE_HEATING:
            if (g_temperature_c >= TEMP_STOP_HEATING) {
                BPC_Stop();
                g_state = STATE_IDLE;
                digitalWrite(LED_PIN, HIGH); // Active Low LED OFF
            }
            else if (g_current_ma > CURRENT_FAULT_MA) {
                BPC_Stop();
                g_state = STATE_FAULT;
                Serial.print("FAULT:OVERCURRENT\r\n");
            }
            else if (g_temperature_c > TEMP_FAULT) {
                BPC_Stop();
                g_state = STATE_FAULT;
                Serial.print("FAULT:OVERTEMP\r\n");
            }
            else if (g_voltage_mv < VOLTAGE_CUTOFF_MV) {
                BPC_Stop();
                g_state = STATE_IDLE;
                Serial.print("INFO:UNDERVOLTAGE\r\n");
            }
            break;

        case STATE_FAULT:
            BPC_Stop();
            digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Blink LED
            break;

        default:
            g_state = STATE_FAULT;
            break;
    }
}

void Data_Transmit() {
    const char *state_str;
    switch (g_state) {
        case STATE_IDLE:    state_str = "IDLE";    break;
        case STATE_HEATING: state_str = "HEATING"; break;
        case STATE_FAULT:   state_str = "FAULT";   break;
        default:            state_str = "UNKNOWN"; break;
    }

    int temp_int  = (int)(g_temperature_c * 10);
    int curr_int  = (int)(g_current_ma * 10);
    int volt_int  = (int)(g_voltage_mv * 10);

    snprintf(g_tx_buf, sizeof(g_tx_buf),
        "%s,%d.%d,%d.%d,%d.%d\r\n",
        state_str,
        temp_int / 10, abs(temp_int % 10),
        curr_int / 10, abs(curr_int % 10),
        volt_int / 10, abs(volt_int % 10));
        
    // Transmit over USB Serial monitor
    Serial.print(g_tx_buf);
}

// ==========================================
// Setup and Loop
// ==========================================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // Ensure LED is off
    
    // Initialize standard Arduino Serial for USB monitoring
    Serial.begin(115200);
    
    // Force Arduino ADC to match the 12-bit resolution of the STM32 HAL
    analogReadResolution(12);

    // Initialize Subsystems
    BPC_Init();
    BPC_Stop();
    INA219_Init();
    
    Serial.print("SIREN BPC Ready\r\n");
    g_last_monitor = millis();
}

void loop() {
    uint32_t now = millis();
    
    // 1-Second Non-Blocking Monitoring Loop
    if ((now - g_last_monitor) >= MONITOR_INTERVAL_MS) {
        g_last_monitor  = now;
        
        g_temperature_c = ADC_ReadTemperature_C();
        g_current_ma    = INA219_ReadCurrent_mA();
        g_voltage_mv    = INA219_ReadBusVoltage_mV();
        
        StateMachine_Update();
        Data_Transmit();
    }
}