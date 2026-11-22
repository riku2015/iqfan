#!/bin/sh
killall iqfan 2>/dev/null
iqfan --daemon --log /tmp/iqfan.log --port 1235 -f 71000000 -s 2048000 -g 99 -d 1052
