#!/bin/bash
set -e
cd "$(dirname "${BASH_SOURCE[0]}")"

rm -f Dockerfile
cat ../Dockerfile Dockerfile.prep > Dockerfile
docker build -t audit-lib-usage .
docker run --rm --privileged --name audit-lib-usage-tmp -v "$(pwd):/data" audit-lib-usage bash -c '/root/fanotify_monitor /root > /data/output.log' &
sleep 1
echo "Running fanotify_monitor in the background..."
echo "Your log is in: $(realpath output.log)"
echo "When done, exit gracefully from this shell to allow proper cleanup."
docker exec -it audit-lib-usage-tmp bash || true
docker exec -it audit-lib-usage-tmp bash -c 'kill -2 $(pidof fanotify_monitor)'
echo "Your log is in: $(realpath output.log)"
sleep 5
docker rmi audit-lib-usage
rm -f Dockerfile
