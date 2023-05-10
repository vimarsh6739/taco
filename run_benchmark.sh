#!/bin/bash

## Parallel Script to run alias analysis on all testcases provided in the qTACoJava format.   
## Reads input from tests/inputs and tests/outputs and dumps outputs in tests/temp  
## Usage:
## ./run_all.sh [BENCHMARK FOLDER]  

singlerun () {
    input=$1
    cd ${input}
    bname=$(basename ${input})
    echo "Running benchmark: ${bname}"

    make clean > /dev/null
    make build > /dev/null
    make run_taco > /dev/null
    make run_hydride > /dev/null
    
    bname=$(basename ${input})
    
    filename=${bname%.java}
    output="out_hydride.tns"
    expout="out_taco.tns"
    
    fmt_pass="%-40s${GREEN}%-10s${NC}\n"
    fmt_fail="%-40s${RED}%-10s${NC}\n"
    
    # Compare with expected output
    diff -w ${output} ${expout} &> /dev/null

    if [ $? -ne 0 ]; then
        printf ${fmt_fail} "Testing ${bname}: " "FAIL"
        return 1
    else 
        printf ${fmt_pass} "Testing ${bname}: " "PASS"
        return 0
    fi 
}

export RED='\033[0;31m'    # Red color
export NC='\033[0m'        # No Color
export GREEN='\033[0;32m'  # Green Color
export BLUE='\033[0;34m'   # Blue Color

# Export function to be used by GNU Parallel 
export -f singlerun

# Accept commandline args for input and output files
benchdir=$1
if [ -z "$1" ]
  then
    echo -e "${BLUE}WARNING: No input directory provided. Defaulting to benchmarks${NC}"
    benchdir="benchmarks"
fi

# Check number of failures

fail=0

# Run benchmarks
echo -e "${RED}Beginning serial execution of tests.${NC}"
for FOLD in "${benchdir}"/* ; do
    singlerun ${FOLD}
    fail=$(($fail + $?))
done

# Return based on value of fail
if [ $fail -ne 0 ] ; then 
	exit 1
fi
