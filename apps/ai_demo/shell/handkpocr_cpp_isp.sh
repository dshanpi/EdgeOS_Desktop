#!/bin/sh

set -x
./sq_handkp_ocr.elf hand_det.kmodel None 0.15 0.4 handkp_det.kmodel ocr_det_int16.kmodel 0.15 0.4 ocr_rec_int16.kmodel 0
