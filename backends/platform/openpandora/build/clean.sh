#!/bin/sh

echo Quick script to make building all the time less painful.

. /usr/local/angstrom/arm/environment-setup

cd ../../../..

echo Cleaning NovelVM for the OpenPandora.
make clean
