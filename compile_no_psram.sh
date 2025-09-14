export BUILD_DIR=`pwd`
echo $BUILD_DIR
cd  lvgl_micropython
python3 make.py esp32 BOARD=ESP32_GENERIC DISPLAY=ili9341 DISPLAY=st7789 INDEV=xpt2046 INDEV=cst816s FROZEN_MANIFEST=../scripts/manifest.py  USER_C_MODULE=${BUILD_DIR}/mymod_chipinfo/micropython.cmake USER_C_MODULE=${BUILD_DIR}/micropython-ulab/code/micropython.cmake
cp build/lvgl_micropy_ESP32_GENERIC-4.bin ../firmware/lvgl_mcropython_lms-esp32.bin
