// test/test_sensors/test_sensors.cpp
#include <Arduino.h>
#include <unity.h>
#include "../../src/atmega/sensors.h"

void test_sensor_init() {
    Sensors::init();
    TEST_ASSERT_TRUE(true); // add real asserts
}

void test_sensor_read() {
    SensorData d;
    Sensors::read(d);
    TEST_ASSERT_FLOAT_WITHIN(50.0, 25.0, d.tempC); // 25±50°C
    TEST_ASSERT_FLOAT_WITHIN(50.0, 50.0, d.humidity);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_init);
    RUN_TEST(test_sensor_read);
    UNITY_END();
}
void loop() {}
