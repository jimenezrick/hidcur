#!/bin/sh

gcc -std=c99 -Wall -g -o hidcur hidcur.c `pkg-config --cflags --libs xcb`
