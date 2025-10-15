# FROG Evaluation Results

This repository presents additional evaluation results for **FROG** and the baseline approaches under various network and video configurations.  
The comparisons are shown through figures generated from experiments using the **Elephants Dream (ED)** and **Big Buck Bunny (BBB)** SVC video sets.

## Scenario Naming Convention

Each figure name follows the format:

- **ED-1x1y** → Evaluation of the *Elephants Dream* SVC video set with  
  • **1x**: one server-side OpenFlow (OF) switch  
  • **1y**: one client-side OF switch  

- **BBB-4x2y** → Evaluation of the *Big Buck Bunny* SVC video set with  
  • **4x**: four server-side OF switches  
  • **2y**: two client-side OF switches  

- **P** suffix → Denotes tests conducted with **Poisson-distributed bandwidth capacities**.

## Figures

### Big Buck Bunny (BBB) Results
| Description | Figure |
|--------------|---------|
| QoE values for BBB under Single-server-side setup | ![BBB-QoE-Single Server Side](BBB-QoE-Single Server Side.png) |
| Average video quality for BBB under Single-server-side setup | ![BBB-Quality-Single Server Side](BBB-Quality-Single Server Side.png) |
| Quality variation for BBB under Single-server-side setup | ![BBB-Quality Change-Single Server Side](BBB-Quality Change-Single Server Side.png) |
| Total stalls for BBB under Single-server-side setup | ![BBB-Stalls-Single Server Side](BBB-Stalls-Single Server Side.png) |

### Elephants Dream (ED) Results
| Description | Figure |
|--------------|---------|
| QoE values for ED (1x1y) | ![ED-1x1y-QoE](figures/ED-1x1y-QoE.png) |
| Average video quality for ED (1x1y) | ![ED-1x1y-Quality](ED-1x1y-Quality.png) |
| Quality change for ED (1x1y) | ![ED-1x1y-QChange](ED-1x1y-Quality-Change.png) |
| Total stalls for ED (1x1y) | ![ED-1x1y-Stalls](ED-1x1y-Stalls.png) |
| QoE results with Poisson-distributed bandwidth (ED-P) | ![ED-P-QoE](ED-P-QoE.png) |

### Fairness Analysis
| Description | Figure |
|--------------|---------|
| QoE Fairness Index under Poisson-distributed bandwidth | ![Fairness-P](figures/Fairness-P.png) |

## Notes

These figures complement the main paper by providing additional experimental insights that could not be included due to page limitations.  
All evaluations use the same optimization, network configurations, and parameter settings as described in the paper.
