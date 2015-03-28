mykill() {
	kill -9 $1 2>/dev/null
	wait $! 2>/dev/null
}

for i in $(seq 0 31); do
	taskset -c 0-$i ./kweb 8080 files/ 100
	trap "mykill $!; exit" SIGHUP SIGINT SIGTERM
done
