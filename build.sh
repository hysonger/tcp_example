#!/bin/sh

if [ -d $1 ]; then
    cmake . && make clean && make -j4
elif [ $1 = "check" ]; then
    cppcheck src/ --enable=all -I /usr/include -I include/ 2>&1 | tee cppcheck.log
fi