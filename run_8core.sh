if [ $# -ne 1 ]; then
    echo "Usage: ./run_8core.sh <schedulder>"
    exit
fi

if [ $1 == "BLISS" ]; then
    ./bin/perceptron-no-no-no-lru-8core-BLISS -warmup_instructions 5000000 -simulation_instructions 20000000 -traces ./traces/401.bzip2-277B.champsimtrace.xz ./traces/429.mcf-22B.champsimtrace.xz ./traces/410.bwaves-2097B.champsimtrace.xz ./traces/437.leslie3d-232B.champsimtrace.xz ./traces/401.bzip2-38B.champsimtrace.xz ./traces/458.sjeng-1088B.champsimtrace.xz ./traces/429.mcf-192B.champsimtrace.xz ./traces/437.leslie3d-273B.champsimtrace.xz > sim_results/8-core_BLISS-DRAM_sim1.txt

elif [ $1 == "ATLAS" ]; then
    ./bin/perceptron-no-no-no-lru-8core-ATLAS -warmup_instructions 5000000 -simulation_instructions 20000000 -traces ./traces/401.bzip2-277B.champsimtrace.xz ./traces/429.mcf-22B.champsimtrace.xz ./traces/410.bwaves-2097B.champsimtrace.xz ./traces/437.leslie3d-232B.champsimtrace.xz ./traces/401.bzip2-38B.champsimtrace.xz ./traces/458.sjeng-1088B.champsimtrace.xz ./traces/429.mcf-192B.champsimtrace.xz ./traces/437.leslie3d-273B.champsimtrace.xz > sim_results/8-core_ATLAS-DRAM_sim1.txt

elif [ $1 == "FRFCFS" ]; then
    ./bin/perceptron-no-no-no-lru-8core-FRFCFS -warmup_instructions 5000000 -simulation_instructions 20000000 -traces ./traces/401.bzip2-277B.champsimtrace.xz ./traces/429.mcf-22B.champsimtrace.xz ./traces/410.bwaves-2097B.champsimtrace.xz ./traces/437.leslie3d-232B.champsimtrace.xz ./traces/401.bzip2-38B.champsimtrace.xz ./traces/458.sjeng-1088B.champsimtrace.xz ./traces/429.mcf-192B.champsimtrace.xz ./traces/437.leslie3d-273B.champsimtrace.xz > sim_results/8-core_FRFCFS-DRAM_sim1.txt
fi
