; SPDX-License-Identifier: MPL-2.0
; Deneb-authored behavior-compatible finish cleanup.
M400
G91
G1 Z3
G90
G28 X Y
G28 Z
M104 S0
M140 S0
M107
M400
M84
