#!/bin/bash -xeu
. ../devenv/env.sh

DESTDIR=${DEVENV_PROJ}/piotbench-noinstr INSTR_CFLAGS="" ./build-x.sh
