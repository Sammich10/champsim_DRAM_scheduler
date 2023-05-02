#!/bin/bash

./run_8core.sh FRFCFS HIGH &
./run_8core.sh FRFCFS BALANCED &
./run_8core.sh FRFCFS LOW &
./run_8core.sh ATLAS HIGH &
./run_8core.sh ATLAS BALANCED &
./run_8core.sh ATLAS LOW &
./run_8core.sh BLISS HIGH &
./run_8core.sh BLISS BALANCED &
./run_8core.sh BLISS LOW &