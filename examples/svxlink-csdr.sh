#!/bin/sh
nc 127.0.0.1 1235 | csdr convert_u8_f | csdr shift_addition_cc 0 | \
csdr fir_decimate_cc 8 0.005 HAMMING | csdr fmdemod_atan_cf | csdr limit_ff | \
csdr gain_ff 0.8 | csdr convert_f_s16 | nc -u 127.0.0.1 4444
