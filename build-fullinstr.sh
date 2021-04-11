#!/bin/bash -xeu
. ../devenv/env.sh

DESTDIR=${DEVENV_PROJ}/piotbench-fullinstr INSTR_CFLAGS=${CFLAGS_FULLINSTR} ./build-x.sh
