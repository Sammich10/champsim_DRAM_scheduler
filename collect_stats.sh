#!/bin/bash

for file in sim_results/*; do
    # Extract only the filename without the directory path
    filename=$(basename "$file")
    # Loop through cores from 0 to 24
    for ((i=0; i<=24; i++)); do
        grep -o "Core_${i}_IPC [0-9]*\.[0-9]*" "$file" >> stats/${filename}_stats.txt
    done
done
