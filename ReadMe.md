# OMNI-Prom Universal Programmer

This repository documents a variation made by RCL9 of the "OMNI-Prom" programmer outlined in the April 1983 issue of [80 Microcomputing](</Files/OMNIProm%20-%2080%20Micro%2C%20April%201983.pdf>) with new software written in both Z80 assembler and a follow-on version written for Aztec-C. With the "[personality modules](/Images/Img3.jpg)", the programmer could handle 2704, 2708, 2716, 2732/2532, 2732A/462732, 2764, 27128 and 27128 EPROMs. It connected to his various Z80-based computers via the **Universal I/O Port** documented at the [bottom of this page](https://github.com/rcl9/Cypher-Z80-68000-Single-Board-Computer----Expansion-Board).

The following setup shows the programmer attached to RCL's Pied-Piper computer "Universal I/O Port" adaptor and his custom designed linear power supply:

<div style="text-align:center">
<img src="/Images/Img2.jpg" alt="" style="width:75%; height:auto;">
</div>

And the 5 personality modules:

<div style="text-align:center">
<img src="/Images/Img3.jpg" alt="" style="width:75%; height:auto;">
</div>

An interesting aspect of this programmer is that it uses AN-183 and inductor coil to generate as much as 60v at 100ma from the provided 12v power rail. 

## Source Code

The [original driver](/Src/omni.mac) was written in Z80 assembler in 1984 while a [follow-on version](/Src/omni.c) in 1989 was written in Aztec C.

## Programming Modules

Five [personality modules](/Images/Img3.jpg) were designed and built to support 2708, 2716, 2732, 2764 and 27128 EPROMs using these wiring diagrams:

<div style="text-align:center">
<img src="/Images/Modules.jpg" alt="">
</div>
