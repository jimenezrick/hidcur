#!/bin/sh

gcc -g -Wall -o hidcur hidcur.c `pkg-config --cflags --libs xcb`
