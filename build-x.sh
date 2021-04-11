#!/bin/bash -xeu
. ../devenv/env.sh

NPROC=${NPROC:-$(nproc)}
export CC=${CC_RAW}

make clean
make -j${NPROC}

mkdir -p ${DESTDIR}
cp -avp bin/server bin/client runner.sh ${DESTDIR}