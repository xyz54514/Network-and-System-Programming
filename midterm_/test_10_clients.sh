#!/usr/bin/env bash
set -u

export LD_LIBRARY_PATH="$PWD:${LD_LIBRARY_PATH:-}"

# Start the server separately before running this script:
#   ./server --debug=1
#
# Each client stays connected for ~8 seconds, which gives time to prove
# that the parent server has at least 10 child processes concurrently.

for i in $(seq 1 10); do
    (
        printf 'Client%02d\n' "$i"
        sleep 8
        printf 'SYSINFO\n'
        sleep 1
        printf 'QUIT\n'
    ) | ./client > "client_${i}.log" 2>&1 &
done

sleep 2

echo "===== Runtime proof: server processes ====="
ps -ef | grep '[s]erver'

echo
echo "Ten clients are still connected for several seconds."
echo "You can take a screenshot now."

wait

echo "All client tests finished. Logs: client_1.log ... client_10.log"
