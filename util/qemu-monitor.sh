#!/bin/sh

socat - echo=0,icanon=0 unix-connect:qemu-monitor-socket