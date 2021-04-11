#!/bin/bash -xeu
./build-noinstr.sh
./build-instr.sh
./build-fullinstr.sh