
set MSYSTEM=
call %HOMEPATH%\esp\esp-idf\export.bat
rem first build the project using cmake/idf.py
idf.py clean
idf.py all
rem now generate the combined binary image
cd build
%IDF_PATH%\components\esptool_py\esptool\esptool.py --chip esp32 merge_bin -o DuetWiFiServer.bin --flash_mode dio --flash_freq 80m --flash_size 4MB 0x1000 bootloader/bootloader.bin 0x10000 DuetWiFiSocketServer.bin 0x8000 partition_table/partition-table.bin
cd ..
