# HUNTER-File-System

This repo contains the source code of *Delaying Crash Consistency for Building A High-Performance Persistent Memory File System*.

The key for HUNTER is to delaying metadata writes to background and thus avoids the high overheads of synchronous metadata writes. Compared to SoupFS (one file system using soft update), HUNTER fully decouples the consistent view (PM) and the latest view (DRAM). Besides, HUNTER still maintains atomicity of background metadata updates (e.g., using journal) instead of relying on only I/O ordering for consistency like soft update. 

To support data integrity (durability), (1) fsync is used to flush the metadata to the PM; (2) the metadata is periodically checkpointed to the PM to satisfy the flush-on-fail hardware energy window.

## Requirement

- Linux Kernel 5.1.0 (NOVA)

- The (virtual) machine with Persistent Memory

## Quick Start

We have provided configuration for the HUNTER file system. You can use the following command to setup the HUNTER file system. HUNTER is mounted at `/mnt/pmem0` by default.

```shell
sudo su

bash setup.sh ./config.example.json
```

## Parameters

- `HK_ENABLE_LFS`: Whether enable pure log-structured file system (pure log). Default is disabled.

- `HK_ENABLE_ASYNC`: Whether enable soft-update style asynchronous metadata update. Default is enabled.

- `HK_ENABLE_IDX_ALLOC_PREDICT`: Whether enable the index allocation prediction (using history-window). Default is enabled.

- `HK_CHECKPOINT_INTERVAL`: The interval (in seconds) of the metadata update. Default is 5 seconds.

## Note

Pure LFS mode is not fully tested. It is used in our previous paper *HUNTER: Releasing Persistent Memory Write Performance with A Novel PM-DRAM Collaboration Architecture* for comparison. Note that doing data block GC should be avoided in PM due to its high overheads.

## Publication

- Accpected in ACM/IEEE DAC'23 titled *HUNTER: Releasing Persistent Memory Write Performance with A Novel PM-DRAM Collaboration Architecture*

    ```bibtex
    @inproceedings{pan2023hunter,
    title={HUNTER: Releasing Persistent Memory Write Performance with A Novel PM-DRAM Collaboration Architecture},
    author={Pan, Yanqi and Zhang, Yifeng and Xia, Wen and Zou, Xiangyu and Deng, Cai},
    booktitle={2023 60th ACM/IEEE Design Automation Conference (DAC)},
    pages={1--6},
    year={2023},
    organization={IEEE}
    }
    ```


- Accpected in TCAD titled *Delaying Crash Consistency for Building A High-Performance Persistent Memory File System*. 

    ```bibtex
    @article{pan2024delaying,
    title={Delaying Crash Consistency for Building A High-Performance Persistent Memory File System},
    author={Pan, Yanqi and Huang, Hao and Zhang, Yifeng and Xia, Wen and Zou, Xiangyu and Deng, Cai},
    journal={IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems},
    year={2024},
    publisher={IEEE}
    }
    ```