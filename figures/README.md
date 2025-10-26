# FROG Evaluation Results

This repository presents additional evaluation results for **FROG** and the baseline approaches under various network and video configurations.  
The comparisons are shown through figures generated from experiments using the **Elephants Dream (ED)** and **Big Buck Bunny (BBB)** SVC video sets.
Note: "P" suffix → Denotes tests conducted with **Poisson-distributed bandwidth capacities**.

## Figures
### Elephants Dream (ED) Results
| Description | Figure |
|--------------|---------|
| QoE values for ED under Single-server-side setup | ![ED-QoE-Single Server Side](ED-QoE-Single%20Server%20Side.png) |
| Average video quality for ED under Single-server-side setup | ![ED-Quality-Single Server Side](ED-Quality-Single%20Server%20Side.png) |
| Quality variation for ED under Single-server-side setup | ![ED-Quality Change-Single Server Side](ED-Quality%20Change-Single%20Server%20Side.png) |
| Total stalls for ED under Single-server-side setup | ![ED-Stalls-Single Server Side](ED-Stalls-Single%20Server%20Side.png) |

### Big Buck Bunny (BBB) Results
| Description | Figure |
|--------------|---------|
| QoE values for BBB under Single-server-side setup | ![BBB-QoE-Single Server Side](BBB-QoE-Single%20Server%20Side.png) |
| Average video quality for BBB under Single-server-side setup | ![BBB-Quality-Single Server Side](BBB-Quality-Single%20Server%20Side.png) |
| Quality variation for BBB under Single-server-side setup | ![BBB-Quality Change-Single Server Side](BBB-Quality%20Change-Single%20Server%20Side.png) |
| Total stalls for BBB under Single-server-side setup | ![BBB-Stalls-Single Server Side](BBB-Stalls-Single%20Server%20Side.png) |



### Fairness Analysis
| Description | Figure |
|--------------|---------|
| QoE Fairness Index under Poisson-distributed bandwidth | ![Fairness-P](figures/Fairness-P.png) |
Average fairness values for single server-side setup
## Notes

These figures complement the main paper by providing additional experimental insights that could not be included due to page limitations.  
All evaluations use the same optimization, network configurations, and parameter settings as described in the paper.
