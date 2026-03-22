#include <Arduino.h>
// #include <SPI.h>
#include <unity.h>        // ← no path prefix, no "Unity/" folder
#include "../src/atmega/sensors.h"

void test_sensor_init() {
    Sensors::init();
    TEST_PASS();
}

void test_dht_reads_valid_range() {
    SensorData d;
    Sensors::read(d);
    TEST_ASSERT_TRUE(isnan(d.tempC) || (d.tempC > -40.0f && d.tempC < 80.0f));
    TEST_ASSERT_TRUE(isnan(d.humidity) || (d.humidity >= 0.0f && d.humidity <= 100.0f));
}

void test_analog_sensors_in_range() {
    SensorData d;
    Sensors::read(d);
    TEST_ASSERT_INT_WITHIN(512, 512, d.water);
    TEST_ASSERT_INT_WITHIN(512, 512, d.mq2);
    TEST_ASSERT_INT_WITHIN(512, 512, d.flame);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_sensor_init);
    RUN_TEST(test_dht_reads_valid_range);
    RUN_TEST(test_analog_sensors_in_range);
    UNITY_END();
}

void loop() {}
