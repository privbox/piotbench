#!/bin/bash -xeu
. ../devenv/env.sh

DESTDIR=${DEVENV_PROJ}/piotbench-instr INSTR_CFLAGS="${CFLAGS_INSTR}" ./build-x.sh
