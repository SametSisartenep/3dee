#!/bin/awk -f
BEGIN{ doprint=0 }
{ if($1 == "$$EOE") doprint=0; if(doprint) print; if($1 == "$$SOE") doprint=1; }
