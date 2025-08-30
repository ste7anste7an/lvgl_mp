export LMS_ESP32_MODULES=../lms-frozen
rm -rf ../submodules
rm -rf ../lms-frozen
mkdir -p ../submodules
mkdir -p ../lms-frozen

mkdir -p ../lvgl_micropython/micropy_updates/esp32/modules/
cp inisetup.py ../lvgl_micropython/micropy_updates/esp32/modules

cd ../submodules
git clone https://github.com/v923z/micropython-ulab.git
git clone -b frozen-modules-esp32 https://github.com/antonvh/mpy-robot-tools.git
git clone https://github.com/antonvh/SerialTalk.git
git clone https://github.com/antonvh/PUPRemote.git
git clone https://github.com/antonvh/PyHuskyLens.git

# change inisetup.py for starting test_dut/start_test

#cp ../inisetup/inisetp.py ../lvgl_micropython/lib/micropython/ports/esp32/modules

cp ../lms_modules/*.py $LMS_ESP32_MODULES
cp ../submodules/PUPRemote/src/lpf2.py $LMS_ESP32_MODULES
cp ../submodules/PUPRemote/src/pupremote.py $LMS_ESP32_MODULES
cp ../submodules/mpy-robot-tools/mpy_robot_tools/servo.py $LMS_ESP32_MODULES
cp ../submodules/mpy-robot-tools/mpy_robot_tools/np_animation.py $LMS_ESP32_MODULES
cp ../submodules/mpy-robot-tools/mpy_robot_tools/bt.py $LMS_ESP32_MODULES
cp ../submodules/mpy-robot-tools/mpy_robot_tools/ctrl_plus.py $LMS_ESP32_MODULES
cp ../submodules/mpy-robot-tools/mpy_robot_tools/ctrl_plus.py $LMS_ESP32_MODULES
cp ../submodules/PyHuskyLens/Library/pyhuskylens.py $LMS_ESP32_MODULES
cp ../submodules/SerialTalk/uartremote.py $LMS_ESP32_MODULES
mkdir -p $LMS_ESP32_MODULES/serialtalk
cp ../submodules/SerialTalk/serialtalk/usockets.py $LMS_ESP32_MODULES/serialtalk
cp ../submodules/SerialTalk/serialtalk/esp32.py $LMS_ESP32_MODULES/serialtalk
cp ../submodules/SerialTalk/serialtalk/serialtalk.py $LMS_ESP32_MODULES/serialtalk
cp ../submodules/SerialTalk/serialtalk/__init__.py $LMS_ESP32_MODULES/serialtalk
cp ../submodules/SerialTalk/serialtalk/auto.py $LMS_ESP32_MODULES/serialtalk
