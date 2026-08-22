#include <Arduino.h>

#if defined(ECLOCK_CORE_MBED)
#include <ArduinoBLE.h>
#else
#error "Must compile with mbed core for ArduinoBLE!"
#endif

#include "board_pins.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Standard Current Time Service UUID
BLEService timeService("1805");

// Current Time Characteristic UUID. 
// Our HA component writes 8 bytes: int32 epoch, int32 tz_offset (little-endian)
BLECharacteristic timeCharacteristic("2A2B", BLEWrite | BLEWriteWithoutResponse | BLENotify | BLERead, 8);

// Time state
static int32_t g_epoch = 0;
static int32_t g_tz_offset = 0;
static uint32_t g_last_tick_millis = 0;
static bool g_time_synced = false;
static bool g_needs_display_update = false;

static uint8_t  g_hour = 0;
static uint8_t  g_minute = 0;
static uint8_t  g_second = 0;

static void updateTimeStruct() {
    if (!g_time_synced) return;
    int32_t local_time = g_epoch + g_tz_offset;
    g_hour = (local_time / 3600) % 24;
    g_minute = (local_time / 60) % 60;
    g_second = local_time % 60;
}

static void drawClockFace() {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        char timeBuf[16];
        if (g_time_synced) {
            // Only show HH:MM so it behaves like a clock
            snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", g_hour, g_minute);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "Waiting Sync");
        }
        
        display.setFont(&FreeSansBold24pt7b);
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(timeBuf, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((display.width() - bw) / 2 - bx,
                          (display.height() + bh) / 2 - 8);
        display.print(timeBuf);

        display.setFont(&FreeSans9pt7b);
        display.setCursor(4, display.height() - 5);
        display.print(g_time_synced ? F("Synced via HA") : F("No time"));
    } while (display.nextPage());
}

void setup() {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    Serial.begin(115200);
    
    // Power the panel
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(50);
    
    // Release mbed TWIM pins if needed (mostly legacy)
    #if defined(ECLOCK_CORE_MBED) && defined(NRF_TWIM0)
        eclock_release_twim_pins();
    #endif

    display.init(115200, true, 2, false);
    display.setRotation(1);
    
    drawClockFace();

    if (!BLE.begin()) {
        Serial.println("FAIL: BLE.begin() failed!");
        while (1) {
            digitalWrite(LED_RED, LOW);
            delay(100);
            digitalWrite(LED_RED, HIGH);
            delay(100);
        }
    }

    BLE.setLocalName("ePaper Clock");
    BLE.setAdvertisedService(timeService);
    timeService.addCharacteristic(timeCharacteristic);
    BLE.addService(timeService);
    
    uint8_t init_val[8] = {0};
    timeCharacteristic.writeValue(init_val, sizeof(init_val));
    BLE.advertise();
    
    Serial.println("BLE Advertising started!");
    g_last_tick_millis = millis();
}

void loop() {
    BLE.poll();

    if (timeCharacteristic.written()) {
        if (timeCharacteristic.valueLength() == 8) {
            const uint8_t* val = timeCharacteristic.value();
            memcpy(&g_epoch, val, 4);
            memcpy(&g_tz_offset, val + 4, 4);
            
            Serial.print("Received Time Sync! Epoch: ");
            Serial.println(g_epoch);

            g_time_synced = true;
            updateTimeStruct();
            g_needs_display_update = true;
            
            // Stop advertising to save battery now that we have the time
            BLE.stopAdvertise();
            Serial.println("Time synced. Advertising stopped.");
            
            // Flash green
            digitalWrite(LED_GREEN, LOW);
            delay(10); 
            digitalWrite(LED_GREEN, HIGH);
        }
    }

    if (g_needs_display_update) {
        drawClockFace();
        g_needs_display_update = false;
    }

    uint32_t now = millis();
    if (g_time_synced && (now - g_last_tick_millis >= 1000)) {
        g_epoch++;
        updateTimeStruct();
        g_last_tick_millis = now;
        
        // Update display every minute when seconds hit 0
        if (g_second == 0) {
            g_needs_display_update = true;
        }
    }
}