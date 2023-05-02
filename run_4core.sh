#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: ./run_4core.sh <schedulder::BLISS|ATLAS|FRFCFS> <intensity::HIGH|BALANCED|LOW>"
    exit
fi

if [ $2 == "BALANCED" ];then
    echo "Running 4-core simulation with balanced intensity"
    if [ $1 == "BLISS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-BLISS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/464.h264ref-64B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_BLISS-DRAM_BAL_sim1.txt

    elif [ $1 == "ATLAS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-ATLAS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/464.h264ref-64B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_ATLAS-DRAM_BAL_sim1.txt

    elif [ $1 == "FRFCFS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-FRFCFS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/464.h264ref-64B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_FRFCFS-DRAM_BAL_sim1.txt
    fi

elif [ $2 == "HIGH" ]; then
    echo "Running 4-core simulation with HIGH intensity"
    if [ $1 == "BLISS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-BLISS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/450.soplex-247B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/483.xalancbmk-716B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_BLISS-DRAM_HIGH_sim1.txt

    elif [ $1 == "ATLAS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-ATLAS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/450.soplex-247B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/483.xalancbmk-716B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_ATLAS-DRAM_HIGH_sim1.txt

    elif [ $1 == "FRFCFS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-FRFCFS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/450.soplex-247B.champsimtrace.xz \
        ./traces/429.mcf-22B.champsimtrace.xz \
        ./traces/483.xalancbmk-716B.champsimtrace.xz \
        ./traces/437.leslie3d-232B.champsimtrace.xz \
        > sim_results/4-core_FRFCFS-DRAM_HIGH_sim1.txt
    fi


elif [ $2 == "LOW" ]; then
    echo "Running 4-core simulation with LOW intensity"
    if [ $1 == "BLISS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-BLISS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/464.h264ref-97B.champsimtrace.xz \
        ./traces/473.astar-153B.champsimtrace.xz \
        ./traces/458.sjeng-283B.champsimtrace.xz \
        > sim_results/4-core_BLISS-DRAM_LOW_sim1.txt

    elif [ $1 == "ATLAS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-ATLAS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/464.h264ref-97B.champsimtrace.xz \
        ./traces/473.astar-153B.champsimtrace.xz \
        ./traces/458.sjeng-283B.champsimtrace.xz \
        > sim_results/4-core_ATLAS-DRAM_LOW_sim1.txt

    elif [ $1 == "FRFCFS" ]; then
        ./bin/perceptron-no-no-no-lru-4core-FRFCFS -warmup_instructions 5000000 -simulation_instructions 20000000 \
        -traces ./traces/401.bzip2-277B.champsimtrace.xz \
        ./traces/464.h264ref-97B.champsimtrace.xz \
        ./traces/473.astar-153B.champsimtrace.xz \
        ./traces/458.sjeng-283B.champsimtrace.xz \
        > sim_results/4-core_FRFCFS-DRAM_LOW_sim1.txt
    fi



else
    echo "Usage: ./run_4core.sh <schedulder::BLISS|ATLAS|FRFCFS> <intensity::HIGH|BALANCED|LOW>"
    exit
fi
