export BUILD_DIR=/home/stefan/esp32/lvgl_mp
cd  lvgl_micropython
python3 make.py esp32 BOARD=ESP32_GENERIC BOARD_VARIANT=SPIRAM DISPLAY=ili9341 DISPLAY=st7789 INDEV=xpt2046 INDEV=cst816s FROZEN_MANIFEST=../lms_modules/manifest.py  USER_C_MODULE=/home/stefan/esp32/lvgl_mp/mymod_chipinfo/micropython.cmake USER_C_MODULE=/home/stefan/esp32/lvgl_mp/micropython-ulab/code/micropython.cmake
cp lvgl_micropython/build/lvgl_micropy_ESP32_GENERIC-SPIRAM-4.bin lvgl_mcropython_lms-esp32.bin
