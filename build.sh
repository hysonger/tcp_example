#!/bin/sh

if [ -z $1 ]; then
    cmake . && make clean && make -j4 program
elif [ $1 = "check" ]; then
    cppcheck src/ --enable=all -I /usr/include -I include/ 2>&1 | tee cppcheck.log
elif [ $1 = "test" ]; then
    cmake . && make clean && make -j4 test_tcp && ./test_tcp | tee test_tcp.log
fi