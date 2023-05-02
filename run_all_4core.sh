#!/bin/bash

./run_4core.sh FRFCFS HIGH &
./run_4core.sh FRFCFS BALANCED &
./run_4core.sh FRFCFS LOW &
./run_4core.sh ATLAS HIGH &
./run_4core.sh ATLAS BALANCED &
./run_4core.sh ATLAS LOW &
./run_4core.sh BLISS HIGH &
./run_4core.sh BLISS BALANCED &
./run_4core.sh BLISS LOW &