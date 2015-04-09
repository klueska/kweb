#! /usr/bin/env bash

set-env()
{
	local LIBGOMP_LIB=$LIBGOMP_BASE/libgomp-${1}/$LIBGOMP_LIBDIR
	local TBB_LIB=$TBB_BASE/tbb-${1}/$TBB_LIBDIR
	export LD_LIBRARY_PATH="$MKL_LIB:$LIBGOMP_LIB:$TBB_LIB"
}

run-linux() {
	export VCORE_LIMIT="7"
	export GOMP_CPU_AFFINITY="0-15"
	export OMP_NUM_THREADS="16"
	make clean; make linux
	set-env native
	./kweb 8080 files/ 100
}

run-upthread() {
	export VCORE_LIMIT="16"
	unset GOMP_CPU_AFFINITY
	export OMP_NUM_THREADS="16"
	make clean; make linux-upthread
	set-env upthread
	./kweb 8080 files/ 100
}

run-upthread-lithe() {
	export VCORE_LIMIT="16"
	unset GOMP_CPU_AFFINITY
	export OMP_NUM_THREADS="16"
	make clean; make linux-upthread-lithe
	set-env lithe 
	./kweb 8080 files/ 100
}

run-upthread-native-omp() {
	export VCORE_LIMIT="16"
	export GOMP_CPU_AFFINITY="0-15"
	export OMP_NUM_THREADS="16"
	make clean; make linux-upthread
	set-env native
	./kweb 8080 files/ 100
}

run-upthread-lithe-native-omp() {
	export VCORE_LIMIT="16"
	export GOMP_CPU_AFFINITY="0-15"
	export OMP_NUM_THREADS="16"
	make clean; make linux-upthread-lithe
	set-env native
	./kweb 8080 files/ 100
}

run-linux
run-upthread
run-upthread-lithe
run-upthread-native-omp
run-upthread-lithe-native-omp

