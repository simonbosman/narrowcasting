#!/bin/bash
LIMIT=29
a=0

while [ $a -le "$LIMIT" ]
do
	a=$(($a+1))
	cp kempen.pin "$a.pin"
done

exit 0
