# TuringCell: Run Linux over Paxos/Raft

![TuringCell Logo](img/logo_small_icon.png)

This article belongs to the open source project [turingcell](https://turingcell.org). This article focuses on the principle, a simple proof of Turing Cell computing model, and its corresponding implementation -- the core design of TuringCell computer.

Turing Cell Model is a computer model which runs over distributed consensus algorithm (like Paxos/Raft). TuringCell computer is an open source implementation of the Turing Cell Model. With TuringCell, you can easily add the characteristics of a Paxos/Raft group like high availability, high consensus, and fault tolerance to nearly any already existing software. At the meantime, TuringCell is a business friendly opensource project. Its core strength is from an open and inclusive opensource community. No matter where you come from and what language you speak, you can join in to develop and change the future of TuringCell equally and freely!

作者：Sen Han (韩森) 00hnes@gmail.com

# Table of Content

   * [0 What is Turing Cell?](#0-what-is-turing-cell)
   * [1 Details and Proof of Turing Cell Model](#1-details-and-proof-of-turing-cell-model)
      * [1.1 Replicated State Machine Model](#11-replicated-state-machine-model)
      * [1.2 Common Computer Running Model](#12-common-computer-running-model)
      * [1.3 TuringCell Theorem](#13-turingcell-theorem)
   * [2 The Design and Implementation of TuringCell Computer](#2-the-design-and-implementation-of-turingcell-computer)
      * [2.1 Choice of CPU](#21-choice-of-cpu)
      * [2.2 Core I/O Devices](#22-core-io-devices)
      * [2.3 Distributed Infinitely-Long Instruction Tape](#23-distributed-infinitely-long-instruction-tape)
      * [2.4 More Detailed Design](#24-more-detailed-design)
   * [3 Community, Support and Cooperation](#3-community-support-and-cooperation)
   * [4 Donate](#4-donate)
   * [5 History of Versions](#5-history-of-versions)
   * [6 Copyright and License](#6-copyright-and-license)

## 0 What is Turing Cell?

![turingcell_computer_and_common_computer_arch](img/turingcell_computer_and_common_computer_arch.png)

**Turing Machine + Paxos/Raft = Turing Cell**

A Turing Cell computer is an implementation of Turing Cell model, which just like the common computer is the implementation of the Turing Machine model.

A Turing Cell computer has no big differences with a common computer except it runs over a Paxos/Raft group and thus it has all those natural supports for high availabilty, fault tolerance, high consensus and any property that a Paxos/Raft group could has (others like membership changement, multi-master in Paxos and etc).

So, you can run any operating system and its corresponding userspace applications over a Turing Cell computer just as you could do on a common computer as long as this operating system supports the ISA and I/O devices of this certain Turing Cell computer implementation. In addition to the running of Linux which the OS needs MMU, you could also even choose to run a RTOS without MMU requirement or even the bare metal way if you really care about the performance of your distributed applications.

What is "Cell"?

The cell is one of the fundamental elements of biological tissue. After receiving a certain degree of non-fatal injury, the general biological tissue can recover from the damage, which is called "regeneration" in biology. At the same time of regeneration, the tissue can ensure that its function in the overall system is in a certain degree of the normal state. One of the initial design intentions of the TuringCell computer is to have a certain degree of similar biological regeneration feature.

## 1 Details and Proof of Turing Cell Model

### 1.1 Replicated State Machine Model

![tape_of_RSM](img/RSM.png)

What is Replicated State Machine?

Replicated State Machines, abbr. RSM:

Based on the distributed consensus algorithm (for example, by combining infinite Paxos instances sequentially, for more information about this topic please refer 《[Paxos Made Easy](https://github.com/turingcell/paxos-made-easy)》), to build a consensus, high-available and fault-tolerant distributed infinitely-long instruction tape. Several state machine executors with the same initial state execute the instructions on the tape sequentially. If each instruction on the tape is mathematically deterministic, then it is certain that when all state machine executors finish the execution of a same instruction at the same position on the tape, the internal state of all state machines must be the same. This model is called the "replicated state machines model". The essence of the replication state machine is to obtain a consensus, high-available and fault-tolerant distributed state machine through a consensus, high-available and fault-tolerant, infinitely long distributed instruction tape.

Definition of the mathematical deterministic function 𝘮𝘥𝑓:

&emsp;**Mathematical deterministic function 𝘮𝘥𝑓**&emsp;**In any case, as long as the mdf function is given a certain input state S1, then it must be able to uniquely and definitely map to a certain output state S2**

&emsp;&emsp;**S2 = mdf(S1)**

In this way, we can get the mdf-version definition of the RSM

&emsp;Multiple state machine executors with the same initial state execute the instructions on the distributed instruction tape sequentially. **The instruction on the tape is a kind of mdf function**. Due to the characteristics of the mdf function, then it is certain that when all state machine executors finished the executing of the same 𝘮𝘥𝑓 function at the same position on the tape, the internal states of all these state machines are necessarily the same. This model is called the "replicated state machine" model.

![mdf-RSM-diagram](img/mdf-RSM-diagram.png)

### 1.2 Common Computer Running Model

![state_registers_of_common_computer](img/state_registers_of_common_computer.png)

```
state{cpu regsters' state, memory state, i/o devices' state}
```

In a sense, the running of the common computer can be viewed as a continuous sequence of the state transition.

To simplify the model, we will only discuss the computer has single-cycle-instruction architecture below. Of course, these conclusions can be easily extended to more general computers.

&emsp;Suppose that the state of the running computer at the beginning of the clock cycle t1 is denoted as S1, and the state of the beginning of the clock cycle t2 is denoted as S2. It is known that t2 > t1, then there must exist a function f of state mapping, which satisfies

&emsp;**S2 = f(S1)**

&emsp;Then we can regard the running of the computer as the sequential execution of one after another such f-functions (that is, the f-function is equivalent to the CPU instruction to execute in the current clock cycle).

![f-common-computer-diagram](img/f-common-computer-diagram.png)

### 1.3 TuringCell Theorem

In summary, we could get

&emsp;**TuringCell Theorem**&emsp;**For an running common computer 𝒞, its 𝑓 function is 𝑓n, where n is any non-negative integer. There also exists a replicated state machine running model ℛ, and its 𝘮𝘥𝑓 function is 𝘮𝘥𝑓n, where n is any non-negative integer. If the proposition "𝑓n and 𝘮𝘥𝑓n are all equivalent" is true, then we could call 𝒞 and ℛ is equivalent from the perspective of state transition.**

For a common computer, all CPU instructions can be regarded as a mdf function. The only thing that introduces "random" into the state of the system is the I/O device -- the fundamental reason of that is the nature of I/O is the interaction between the system and the outside world. In order to mdf-functionalize all these I/O devices, we can move all the parts that could generate "random" data out of the RSM itself -- that is, by introducing the I/O mechanism via the definition of special I/O instructions in the RSM, thus the RSM can interact with the outside world from the perspective of mdf function. 

Then, we could get

&emsp;**The equivalence of state transition between 𝒞 and ℛ stated in TuringCell theorem do exist and could also be implemented**

## 2 The Design and Implementation of TuringCell Computer

![TuringCell-Computer-Architecture-v0.1-and-v1.0](img/TuringCell-Computer-Architecture-v0.1-and-v1.0.png)

TuringCell computer v0.1 is for the smallest prototype verification, and v1.0 version is the 1st stable version which could be used in the production environment.

### 2.1 Choice of CPU

```
x86 or x64
    Cost of Implementation: 
        Large amount of engineering cost, simply all physical work. Because the decoder of CISC CPU is more complicated, the interpretation performance of CISC-CPU will have certain disadvantages compared to RISC-CPU
    Ecological prosperity of compiler toolchain: 
        Excellent
    Maintainability: 
        Poor
ARM
    Cost of Implementation: 
        RSIC has rather low implementation cost, simple decoding, and instructions are more concise than CISC
    Ecological prosperity of compiler toolchain: 
        Good
    Maintainability: 
        Good
MIPS
    Cost of Implementation: 
        RSIC has rather low implementation cost, simple decoding, and instructions are more concise than CISC; MIPS is even simpler than ARM
    Ecological prosperity of compiler toolchain: 
        Not so good compared to ARM
    Maintainability:
        Good

Hardware digital circuits like AISC/FPGA is very good at CISC instruction decoding because they are just simple parallelable combinatorial logic circuits whereas the common instruction-executing-serially CPU is not. In contrast with CISC, RISC decoding is much simpler and straightforward, thus the performance penalty would be much less.

So, it is finally decided that the v0.1 version would choose to emulate the ARMv4 architecture and further the ARMv5 architecture in the 1st stable release version, i.e. v1.0.
```

In addition, the TuringCell computer also supports addons for extending other kinds of CPU implementations.

### 2.2 Core I/O Devices

timer: It is essential. For example, it is used to implement time sharing and preemption between multiple tasks in the operating system.

UART: It is probably the simplest general-purpose I/O device that can interact with the outside world. For example, it can be used as a console, or simply as a general data link for communication with other external systems.

Disk: persistent storage block devices.

Interrupt controller: Listens for the event status of all I/O devices, and notifies the CPU of the information interested by the CPU in the form of events, which provides the CPU with another option besides polling the status register of I/O devices.

In addition, the TuringCell computer also supports addons for extending other types of CPU implementations.

In addition, the TuringCell computer also supports addons for extending other kinds of I/O devices implementations.

### 2.3 Distributed Infinitely-Long Instruction Tape

As the prototype verification version, V0.1 chooses to use etcd underneath the RSM temporarily.

As the first stable release version, v1.0 is implemented with a Paxos group which developed by the TuringCell community itself, including but not limited to the following features:

1. Egalitarianism
2. Multi-Master
3. Out-of-order and parallel chosen
4. Dynamic election and with a variable election weight of each master 
5. Membership changement
6. Single-RTT chosen for ordinary operations
7. Optimizations for the complex WAN environment 
8. Service can be a basic component in any other project, and the goal is to become one of the best choices in the industry's open source implementation of distributed consistency algorithm
9. Any other cool ideas is welcome

### 2.4 More Detailed Design

For more detailed design of TuringCell computer please refer to [this document](https://github.com/turingcell/turingcell/blob/master/design_in_detail.md).

## 3 Community, Support and Cooperation

Welcome to join the [TuringCell community](https://github.com/turingcell/join-community)!

Your participation, support and feedback are essential to this open source project! The exchange and collision of ideas, openness and inclusiveness, equality and freedom are always the charms of open source! Become a member of the TuringCell community, let us build the next exciting distributed opensource project together!

You can choose to join the mailing list, Wechat group, apply to be a member of TuringCell GitHub organization, share and spread, ask questions, star/watch/follow, donate, etc. to support this project.

In addition, any form of cooperation is very welcome. Please [contact](https://github.com/turingcell/contact) me.

## 4 Donate

Thank you very much for your [generous donation](https://github.com/turingcell/donate)!

## 5 History of Versions

```
v0.01 2017.5
    Sen Han (韩森) <00hnes@gmail.com>

v0.2  2017.12-2018.2 
    Sen Han (韩森) <00hnes@gmail.com>

v0.9   2020.5 
    Sen Han (韩森) <00hnes@gmail.com>
```

## 6 Copyright and License

Author: Sen Han (韩森) <00hnes@gmail.com>

Website: https://turingcell.org/

License: This article is licensed under the [Creative Commons Attribution-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-sa/4.0/), except the picture of the [TuringCell Logo](https://github.com/turingcell/logo) which is under the [Creative Commons Attribution-NoDerivatives 4.0 International License](http://creativecommons.org/licenses/by-nd/4.0/).

本文开头处的[TuringCell Logo](https://github.com/turingcell/logo)图片采用[知识共享署名-禁止演绎 4.0 国际许可协议](http://creativecommons.org/licenses/by-nd/4.0/)进行许可，文中除此logo之外的部分均采用[知识共享署名-相同方式共享 4.0 国际许可协议](https://creativecommons.org/licenses/by-sa/4.0/)进行许可。
