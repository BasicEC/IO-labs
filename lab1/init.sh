#!/bin/bash

insmod ch_drv.ko
dmesg | tail -n 1
