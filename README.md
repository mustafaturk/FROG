# FROG
FROG: "Fast Response to Optimization Goals for HTTP Adaptive Streaming over SDN" is an academic research study which optimize QoE for HTTP Adaptive Streaming (HAS) videos.
This repository accompanies the study presenting **FROG** — a novel framework that integrates **Software-Defined Networking (SDN)**, **Common Media Client/Server Data (CMCD/SD)**, and optimization techniques based on **Linear Programming (LP)** and **Mixed-Integer Linear Programming (MILP)**.

The framework addresses key challenges in **HTTP Adaptive Streaming (HAS)**, including:

* Multi-server and multi-path utilization
* Fair and efficient bandwidth allocation
* Dynamic path selection
* Quality oscillation reduction
* Stall minimization under varying network conditions

FROG employs a **decomposition strategy**, where the LP model determines feasible bandwidth allocations and flow paths, which then guide the MILP-based quality selection process.

Experimental results from an emulated testbed demonstrate that FROG achieves **near-optimal Quality of Experience (QoE)** with **low latency** and scales efficiently to **4,000 clients** with approximately **one-second optimization time**. These results highlight the framework’s suitability for **real-time, large-scale deployments**.

Because the study covers a broad evaluation scope, this repository provides **selected results and comparisons** to ensure transparency and reproducibility.
In addition, the **optimization code** used in the study is included here for research and reference purposes.
