#!/bin/bash

# المسار الصحيح للمترجم بناءً على شجرة المجلدات الخاصة بك
COMPILER="./bin/arduino-cli"

# تشغيل البناء
$COMPILER compile --fqbn esp32:esp32:esp32s3 \
--build-path SmartHive_Slave/build \
SmartHive_Slave/SmartHive_Slave.ino